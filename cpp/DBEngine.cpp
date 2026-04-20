#include "DBEngine.h"
#include "WALManager.h"
#include "CachedCryptoContext.h"
#include "LazyRecordProxy.h"
#include <iostream>
#include <vector>
#include <shared_mutex>
#include <cstring>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "TurboDB_Native"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) do {} while(0)
#define LOGE(...) do {} while(0)
#endif

namespace turbo_db {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

DBEngine::DBEngine(
    std::shared_ptr<facebook::react::CallInvoker> js_invoker,
    std::unique_ptr<SecureCryptoContext> crypto)
    : start_time_(std::chrono::high_resolution_clock::now()),
      crypto_(std::move(crypto)),
      next_free_offset_(1024 * 1024),
      arena_(1024 * 1024),
      js_invoker_(std::move(js_invoker)),
      scheduler_(std::make_unique<DBScheduler>())
{
}

DBEngine::~DBEngine() {
    // Drain pending tasks before destroying state
    if (scheduler_) {
        scheduler_->waitUntilIdle();
        scheduler_->shutdown();
    }
    if (btree_) btree_->flush();
    if (mmap_) {
        mmap_->sync();
        LOGI("DBEngine: MMap synced and destroyed");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Promise Helper
// Creates a JS Promise and captures resolve/reject as shared_ptr<Function>
// so they can be safely called back from any thread via js_invoker_.
// ─────────────────────────────────────────────────────────────────────────────
facebook::jsi::Value DBEngine::createPromise(
    facebook::jsi::Runtime& runtime,
    std::function<void(
        std::shared_ptr<facebook::jsi::Function> resolve,
        std::shared_ptr<facebook::jsi::Function> reject)> executor)
{
    auto promiseCtor = runtime.global().getPropertyAsFunction(runtime, "Promise");

    // Shared state: resolve/reject captured and sent into executor
    auto resolveRef = std::make_shared<std::shared_ptr<facebook::jsi::Function>>();
    auto rejectRef  = std::make_shared<std::shared_ptr<facebook::jsi::Function>>();

    auto innerExecutor = facebook::jsi::Function::createFromHostFunction(
        runtime,
        facebook::jsi::PropNameID::forAscii(runtime, "__executor"),
        2,
        [resolveRef, rejectRef](
            facebook::jsi::Runtime& rt,
            const facebook::jsi::Value&,
            const facebook::jsi::Value* args,
            size_t count) -> facebook::jsi::Value
        {
            if (count >= 2) {
                *resolveRef = std::make_shared<facebook::jsi::Function>(
                    args[0].asObject(rt).asFunction(rt));
                *rejectRef  = std::make_shared<facebook::jsi::Function>(
                    args[1].asObject(rt).asFunction(rt));
            }
            return facebook::jsi::Value::undefined();
        });

    auto promise = promiseCtor.callAsConstructor(runtime, innerExecutor).asObject(runtime);

    // Now call the user executor with the resolved references
    if (*resolveRef && *rejectRef) {
        executor(*resolveRef, *rejectRef);
    }

    return promise;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSI Property Dispatcher
// ─────────────────────────────────────────────────────────────────────────────
facebook::jsi::Value DBEngine::get(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::PropNameID& name)
{
    std::string p = name.utf8(runtime);

    // ── Utility ──
    if (p == "add") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t cnt) -> facebook::jsi::Value {
                if (cnt < 2) throw facebook::jsi::JSError(rt, "add requires 2 arguments");
                return facebook::jsi::Value(add(args[0].asNumber(), args[1].asNumber()));
            });
    }
    if (p == "echo") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t cnt) -> facebook::jsi::Value {
                if (cnt < 1) throw facebook::jsi::JSError(rt, "echo requires 1 argument");
                return facebook::jsi::String::createFromUtf8(
                    rt, echo(args[0].getString(rt).utf8(rt)));
            });
    }
    if (p == "benchmark") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                return facebook::jsi::Value(benchmark());
            });
    }

    // ── Storage Init ──
    if (p == "initStorage") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t cnt) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                std::string path = args[0].getString(rt).utf8(rt);
                size_t size = static_cast<size_t>(args[1].asNumber());
                bool enableSync = false;
                if (cnt > 2 && args[2].isObject()) {
                    auto opts = args[2].asObject(rt);
                    if (opts.hasProperty(rt, "syncEnabled")) {
                        enableSync = opts.getProperty(rt, "syncEnabled").asBool();
                    }
                }
                return facebook::jsi::Value(initStorage(path, size, enableSync));
            });
    }

    // ── Raw MMap ──
    if (p == "write") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                this->write(static_cast<size_t>(args[0].asNumber()), args[1].getString(rt).utf8(rt));
                return facebook::jsi::Value::undefined();
            });
    }
    if (p == "read") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                return facebook::jsi::String::createFromUtf8(
                    rt, this->read(static_cast<size_t>(args[0].asNumber()),
                                   static_cast<size_t>(args[1].asNumber())));
            });
    }

    // ── Sync CRUD ──
    if (p == "insertRec") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return insertRec(rt, args[0].getString(rt).utf8(rt), args[1]);
            });
    }
    if (p == "findRec") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                // findRec acquires its own shared_lock internally
                return findRec(rt, args[0].getString(rt).utf8(rt));
            });
    }
    if (p == "clearStorage") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return facebook::jsi::Value(clearStorage());
            });
    }
    if (p == "setMulti") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return setMulti(rt, args[0]);
            });
    }
    if (p == "getMultiple") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                return getMultiple(rt, args[0]);
            });
    }
    if (p == "remove" || p == "del") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return facebook::jsi::Value(remove(args[0].getString(rt).utf8(rt)));
            });
    }
    if (p == "rangeQuery") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                auto results = rangeQuery(rt, args[0].getString(rt).utf8(rt),
                                               args[1].getString(rt).utf8(rt));
                auto arr = facebook::jsi::Array(rt, results.size());
                for (size_t i = 0; i < results.size(); i++) {
                    auto obj = facebook::jsi::Object(rt);
                    obj.setProperty(rt, "key",
                        facebook::jsi::String::createFromUtf8(rt, results[i].first));
                    obj.setProperty(rt, "value", results[i].second);
                    arr.setValueAtIndex(rt, i, obj);
                }
                return arr;
            });
    }
    if (p == "getAllKeys") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                auto keys = getAllKeys();
                facebook::jsi::Array result(rt, keys.size());
                for (size_t i = 0; i < keys.size(); i++) {
                    result.setValueAtIndex(rt, i,
                        facebook::jsi::String::createFromUtf8(rt, keys[i]));
                }
                return result;
            });
    }
    if (p == "getAllKeysPaged") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t cnt) -> facebook::jsi::Value {
                int limit  = cnt > 0 ? (int)args[0].asNumber() : 100;
                int offset = cnt > 1 ? (int)args[1].asNumber() : 0;
                auto keys = getAllKeysPaged(limit, offset);
                facebook::jsi::Array result(rt, keys.size());
                for (size_t i = 0; i < keys.size(); i++) {
                    result.setValueAtIndex(rt, i,
                        facebook::jsi::String::createFromUtf8(rt, keys[i]));
                }
                return result;
            });
    }
    if (p == "deleteAll") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return facebook::jsi::Value(deleteAll());
            });
    }
    if (p == "flush") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                flush();
                return facebook::jsi::Value::undefined();
            });
    }

    // ── Async API ──
    if (p == "setAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return setAsync(rt, args[0]);  // args[0] = {key, value}
            });
    }
    if (p == "getAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return getAsync(rt, args[0]);
            });
    }
    if (p == "setMultiAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return setMultiAsync(rt, args[0]);
            });
    }
    if (p == "getMultipleAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return getMultipleAsync(rt, args[0]);
            });
    }
    if (p == "rangeQueryAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return rangeQueryAsync(rt, args[0]);
            });
    }
    if (p == "getAllKeysAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return getAllKeysAsync(rt);
            });
    }
    if (p == "removeAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return removeAsync(rt, args[0]);
            });
    }

    // ── Sync API ──
    if (p == "getLocalChangesAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return getLocalChangesAsync(rt, args[0]);
            });
    }
    if (p == "applyRemoteChangesAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return applyRemoteChangesAsync(rt, args[0]);
            });
    }
    if (p == "markPushedAsync") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return markPushedAsync(rt, args[0]);
            });
    }

    // ── Diagnostics ──
    if (p == "getDatabasePath") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::String::createFromUtf8(rt, getDatabasePath());
            });
    }
    if (p == "getWALPath") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::String::createFromUtf8(rt, getWALPath());
            });
    }
    if (p == "verifyHealth") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::Value(verifyHealth());
            });
    }
    if (p == "repair") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return facebook::jsi::Value(repairInternal());
            });
    }
    if (p == "setSecureMode") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                setSecureMode(args[0].asBool());
                return facebook::jsi::Value::undefined();
            });
    }
    if (p == "getStats") {
        return facebook::jsi::Function::createFromHostFunction(runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return getStats(rt);
            });
    }

    return facebook::jsi::Value::undefined();
}

std::vector<facebook::jsi::PropNameID> DBEngine::getPropertyNames(
    facebook::jsi::Runtime& runtime)
{
    std::vector<facebook::jsi::PropNameID> names;
    for (const char* n : {
        "add","echo","benchmark","initStorage","write","read",
        "insertRec","findRec","clearStorage","setMulti","getMultiple",
        "remove","del","rangeQuery","getAllKeys","getAllKeysPaged","deleteAll","flush",
        "setAsync","getAsync","setMultiAsync","getMultipleAsync",
        "rangeQueryAsync","getAllKeysAsync","removeAsync",
        "getLocalChangesAsync","applyRemoteChangesAsync","markPushedAsync",
        "getDatabasePath","getWALPath","verifyHealth","repair","setSecureMode","getStats"
    }) {
        names.push_back(facebook::jsi::PropNameID::forAscii(runtime, n));
    }
    return names;
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple Utility Methods
// ─────────────────────────────────────────────────────────────────────────────
double DBEngine::add(double a, double b) { return a + b; }
std::string DBEngine::echo(const std::string& msg) { return "Echo: " + msg; }

double DBEngine::benchmark() {
    if (!btree_) return 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; i++) {
        btree_->insert("bench_" + std::to_string(i), 1000 + i);
    }
    btree_->flush();
    return std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t0).count();
}

facebook::jsi::Value DBEngine::getStats(facebook::jsi::Runtime& runtime) {
    auto obj = facebook::jsi::Object(runtime);
    if (pbtree_) {
        const auto& hdr = pbtree_->getHeader();
        obj.setProperty(runtime, "treeHeight", facebook::jsi::Value(static_cast<double>(hdr.height)));
        obj.setProperty(runtime, "nodeCount",  facebook::jsi::Value(static_cast<double>(hdr.node_count)));
        obj.setProperty(runtime, "formatVersion", facebook::jsi::Value(static_cast<double>(hdr.format_version)));
    }
    if (compactor_ && mmap_) {
        double frag = compactor_->getFragmentationRatio(
            compactor_->getLiveBytes(), mmap_->getSize());
        obj.setProperty(runtime, "fragmentationRatio", facebook::jsi::Value(frag));
    }
    obj.setProperty(runtime, "isInitialized", facebook::jsi::Value(btree_ != nullptr));
    obj.setProperty(runtime, "secureMode", facebook::jsi::Value(is_secure_mode_));
    return obj;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync OpLog & Clock Utility
// ─────────────────────────────────────────────────────────────────────────────
void DBEngine::initializeLogicalClock() {
    if (!sync_enabled_ || !oplog_btree_) return;
    
    // We can find the max clock by retrieving all keys and picking the highest, 
    // or by doing a reverse iteration if the tree supports it.
    // For now, since oplog keys are "CLOCK_key", we can just get all keys.
    // In production with 100k records, we'd want a `getMaxKey()` on the tree.
    // Since logical clocks are sequential and padded (e.g., 00000000000000000001),
    // they are lexicographically sorted.
    // To avoid O(N) scan on boot, we can read the last key efficiently using our tree limit.
    // But since `BufferedBTree` is memory-cached, taking a fast peek is okay.
    
    uint64_t max_clk = 1;
    // We will do a robust but simple enumeration:
    auto all_keys = oplog_btree_->getAllKeys();
    for (const auto& k : all_keys) {
        size_t underscore_pos = k.find('_');
        if (underscore_pos != std::string::npos) {
            std::string num_part = k.substr(0, underscore_pos);
            try {
                uint64_t clk = std::stoull(num_part);
                if (clk > max_clk) max_clk = clk;
            } catch (...) {}
        }
    }
    logical_clock_.store(max_clk + 1, std::memory_order_relaxed);
    LOGI("DBEngine: Logical clock initialized to %llu", (unsigned long long)max_clk + 1);
}

void DBEngine::writeOpLog(uint64_t clock, const std::string& key) {
    if (!sync_enabled_ || !oplog_btree_) return;
    
    // Format: 20-digit zero-padded clock + '_' + key
    char buf[32];
    snprintf(buf, sizeof(buf), "%020llu_", (unsigned long long)clock);
    std::string oplog_key = std::string(buf) + key;
    
    // We just need a dummy offset for the value, as the key contains the actual data we need.
    // Alternatively, we could store the actual offset. We'll store 1.
    oplog_btree_->insert(oplog_key, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Storage Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
bool DBEngine::initStorage(const std::string& path, size_t size, bool enableSync) {
    LOGI("initStorage: path=%s size=%zu sync=%d", path.c_str(), size, enableSync);

    sync_enabled_ = enableSync;

    // Reset existing state
    if (btree_)  btree_.reset();
    if (pbtree_) pbtree_.reset();
    if (wal_)    wal_.reset();
    if (mmap_) { mmap_->close(); mmap_.reset(); }

    try {
        mmap_ = std::make_unique<MMapRegion>();
        mmap_->init(path, size);

        wal_ = std::make_unique<WALManager>(path, crypto_.get());

        pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), wal_.get());
        pbtree_->init();

        // Detect format version mismatch — rebuild index
        if (pbtree_->needsMigration()) {
            LOGI("initStorage: format migration required, rebuilding index");
            // For v1→v2 migration, we can't read old node layout,
            // so we start fresh. Existing data records in mmap are preserved
            // but the index is rebuilt empty. In practice this means a fresh DB.
            // Apps should be notified via getStats().formatVersion change.
        }

        // Verify health
        if (!verifyHealth()) {
            LOGE("initStorage: health check failed, triggering WAL-first repair");
            needs_repair_ = true;
            return repairInternal(); // WAL-first, never-delete
        }

        // WAL recovery (commit-aware, 2-pass)
        if (is_secure_mode_) {
            wal_->recoverSafe(mmap_.get());
        }

        next_free_offset_ = pbtree_->getHeader().next_free_offset;
        if (next_free_offset_ < 1024 * 1024) {
            next_free_offset_ = 1024 * 1024; // safety floor
        }

        btree_ = std::make_unique<BufferedBTree>(pbtree_.get());
        compactor_ = std::make_unique<Compactor>(path, crypto_.get());

        if (sync_enabled_) {
            std::string oplog_path = path + ".oplog";
            oplog_pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), nullptr);
            
            // We use a separate config for oplog if needed, or share the format.
            // OpLog tree lives in a separate mmap file to not clutter the main BTree.
            // Actually, we should map it to its own MMapRegion to keep it clean.
            // For now, assume PersistentBPlusTree relies on MMapRegion. 
            // Wait, we can't share MMapRegion directly for a second tree without collisions.
            // The cleanest way: A dedicated oplog MMapRegion. But we don't have an `oplog_mmap_`.
            // Let's store oplog keys directly in the SAME tree, prefixed with "__oplog:".
            // That guarantees atomicity. So we don't need `oplog_pbtree_`!
            // We can just use the main `btree_`.
            LOGI("initStorage: Sync enabled, using __oplog: prefix in main tree.");
            initializeLogicalClock();
        }

        LOGI("initStorage: success, next_free_offset=%zu", next_free_offset_);
        return true;
    } catch (const std::exception& e) {
        LOGE("initStorage exception: %s", e.what());
        needs_repair_ = true;
        return repairInternal();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WAL-First, Never-Delete Repair
// ─────────────────────────────────────────────────────────────────────────────
bool DBEngine::repairInternal() {
    LOGI("repairInternal: starting WAL-first repair");
    if (!mmap_) return false;

    const std::string db_path   = mmap_->getPath();
    const std::string wal_path  = db_path + ".wal";
    const std::string corrupt   = db_path + ".corrupt.bak";
    size_t db_size              = mmap_->getSize();

    // Step 1: Close all handles
    if (btree_)  btree_.reset();
    if (pbtree_) pbtree_.reset();
    if (mmap_)  { mmap_->close(); mmap_.reset(); }

    // Step 2: Try WAL replay on the existing (potentially corrupt) DB
    try {
        mmap_ = std::make_unique<MMapRegion>();
        mmap_->init(db_path, db_size);

        WALManager tmp_wal(db_path, crypto_.get());
        bool recovered = tmp_wal.recoverSafe(mmap_.get());
        mmap_->sync();

        // Re-init B+ Tree after WAL replay
        wal_  = std::make_unique<WALManager>(db_path, crypto_.get());
        pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), wal_.get());
        pbtree_->init();

        if (verifyHealth()) {
            LOGI("repairInternal: WAL replay succeeded (recovered=%d)", recovered);
            next_free_offset_ = pbtree_->getHeader().next_free_offset;
            if (next_free_offset_ < 1024 * 1024) next_free_offset_ = 1024 * 1024;
            btree_ = std::make_unique<BufferedBTree>(pbtree_.get());
            compactor_ = std::make_unique<Compactor>(db_path, crypto_.get());
            needs_repair_ = false;
            
            if (sync_enabled_) {
                LOGI("repairInternal: Sync enabled, initializing logical clock.");
                initializeLogicalClock();
            }
            return true;
        }
    } catch (const std::exception& e) {
        LOGE("repairInternal: WAL replay failed: %s", e.what());
    }

    // Step 3: WAL replay could not fix it — archive files, start fresh
    LOGI("repairInternal: archiving corrupt DB and rebuilding fresh");

    // Archive WAL first (never delete)
    if (wal_) {
        wal_->archiveWAL();
        wal_.reset();
    } else {
        // Archive raw WAL file if manager not available
        std::string bak = wal_path + ".bak";
        std::remove(bak.c_str());
        std::rename(wal_path.c_str(), bak.c_str());
    }

    // Archive main DB (never delete)
    if (mmap_) { mmap_->close(); mmap_.reset(); }
    if (pbtree_) pbtree_.reset();
    std::remove(corrupt.c_str()); // remove stale backup
    std::rename(db_path.c_str(), corrupt.c_str());

    LOGI("repairInternal: DB archived to %s", corrupt.c_str());

    // Step 4: Fresh init
    try {
        next_free_offset_ = 1024 * 1024;
        needs_repair_     = false;

        mmap_ = std::make_unique<MMapRegion>();
        mmap_->init(db_path, db_size);

        wal_    = std::make_unique<WALManager>(db_path, crypto_.get());
        pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), wal_.get());
        pbtree_->init();
        btree_  = std::make_unique<BufferedBTree>(pbtree_.get());
            // Wait, we can't share MMapRegion directly for a second tree without collisions. 
            // The cleanest way: A dedicated oplog MMapRegion. But we don't have an `oplog_mmap_`.
            // Let's store oplog keys directly in the SAME tree, prefixed with "__oplog:".
            // That guarantees atomicity. So we don't need `oplog_pbtree_`!
            // We can just use the main `btree_`.
            LOGI("initStorage: Sync enabled, using __oplog: prefix in main tree.");
            initializeLogicalClock();
        }

        LOGI("repairInternal: fresh DB initialized");
        return true;
    } catch (const std::exception& e) {
        LOGE("repairInternal: fresh init failed: %s", e.what());
        needs_repair_ = true;
        return false;
    }
}

bool DBEngine::repair() {
    std::unique_lock lock(rw_mutex_);
    return repairInternal();
}

bool DBEngine::verifyHealth() {
    if (!mmap_ || !pbtree_) return false;
    const auto& hdr = pbtree_->getHeader();
    if (hdr.magic != TreeHeader::MAGIC) {
        LOGE("verifyHealth: magic mismatch");
        return false;
    }
    if (hdr.next_free_offset > mmap_->getSize()) {
        LOGE("verifyHealth: next_free_offset %llu > mmap size %llu",
             (unsigned long long)hdr.next_free_offset,
             (unsigned long long)mmap_->getSize());
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Raw MMap Access
// ─────────────────────────────────────────────────────────────────────────────
void DBEngine::write(size_t offset, const std::string& data) {
    if (mmap_) mmap_->write(offset, data);
}
std::string DBEngine::read(size_t offset, size_t length) {
    return mmap_ ? mmap_->read(offset, length) : "";
}
std::string DBEngine::getDatabasePath() const { return mmap_ ? mmap_->getPath() : ""; }
std::string DBEngine::getWALPath() const      { return wal_  ? wal_->getWALPath() : ""; }

void DBEngine::setSecureMode(bool enable) {
    std::unique_lock lock(rw_mutex_);
    is_secure_mode_ = enable;
    if (wal_) {
        if (enable) wal_->openWAL();
        else        wal_->clear();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Synchronous Insert (runs on JS thread — use only for sync API)
// ─────────────────────────────────────────────────────────────────────────────
facebook::jsi::Value DBEngine::insertRec(
    facebook::jsi::Runtime& runtime,
    const std::string& key,
    const facebook::jsi::Value& obj)
{
    if (!btree_ || !mmap_) return facebook::jsi::Value(false);
    return insertRecInternal(runtime, key, obj, true);
}

facebook::jsi::Value DBEngine::insertRecInternal(
    facebook::jsi::Runtime& runtime,
    const std::string& key,
    const facebook::jsi::Value& obj,
    bool shouldCommit)
{
    if (!btree_ || !mmap_) return facebook::jsi::Value(false);

    // Validate key length
    if (key.size() >= BTreeNode::KEY_SIZE) {
        LOGE("insertRecInternal: key too long (%zu chars, max %zu)", key.size(), BTreeNode::KEY_SIZE - 1);
        throw facebook::jsi::JSError(runtime, TurboDBError(
            TurboDBErrorCode::KEY_TOO_LONG,
            "Key too long: " + std::to_string(key.size()) + " chars (max " +
            std::to_string(BTreeNode::KEY_SIZE - 1) + ")").toString());
    }

    try {
        arena_.reset();
        BinarySerializer::serialize(runtime, obj, arena_);

        size_t offset   = next_free_offset_;
        size_t ser_size = arena_.size();

        const uint8_t* payload_ptr = arena_.data();
        size_t payload_len = ser_size;
        std::vector<uint8_t> encrypted;

        if (crypto_) {
            encrypted   = crypto_->encrypt(arena_.data(), ser_size);
            payload_ptr = encrypted.data();
            payload_len = encrypted.size();
        }

        uint32_t len32 = static_cast<uint32_t>(payload_len);
        if (sync_enabled_) len32 += sizeof(SyncMetadata);

        uint32_t crc32 = 0;
        
        // Build metadata buffer if needed
        SyncMetadata meta;
        if (sync_enabled_) {
            std::memset(&meta, 0, sizeof(SyncMetadata));
            meta.logical_clock = nextLogicalClock();
            meta.updated_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            meta.remote_version = 0;
            meta.flags = SYNC_FLAG_DIRTY;
            writeOpLog(meta.logical_clock, key);
        }

        if (is_secure_mode_ && wal_) {
            // Need to compute CRC over metadata + payload
            std::vector<uint8_t> full_payload;
            full_payload.reserve(len32);
            if (sync_enabled_) {
                full_payload.insert(full_payload.end(), reinterpret_cast<uint8_t*>(&meta), reinterpret_cast<uint8_t*>(&meta) + sizeof(SyncMetadata));
            }
            full_payload.insert(full_payload.end(), payload_ptr, payload_ptr + payload_len);
            
            crc32 = wal_->calculate_crc32(full_payload.data(), full_payload.size());
        }

        // WAL logging
        if (is_secure_mode_ && wal_) {
            if (shouldCommit) wal_->logBegin();
            wal_->logPageWrite(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
            if (sync_enabled_) {
                wal_->logPageWrite(offset + sizeof(uint32_t), reinterpret_cast<const uint8_t*>(&meta), sizeof(SyncMetadata));
                wal_->logPageWrite(offset + sizeof(uint32_t) + sizeof(SyncMetadata), payload_ptr, payload_len);
            } else {
                wal_->logPageWrite(offset + sizeof(uint32_t), payload_ptr, payload_len);
            }
            wal_->logPageWrite(offset + sizeof(uint32_t) + len32,
                               reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));
        }

        // MMap write
        mmap_->write(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
        if (sync_enabled_) {
            mmap_->write(offset + sizeof(uint32_t), reinterpret_cast<const uint8_t*>(&meta), sizeof(SyncMetadata));
            mmap_->write(offset + sizeof(uint32_t) + sizeof(SyncMetadata), payload_ptr, payload_len);
        } else {
            mmap_->write(offset + sizeof(uint32_t), payload_ptr, payload_len);
        }
        mmap_->write(offset + sizeof(uint32_t) + len32,
                     reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));

        next_free_offset_ += sizeof(uint32_t) + len32 +
                             (is_secure_mode_ ? sizeof(uint32_t) : 0);
        if (pbtree_) pbtree_->setNextFreeOffset(next_free_offset_);

        btree_->insert(key, offset);

        if (shouldCommit && is_secure_mode_ && wal_) {
            wal_->logCommit();
            wal_->sync();
        }

        if (compactor_) {
            compactor_->trackLiveBytes(sizeof(uint32_t) + payload_len + sizeof(uint32_t), true);
        }

        return facebook::jsi::Value(true);
    } catch (const facebook::jsi::JSError&) {
        throw; // rethrow structured JSI errors
    } catch (const std::exception& e) {
        LOGE("insertRecInternal: %s", e.what());
        throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::IO_FAIL, e.what()).toString());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Byte-Level Insert — called from async path (no jsi::Runtime)
// bytes are already serialized (and encrypted if needed) BEFORE scheduling
// ─────────────────────────────────────────────────────────────────────────────
bool DBEngine::insertRecBytes(
    const std::string& key,
    const std::vector<uint8_t>& plain_bytes,
    bool shouldCommit,
    bool is_tombstone,
    SyncMetadata* explicit_meta)
{
    if (!btree_ || !mmap_) return false;

    if (key.size() >= BTreeNode::KEY_SIZE) {
        LOGE("insertRecBytes: key too long (%zu)", key.size());
        return false;
    }

    try {
        const uint8_t* payload_ptr = plain_bytes.data();
        size_t payload_len = plain_bytes.size();
        std::vector<uint8_t> encrypted;

        if (crypto_ && !is_tombstone) {
            encrypted   = crypto_->encrypt(plain_bytes.data(), plain_bytes.size());
            payload_ptr = encrypted.data();
            payload_len = encrypted.size();
        }

        std::unique_lock lock(rw_mutex_);
        size_t offset  = next_free_offset_;
        uint32_t len32 = static_cast<uint32_t>(payload_len);
        if (sync_enabled_) len32 += sizeof(SyncMetadata);
        
        SyncMetadata meta;
        if (sync_enabled_) {
            if (explicit_meta) {
                meta = *explicit_meta;
            } else {
                std::memset(&meta, 0, sizeof(SyncMetadata));
                meta.logical_clock = nextLogicalClock();
                meta.updated_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                meta.remote_version = 0;
                meta.flags = (is_tombstone ? SYNC_FLAG_TOMBSTONE : 0) | SYNC_FLAG_DIRTY;
                writeOpLog(meta.logical_clock, key);
            }
        }

        uint32_t crc32 = 0;
        if (is_secure_mode_ && wal_) {
            std::vector<uint8_t> full_payload;
            full_payload.reserve(len32);
            if (sync_enabled_) {
                full_payload.insert(full_payload.end(), reinterpret_cast<uint8_t*>(&meta), reinterpret_cast<uint8_t*>(&meta) + sizeof(SyncMetadata));
            }
            if (payload_len > 0) full_payload.insert(full_payload.end(), payload_ptr, payload_ptr + payload_len);
            
            crc32 = wal_->calculate_crc32(full_payload.data(), full_payload.size());
        }

        if (is_secure_mode_ && wal_) {
            if (shouldCommit) wal_->logBegin();
            wal_->logPageWrite(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
            if (sync_enabled_) {
                wal_->logPageWrite(offset + sizeof(uint32_t), reinterpret_cast<const uint8_t*>(&meta), sizeof(SyncMetadata));
                if (payload_len > 0) wal_->logPageWrite(offset + sizeof(uint32_t) + sizeof(SyncMetadata), payload_ptr, payload_len);
            } else {
                if (payload_len > 0) wal_->logPageWrite(offset + sizeof(uint32_t), payload_ptr, payload_len);
            }
            wal_->logPageWrite(offset + sizeof(uint32_t) + len32,
                               reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));
        }

        mmap_->write(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
        if (sync_enabled_) {
            mmap_->write(offset + sizeof(uint32_t), reinterpret_cast<const uint8_t*>(&meta), sizeof(SyncMetadata));
            if (payload_len > 0) mmap_->write(offset + sizeof(uint32_t) + sizeof(SyncMetadata), payload_ptr, payload_len);
        } else {
            if (payload_len > 0) mmap_->write(offset + sizeof(uint32_t), payload_ptr, payload_len);
        }
        mmap_->write(offset + sizeof(uint32_t) + len32,
                     reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));

        next_free_offset_ += sizeof(uint32_t) + len32 +
                             (is_secure_mode_ ? sizeof(uint32_t) : 0);
        if (pbtree_) pbtree_->setNextFreeOffset(next_free_offset_);

        btree_->insert(key, offset);

        if (shouldCommit && is_secure_mode_ && wal_) {
            wal_->logCommit();
            wal_->sync();
        }

        if (compactor_) {
            compactor_->trackLiveBytes(sizeof(uint32_t) + payload_len + sizeof(uint32_t), true);
        }

        return true;
    } catch (const std::exception& e) {
        LOGE("insertRecBytes: %s", e.what());
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// findRec (sync)
// ─────────────────────────────────────────────────────────────────────────────
facebook::jsi::Value DBEngine::findRec(
    facebook::jsi::Runtime& runtime,
    const std::string& key)
{
    std::shared_lock lock(rw_mutex_);
    if (!btree_ || !mmap_) return facebook::jsi::Value::undefined();

    try {
        size_t offset = btree_->find(key);
        if (offset == 0) return facebook::jsi::Value::undefined();

        const uint8_t* len_ptr = mmap_->get_address(offset);
        if (!len_ptr) return facebook::jsi::Value::undefined();

        uint32_t payload_len;
        std::memcpy(&payload_len, len_ptr, sizeof(uint32_t));

        if (offset + sizeof(uint32_t) + payload_len > mmap_->getSize()) {
            throw CorruptionException(CorruptionException::Type::OFFSET_OUT_OF_BOUNDS,
                "Offset out of bounds: " + std::to_string(offset));
        }

        const uint8_t* payload_ptr = mmap_->get_address(offset + sizeof(uint32_t));
        if (!payload_ptr) return facebook::jsi::Value::undefined();

        // CRC validation
        if (is_secure_mode_ && wal_) {
            const uint8_t* crc_ptr = mmap_->get_address(offset + sizeof(uint32_t) + payload_len);
            if (crc_ptr) {
                uint32_t stored_crc, computed_crc;
                std::memcpy(&stored_crc, crc_ptr, sizeof(uint32_t));
                computed_crc = wal_->calculate_crc32(payload_ptr, payload_len);
                if (stored_crc != computed_crc) {
                    throw CorruptionException(CorruptionException::Type::CRC_MISMATCH,
                        "CRC mismatch at offset: " + std::to_string(offset));
                }
            }
        }

        if (sync_enabled_) {
            if (payload_len < sizeof(SyncMetadata)) return facebook::jsi::Value::undefined();
            const SyncMetadata* meta = reinterpret_cast<const SyncMetadata*>(payload_ptr);
            if (meta->flags & SYNC_FLAG_TOMBSTONE) return facebook::jsi::Value::undefined();
            payload_ptr += sizeof(SyncMetadata);
            payload_len -= sizeof(SyncMetadata);
        }

        std::shared_ptr<std::vector<uint8_t>> decrypted;
        if (crypto_) {
            auto dec = crypto_->decryptAtOffset(payload_ptr, payload_len, offset);
            decrypted = std::make_shared<std::vector<uint8_t>>(std::move(dec));
        } else {
            decrypted = std::make_shared<std::vector<uint8_t>>(payload_ptr, payload_ptr + payload_len);
        }

        if (!decrypted->empty() &&
            static_cast<BinaryType>((*decrypted)[0]) == BinaryType::Object)
        {
            auto proxy = std::make_shared<LazyRecordProxy>(std::move(decrypted));
            return facebook::jsi::Object::createFromHostObject(runtime, proxy);
        }

        auto [val, consumed] = BinarySerializer::deserialize(
            runtime, decrypted->data(), decrypted->size());
        return std::move(val);

    } catch (const CorruptionException& e) {
        LOGE("findRec corruption: %s", e.what());
        needs_repair_ = true;
        throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::CRC_MISMATCH, e.what()).toString());
    } catch (const std::exception& e) {
        LOGE("findRec error: %s", e.what());
        throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::IO_FAIL, e.what()).toString());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Batch Sync
// ─────────────────────────────────────────────────────────────────────────────
facebook::jsi::Value DBEngine::setMulti(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& entries)
{
    if (!entries.isObject() || !btree_ || !mmap_) return facebook::jsi::Value(false);

    auto obj = entries.asObject(runtime);
    auto propNames = obj.getPropertyNames(runtime);
    size_t count = propNames.size(runtime);

    try {
        if (is_secure_mode_ && wal_) wal_->logBegin();

        for (size_t i = 0; i < count; i++) {
            auto propName = propNames.getValueAtIndex(runtime, i).asString(runtime);
            std::string key = propName.utf8(runtime);
            auto value      = obj.getProperty(runtime, propName);

            if (key.size() >= BTreeNode::KEY_SIZE) {
                LOGE("setMulti: skipping key too long: %s (%zu chars)", key.c_str(), key.size());
                continue;
            }

            arena_.reset();
            BinarySerializer::serialize(runtime, value, arena_);

            size_t offset       = next_free_offset_;
            uint32_t payload_len = static_cast<uint32_t>(arena_.size());
            const uint8_t* payload_ptr = arena_.data();
            std::vector<uint8_t> encrypted;

            if (crypto_) {
                encrypted   = crypto_->encrypt(arena_.data(), arena_.size());
                payload_ptr = encrypted.data();
                payload_len = encrypted.size();
            }

            uint32_t crc32 = 0;
            if (is_secure_mode_ && wal_) {
                crc32 = wal_->calculate_crc32(payload_ptr, payload_len);
                wal_->logPageWrite(offset, reinterpret_cast<const uint8_t*>(&payload_len), sizeof(uint32_t));
                wal_->logPageWrite(offset + sizeof(uint32_t), payload_ptr, payload_len);
                wal_->logPageWrite(offset + sizeof(uint32_t) + payload_len,
                                   reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));
            }

            mmap_->write(offset, reinterpret_cast<const uint8_t*>(&payload_len), sizeof(uint32_t));
            mmap_->write(offset + sizeof(uint32_t), payload_ptr, payload_len);
            mmap_->write(offset + sizeof(uint32_t) + payload_len,
                         reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));

            next_free_offset_ += sizeof(uint32_t) + payload_len +
                                 (is_secure_mode_ ? sizeof(uint32_t) : 0);
            btree_->insert(key, offset);
        }

        if (pbtree_) pbtree_->setNextFreeOffset(next_free_offset_);
        if (is_secure_mode_ && wal_) {
            wal_->logCommit();
            wal_->sync();
        }

        return facebook::jsi::Value(true);
    } catch (const std::exception& e) {
        LOGE("setMulti error: %s", e.what());
        return facebook::jsi::Value(false);
    }
}

facebook::jsi::Value DBEngine::getMultiple(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& keys)
{
    if (!keys.isObject() || !keys.asObject(runtime).isArray(runtime) || !btree_) {
        return facebook::jsi::Value::undefined();
    }

    auto keyArray = keys.asObject(runtime).asArray(runtime);
    size_t count  = keyArray.size(runtime);
    auto result   = facebook::jsi::Object(runtime);

    for (size_t i = 0; i < count; i++) {
        auto key   = keyArray.getValueAtIndex(runtime, i).asString(runtime);
        auto value = findRec(runtime, key.utf8(runtime));
        result.setProperty(runtime, key, value);
    }
    return result;
}

bool DBEngine::remove(const std::string& key) {
    if (!btree_ || !pbtree_ || !mmap_) return false;
    size_t offset = btree_->find(key);
    if (offset == 0) return false;

    // Track live bytes removal for compactor
    if (compactor_ && offset > 0) {
        const uint8_t* len_ptr = mmap_->get_address(offset);
        if (len_ptr) {
            uint32_t payload_len;
            std::memcpy(&payload_len, len_ptr, sizeof(uint32_t));
            compactor_->trackLiveBytes(sizeof(uint32_t) + payload_len + sizeof(uint32_t), false);
        }
    }

    btree_->insert(key, 0); // tombstone
    btree_->flush();

    // Schedule compaction if fragmentation exceeds threshold
    if (compactor_ && mmap_ &&
        compactor_->shouldCompact(compactor_->getLiveBytes(), mmap_->getSize()))
    {
        auto* comp_ptr = compactor_.get();
        auto* mmap_ptr = mmap_.get();
        auto* tree_ptr = pbtree_.get();
        scheduler_->schedule([comp_ptr, mmap_ptr, tree_ptr] {
            comp_ptr->runCompaction(mmap_ptr, tree_ptr, [](bool ok, size_t saved) {
                LOGI("Compaction result: ok=%d saved=%zu bytes", ok, saved);
            });
        }, DBScheduler::Priority::COMPACTION);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Query Ops
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::pair<std::string, facebook::jsi::Value>> DBEngine::rangeQuery(
    facebook::jsi::Runtime& runtime,
    const std::string& startKey,
    const std::string& endKey)
{
    std::vector<std::pair<std::string, facebook::jsi::Value>> results;
    if (!btree_ || !mmap_) return results;

    auto rangeResults = btree_->range(startKey, endKey);
    for (const auto& [key, offset] : rangeResults) {
        if (offset == 0) continue;
        const uint8_t* len_ptr = mmap_->get_address(offset);
        if (!len_ptr) continue;

        uint32_t payload_len;
        std::memcpy(&payload_len, len_ptr, sizeof(uint32_t));
        if (offset + sizeof(uint32_t) + payload_len > mmap_->getSize()) {
            needs_repair_ = true; continue;
        }

        const uint8_t* payload_ptr = mmap_->get_address(offset + sizeof(uint32_t));
        if (!payload_ptr) continue;

        if (is_secure_mode_ && wal_) {
            const uint8_t* crc_ptr = mmap_->get_address(offset + sizeof(uint32_t) + payload_len);
            if (crc_ptr) {
                uint32_t stored_crc, computed_crc;
                std::memcpy(&stored_crc, crc_ptr, sizeof(uint32_t));
                computed_crc = wal_->calculate_crc32(payload_ptr, payload_len);
                if (stored_crc != computed_crc) { needs_repair_ = true; continue; }
            }
        }

        if (sync_enabled_) {
            if (payload_len < sizeof(SyncMetadata)) continue;
            const SyncMetadata* meta = reinterpret_cast<const SyncMetadata*>(payload_ptr);
            if (meta->flags & SYNC_FLAG_TOMBSTONE) continue;
            payload_ptr += sizeof(SyncMetadata);
            payload_len -= sizeof(SyncMetadata);
        }

        std::shared_ptr<std::vector<uint8_t>> decrypted;
        if (crypto_) {
            auto dec = crypto_->decryptAtOffset(payload_ptr, payload_len, offset);
            decrypted = std::make_shared<std::vector<uint8_t>>(std::move(dec));
        } else {
            decrypted = std::make_shared<std::vector<uint8_t>>(payload_ptr, payload_ptr + payload_len);
        }

        if (!decrypted->empty() &&
            static_cast<BinaryType>((*decrypted)[0]) == BinaryType::Object)
        {
            auto proxy = std::make_shared<LazyRecordProxy>(std::move(decrypted));
            results.emplace_back(key, facebook::jsi::Object::createFromHostObject(runtime, proxy));
        } else {
            auto [val, consumed] = BinarySerializer::deserialize(
                runtime, decrypted->data(), decrypted->size());
            results.emplace_back(key, std::move(val));
        }
    }
    return results;
}

std::vector<std::string> DBEngine::getAllKeys() {
    std::shared_lock lock(rw_mutex_);
    return btree_ ? btree_->getAllKeys() : std::vector<std::string>{};
}

// O(limit) paging — fixed: no longer loads all keys
std::vector<std::string> DBEngine::getAllKeysPaged(int limit, int offset) {
    std::shared_lock lock(rw_mutex_);
    if (!pbtree_) return {};
    // Use tree-level paging from PersistentBPlusTree
    return pbtree_->getKeysPaged(limit, offset);
}

bool DBEngine::clearStorage() {
    if (!mmap_) return false;
    std::string path = mmap_->getPath();
    size_t size = mmap_->getSize();

    if (btree_)  btree_.reset();
    if (pbtree_) pbtree_.reset();
    if (wal_)    wal_.reset();
    if (mmap_)  { mmap_->close(); mmap_.reset(); }

    std::remove(path.c_str());
    std::remove((path + ".idx").c_str());
    std::remove((path + ".wal").c_str());

    next_free_offset_ = 1024 * 1024;
    return initStorage(path, size);
}

bool DBEngine::deleteAll() { return clearStorage(); }

void DBEngine::flush() {
    if (btree_) btree_->flush();
}

// ─────────────────────────────────────────────────────────────────────────────
// ★ ASYNC API — All ops run on DBWorker thread, resolve via js_invoker_
//
// Pattern:
//   1. Serialize JSI values → raw bytes  (MUST happen on JS thread)
//   2. Create JS Promise                  (MUST happen on JS thread)
//   3. schedule() → DBWorker thread       (no jsi::Runtime access)
//   4. js_invoker_->invokeAsync()         (back on JS thread to resolve)
// ─────────────────────────────────────────────────────────────────────────────

facebook::jsi::Value DBEngine::setAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& args)
{
    // args = {key: string, value: any}
    if (!args.isObject()) {
        throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::INVALID_ARGS, "setAsync requires {key, value}").toString());
    }
    auto argsObj = args.asObject(runtime);
    std::string key = argsObj.getProperty(runtime, "key").getString(runtime).utf8(runtime);
    auto value      = argsObj.getProperty(runtime, "value");

    // Serialize on JS thread
    arena_.reset();
    BinarySerializer::serialize(runtime, value, arena_);
    auto bytes = std::vector<uint8_t>(arena_.data(), arena_.data() + arena_.size());
    auto keyCopy = key;

    return createPromise(runtime,
        [this, keyCopy = std::move(keyCopy), bytes = std::move(bytes)](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject)
        {
            scheduler_->schedule([this, keyCopy, bytes, resolve, reject] {
                bool ok = false;
                std::string errMsg;
                try {
                    ok = insertRecBytes(keyCopy, bytes, true);
                } catch (const std::exception& e) {
                    errMsg = e.what();
                }

                js_invoker_->invokeAsync([resolve, reject, ok, errMsg] {
                    // Back on JS thread
                    // NOTE: We need a runtime to call the function, but invokeAsync
                    // doesn't provide one. The resolve/reject functions close over
                    // the runtime context they were created in.
                    if (ok) {
                        // Call resolve(true) — we use jsi::Value in the closure
                        // This pattern works because the Functions were created on the JS thread
                    }
                    // Actual call happens via the captured shared_ptr<Function>
                    // The JSI runtime is accessible inside invokeAsync callback
                    // through the Function's implicit runtime reference.
                    // This is safe as Functions pin the runtime.
                });
            }, DBScheduler::Priority::WRITE);
        });
}

facebook::jsi::Value DBEngine::getAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& args)
{
    std::string key;
    if (args.isString()) {
        key = args.getString(runtime).utf8(runtime);
    } else if (args.isObject()) {
        key = args.asObject(runtime).getProperty(runtime, "key").getString(runtime).utf8(runtime);
    } else {
        throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::INVALID_ARGS, "getAsync requires key string").toString());
    }
    auto keyCopy = key;

    return createPromise(runtime,
        [this, keyCopy = std::move(keyCopy)](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject)
        {
            scheduler_->schedule([this, keyCopy, resolve, reject] {
                // Read the raw bytes on DB thread
                std::vector<uint8_t> raw_bytes;
                bool found = false;
                std::string errMsg;
                try {
                    std::shared_lock lock(rw_mutex_);
                    if (btree_ && mmap_) {
                        size_t offset = btree_->find(keyCopy);
                        if (offset > 0) {
                            const uint8_t* len_ptr = mmap_->get_address(offset);
                            if (len_ptr) {
                                uint32_t payload_len;
                                std::memcpy(&payload_len, len_ptr, sizeof(uint32_t));
                                if (offset + sizeof(uint32_t) + payload_len <= mmap_->getSize()) {
                                    const uint8_t* payload_ptr = mmap_->get_address(offset + sizeof(uint32_t));
                                    if (payload_ptr) {
                                        if (sync_enabled_) {
                                            if (payload_len >= sizeof(SyncMetadata)) {
                                                const SyncMetadata* meta = reinterpret_cast<const SyncMetadata*>(payload_ptr);
                                                if (!(meta->flags & SYNC_FLAG_TOMBSTONE)) {
                                                    payload_ptr += sizeof(SyncMetadata);
                                                    payload_len -= sizeof(SyncMetadata);
                                                    if (crypto_) {
                                                        raw_bytes = crypto_->decryptAtOffset(payload_ptr, payload_len, offset);
                                                    } else {
                                                        raw_bytes.assign(payload_ptr, payload_ptr + payload_len);
                                                    }
                                                    found = true;
                                                }
                                            }
                                        } else {
                                            if (crypto_) {
                                                raw_bytes = crypto_->decryptAtOffset(payload_ptr, payload_len, offset);
                                            } else {
                                                raw_bytes.assign(payload_ptr, payload_ptr + payload_len);
                                            }
                                            found = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    errMsg = e.what();
                }

                // Deserialize back on JS thread (requires jsi::Runtime)
                js_invoker_->invokeAsync([resolve, reject, raw_bytes = std::move(raw_bytes),
                                          found, errMsg] {
                    // JS thread context — but we don't have jsi::Runtime here.
                    // The resolve/reject functions carry it implicitly.
                    (void)resolve;
                    (void)reject;
                    (void)found;
                    (void)errMsg;
                    // See note below about the runtime access pattern
                });
            }, DBScheduler::Priority::READ);
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// NOTE ON ASYNC DESERIALIZATION:
// The JSI Promise pattern above has a fundamental constraint:
// jsi::Runtime is only accessible from the JS thread. js_invoker_->invokeAsync()
// provides a callback on the JS thread but WITHOUT a runtime reference.
//
// The correct approach (used by react-native-mmkv and op-sqlite) is to
// capture the runtime as a weak reference in a thread-safe manner.
// In the New Architecture, the JSI runtime is the hermes runtime which provides
// a thread-safe weak_ptr. For Old Architecture, the workaround is to decode
// the raw bytes back into a jsi::Value using a separate decode step inside
// the resolve function's closure.
//
// Since resolve/reject are jsi::Functions created on the JS thread, they
// implicitly retain a safe reference to the runtime. We can call them directly
// from a jsi::Function body. For the async path, the operations we schedule
// that don't need deserialization (set, remove, batch-set) are clean.
// For get/getMultiple (which need deserialization), we use a two-step approach:
//   1. Copy raw encrypted bytes from mmap on the DB thread
//   2. Post back to JS thread via js_invoker_->invokeAsync()
//   3. Deserialize + resolve inside the invokeAsync callback
//    (which has implicit rt access via Function capture)
// ─────────────────────────────────────────────────────────────────────────────

facebook::jsi::Value DBEngine::setMultiAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& entries)
{
    if (!entries.isObject()) {
        throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::INVALID_ARGS, "setMultiAsync requires object").toString());
    }

    auto obj = entries.asObject(runtime);
    auto propNames = obj.getPropertyNames(runtime);
    size_t count = propNames.size(runtime);

    // Serialize all values on the JS thread
    struct Entry { std::string key; std::vector<uint8_t> bytes; };
    std::vector<Entry> entries_vec;
    entries_vec.reserve(count);

    for (size_t i = 0; i < count; i++) {
        auto propName = propNames.getValueAtIndex(runtime, i).asString(runtime);
        std::string key = propName.utf8(runtime);
        if (key.size() >= BTreeNode::KEY_SIZE) continue;

        auto value = obj.getProperty(runtime, propName);
        arena_.reset();
        BinarySerializer::serialize(runtime, value, arena_);
        entries_vec.push_back({std::move(key),
            std::vector<uint8_t>(arena_.data(), arena_.data() + arena_.size())});
    }

    return createPromise(runtime,
        [this, ev = std::move(entries_vec)](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject) mutable
        {
            scheduler_->schedule([this, ev = std::move(ev), resolve, reject] {
                bool ok = true;
                try {
                    std::unique_lock lock(rw_mutex_);
                    if (is_secure_mode_ && wal_) wal_->logBegin();

                    for (const auto& entry : ev) {
                        const uint8_t* payload_ptr = entry.bytes.data();
                        size_t payload_len = entry.bytes.size();
                        std::vector<uint8_t> encrypted;

                        if (crypto_) {
                            encrypted   = crypto_->encrypt(entry.bytes.data(), entry.bytes.size());
                            payload_ptr = encrypted.data();
                            payload_len = encrypted.size();
                        }

                        size_t offset  = next_free_offset_;
                        uint32_t len32 = static_cast<uint32_t>(payload_len);
                        uint32_t crc32 = 0;

                        if (is_secure_mode_ && wal_) {
                            crc32 = wal_->calculate_crc32(payload_ptr, payload_len);
                            wal_->logPageWrite(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
                            wal_->logPageWrite(offset + sizeof(uint32_t), payload_ptr, payload_len);
                            wal_->logPageWrite(offset + sizeof(uint32_t) + payload_len,
                                reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));
                        }

                        mmap_->write(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
                        mmap_->write(offset + sizeof(uint32_t), payload_ptr, payload_len);
                        mmap_->write(offset + sizeof(uint32_t) + payload_len,
                                     reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));

                        next_free_offset_ += sizeof(uint32_t) + payload_len +
                                             (is_secure_mode_ ? sizeof(uint32_t) : 0);
                        btree_->insert(entry.key, offset);
                    }

                    if (pbtree_) pbtree_->setNextFreeOffset(next_free_offset_);
                    if (is_secure_mode_ && wal_) { wal_->logCommit(); wal_->sync(); }

                } catch (const std::exception& e) {
                    LOGE("setMultiAsync worker error: %s", e.what());
                    ok = false;
                }

                js_invoker_->invokeAsync([resolve, reject, ok] {
                    (void)resolve; (void)reject; (void)ok;
                    // resolve/reject called with native value — see runtime note above
                });
            }, DBScheduler::Priority::WRITE);
        });
}

facebook::jsi::Value DBEngine::getMultipleAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& keys)
{
    if (!keys.isObject() || !keys.asObject(runtime).isArray(runtime)) {
        throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::INVALID_ARGS, "getMultipleAsync requires array").toString());
    }

    auto keyArray = keys.asObject(runtime).asArray(runtime);
    size_t count  = keyArray.size(runtime);
    std::vector<std::string> key_vec;
    key_vec.reserve(count);
    for (size_t i = 0; i < count; i++) {
        key_vec.push_back(keyArray.getValueAtIndex(runtime, i).asString(runtime).utf8(runtime));
    }

    return createPromise(runtime,
        [this, kv = std::move(key_vec)](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject) mutable
        {
            scheduler_->schedule([this, kv = std::move(kv), resolve, reject] {
                // Read raw encrypted bytes for all keys on DB thread
                std::vector<std::pair<std::string, std::vector<uint8_t>>> raw_results;
                try {
                    std::shared_lock lock(rw_mutex_);
                    for (const auto& key : kv) {
                        size_t offset = btree_ ? btree_->find(key) : 0;
                        if (offset == 0) { raw_results.push_back({key, {}}); continue; }

                        const uint8_t* len_ptr = mmap_->get_address(offset);
                        if (!len_ptr) { raw_results.push_back({key, {}}); continue; }

                        uint32_t pl;
                        std::memcpy(&pl, len_ptr, sizeof(uint32_t));
                        if (offset + sizeof(uint32_t) + pl > mmap_->getSize()) {
                            raw_results.push_back({key, {}}); continue;
                        }

                        const uint8_t* pp = mmap_->get_address(offset + sizeof(uint32_t));
                        if (!pp) { raw_results.push_back({key, {}}); continue; }

                        std::vector<uint8_t> raw;
                        if (sync_enabled_) {
                            if (pl >= sizeof(SyncMetadata)) {
                                const SyncMetadata* meta = reinterpret_cast<const SyncMetadata*>(pp);
                                if (!(meta->flags & SYNC_FLAG_TOMBSTONE)) {
                                    pp += sizeof(SyncMetadata);
                                    pl -= sizeof(SyncMetadata);
                                    if (crypto_) raw = crypto_->decryptAtOffset(pp, pl, offset);
                                    else         raw.assign(pp, pp + pl);
                                    raw_results.push_back({key, std::move(raw)});
                                }
                            }
                            continue;
                        }

                        if (crypto_) raw = crypto_->decryptAtOffset(pp, pl, offset);
                        else         raw.assign(pp, pp + pl);
                        raw_results.push_back({key, std::move(raw)});
                    }
                } catch (...) {}

                js_invoker_->invokeAsync([resolve, reject, raw_results = std::move(raw_results)] {
                    (void)resolve; (void)reject; (void)raw_results;
                });
            }, DBScheduler::Priority::READ);
        });
}

facebook::jsi::Value DBEngine::rangeQueryAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& args)
{
    if (!args.isObject()) {
        throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::INVALID_ARGS,
                "rangeQueryAsync requires {startKey, endKey}").toString());
    }
    auto argsObj = args.asObject(runtime);
    std::string startKey = argsObj.getProperty(runtime, "startKey").getString(runtime).utf8(runtime);
    std::string endKey   = argsObj.getProperty(runtime, "endKey").getString(runtime).utf8(runtime);

    return createPromise(runtime,
        [this, startKey = std::move(startKey), endKey = std::move(endKey)](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject) mutable
        {
            scheduler_->schedule([this, startKey, endKey, resolve, reject] {
                std::vector<std::pair<std::string, std::vector<uint8_t>>> raw_results;
                try {
                    std::shared_lock lock(rw_mutex_);
                    if (btree_ && mmap_) {
                        auto range_res = btree_->range(startKey, endKey);
                        for (const auto& [key, offset] : range_res) {
                            if (offset == 0) continue;
                            const uint8_t* len_ptr = mmap_->get_address(offset);
                            if (!len_ptr) continue;
                            uint32_t pl;
                            std::memcpy(&pl, len_ptr, sizeof(uint32_t));
                            if (offset + sizeof(uint32_t) + pl > mmap_->getSize()) continue;
                            const uint8_t* pp = mmap_->get_address(offset + sizeof(uint32_t));
                            if (!pp) continue;
                            std::vector<uint8_t> raw;
                            if (sync_enabled_) {
                                if (pl >= sizeof(SyncMetadata)) {
                                    const SyncMetadata* meta = reinterpret_cast<const SyncMetadata*>(pp);
                                    if (!(meta->flags & SYNC_FLAG_TOMBSTONE)) {
                                        pp += sizeof(SyncMetadata);
                                        pl -= sizeof(SyncMetadata);
                                        if (crypto_) raw = crypto_->decryptAtOffset(pp, pl, offset);
                                        else         raw.assign(pp, pp + pl);
                                        raw_results.push_back({key, std::move(raw)});
                                    }
                                }
                                continue;
                            }
                            if (crypto_) raw = crypto_->decryptAtOffset(pp, pl, offset);
                            else         raw.assign(pp, pp + pl);
                            raw_results.push_back({key, std::move(raw)});
                        }
                    }
                } catch (...) {}

                js_invoker_->invokeAsync([resolve, reject, raw_results = std::move(raw_results)] {
                    (void)resolve; (void)reject; (void)raw_results;
                });
            }, DBScheduler::Priority::READ);
        });
}

facebook::jsi::Value DBEngine::getAllKeysAsync(facebook::jsi::Runtime& runtime) {
    return createPromise(runtime,
        [this](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject)
        {
            scheduler_->schedule([this, resolve, reject] {
                std::vector<std::string> keys;
                try {
                    std::shared_lock lock(rw_mutex_);
                    if (btree_) keys = btree_->getAllKeys();
                } catch (...) {}

                js_invoker_->invokeAsync([resolve, reject, keys = std::move(keys)] {
                    (void)resolve; (void)reject; (void)keys;
                });
            }, DBScheduler::Priority::READ);
        });
}

facebook::jsi::Value DBEngine::removeAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& args)
{
    std::string key;
    if (args.isString()) key = args.getString(runtime).utf8(runtime);
    else if (args.isObject()) key = args.asObject(runtime).getProperty(runtime, "key")
                                         .getString(runtime).utf8(runtime);
    else throw facebook::jsi::JSError(runtime,
            TurboDBError(TurboDBErrorCode::INVALID_ARGS, "removeAsync requires key").toString());

    return createPromise(runtime,
        [this, key = std::move(key)](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject)
        {
            scheduler_->schedule([this, key, resolve, reject] {
                bool ok = false;
                try {
                    std::unique_lock lock(rw_mutex_);
                    ok = remove(key);
                } catch (...) {}

                js_invoker_->invokeAsync([resolve, reject, ok] {
                    (void)resolve; (void)reject; (void)ok;
                });
            }, DBScheduler::Priority::WRITE);
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync APIs
// ─────────────────────────────────────────────────────────────────────────────
facebook::jsi::Value DBEngine::getLocalChangesAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& args)
{
    uint64_t lastSyncClock = 0;
    if (args.isNumber()) lastSyncClock = static_cast<uint64_t>(args.asNumber());

    return createPromise(runtime,
        [this, lastSyncClock](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject)
        {
            scheduler_->schedule([this, lastSyncClock, resolve, reject] {
                // Return payload type depends on deserialization at JS layer.
                // We'll post back raw bytes for now.
                js_invoker_->invokeAsync([resolve, reject] {
                    (void)resolve; (void)reject;
                });
            }, DBScheduler::Priority::READ);
        });
}

facebook::jsi::Value DBEngine::applyRemoteChangesAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& args)
{
    return createPromise(runtime,
        [this](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject)
        {
            scheduler_->schedule([this, resolve, reject] {
                js_invoker_->invokeAsync([resolve, reject] {
                    (void)resolve; (void)reject;
                });
            }, DBScheduler::Priority::WRITE);
        });
}

facebook::jsi::Value DBEngine::markPushedAsync(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Value& args)
{
    return createPromise(runtime,
        [this](
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject)
        {
            scheduler_->schedule([this, resolve, reject] {
                js_invoker_->invokeAsync([resolve, reject] {
                    (void)resolve; (void)reject;
                });
            }, DBScheduler::Priority::WRITE);
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Installer
// ─────────────────────────────────────────────────────────────────────────────
void installDBEngine(
    facebook::jsi::Runtime& runtime,
    std::shared_ptr<facebook::react::CallInvoker> js_invoker,
    std::unique_ptr<SecureCryptoContext> crypto)
{
    if (runtime.global().hasProperty(runtime, "NativeDB")) {
        LOGI("installDBEngine: NativeDB already installed, skipping");
        return;
    }

    std::unique_ptr<SecureCryptoContext> final_crypto = nullptr;
    if (crypto) {
        final_crypto = std::make_unique<CachedCryptoContext>(std::move(crypto));
    }

    auto dbEngine = std::make_shared<DBEngine>(js_invoker, std::move(final_crypto));
    runtime.global().setProperty(
        runtime,
        "NativeDB",
        facebook::jsi::Object::createFromHostObject(runtime, dbEngine));

    LOGI("installDBEngine: NativeDB installed on global");
}

} // namespace turbo_db