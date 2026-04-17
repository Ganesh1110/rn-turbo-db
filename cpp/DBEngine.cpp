#include "DBEngine.h"
#include "WALManager.h"
#include "CachedCryptoContext.h"
#include "LazyRecordProxy.h"
#include <iostream>
#include <vector>
#include <shared_mutex>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "SecureDB_Native"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...)
#define LOGE(...)
#endif

namespace facebook::jsi {

class Promise {
public:
    static facebook::jsi::Value create(facebook::jsi::Runtime& runtime) {
        auto promiseFunc = runtime.global().getPropertyAsFunction(runtime, "Promise");
        
        auto result = facebook::jsi::Object(runtime);
        
        auto callback = facebook::jsi::Function::createFromHostFunction(
            runtime,
            facebook::jsi::PropNameID::forAscii(runtime, "executor"),
            2,
            [result_shared = std::make_shared<facebook::jsi::Object>(std::move(result))](facebook::jsi::Runtime& rt, const facebook::jsi::Value& thisVal, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                result_shared->setProperty(rt, "resolve", args[0].asObject(rt).asFunction(rt));
                result_shared->setProperty(rt, "reject", args[1].asObject(rt).asFunction(rt));
                return facebook::jsi::Value::undefined();
            }
        );
        
        auto promise = promiseFunc.callAsConstructor(runtime, callback).asObject(runtime);
        // ... I need to be careful with the order here.
        // Let's use a different approach.
        return facebook::jsi::Value::undefined(); // placeholder
    }

    static facebook::jsi::Value promiseReject(facebook::jsi::Runtime& runtime, const std::exception& e) {
        auto promiseFunc = runtime.global().getPropertyAsFunction(runtime, "Promise");
        auto rejectFunc = promiseFunc.getPropertyAsFunction(runtime, "reject");
        auto error = runtime.global().getPropertyAsFunction(runtime, "Error")
            .callAsConstructor(runtime, facebook::jsi::String::createFromUtf8(runtime, e.what()));
        return rejectFunc.call(runtime, error);
    }
};

} // namespace facebook::jsi

namespace secure_db {

DBEngine::DBEngine(std::shared_ptr<facebook::react::CallInvoker> js_invoker, std::unique_ptr<SecureCryptoContext> crypto) 
    : start_time_(std::chrono::high_resolution_clock::now()),
      crypto_(std::move(crypto)),
      next_free_offset_(1024 * 1024),
      arena_(1024 * 1024),
      js_invoker_(js_invoker) { // 1MB reusable buffer
}

facebook::jsi::Value DBEngine::get(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::PropNameID& name
) {
    std::string propName = name.utf8(runtime);
    
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "SecureDB", "getProperty called for: '%s'", propName.c_str());
#endif
    
    if (propName == "add") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime,
            name,
            2,
            [this](facebook::jsi::Runtime& runtime,
                   const facebook::jsi::Value& thisValue,
                   const facebook::jsi::Value* args,
                   size_t count) -> facebook::jsi::Value {
                if (count < 2) {
                    throw facebook::jsi::JSError(runtime, "add requires 2 arguments");
                }
                double a = args[0].asNumber();
                double b = args[1].asNumber();
                return facebook::jsi::Value(this->add(a, b));
            }
        );
    }
    
    if (propName == "echo") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime,
            name,
            1,
            [this](facebook::jsi::Runtime& runtime,
                   const facebook::jsi::Value& thisValue,
                   const facebook::jsi::Value* args,
                   size_t count) -> facebook::jsi::Value {
                if (count < 1) {
                    throw facebook::jsi::JSError(runtime, "echo requires 1 argument");
                }
                std::string msg = args[0].getString(runtime).utf8(runtime);
                return facebook::jsi::String::createFromUtf8(runtime, this->echo(msg));
            }
        );
    }
    
    if (propName == "benchmark") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime,
            name,
            0,
            [this](facebook::jsi::Runtime& runtime,
                   const facebook::jsi::Value& thisValue,
                   const facebook::jsi::Value* args,
                   size_t count) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                return facebook::jsi::Value(this->benchmark());
            }
        );
    }
    
    if (propName == "initStorage") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                std::string path = args[0].getString(runtime).utf8(runtime);
                size_t size = args[1].asNumber();
                return facebook::jsi::Value(this->initStorage(path, size));
            }
        );
    }
    
    if (propName == "write") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                size_t offset = args[0].asNumber();
                std::string data = args[1].getString(runtime).utf8(runtime);
                this->write(offset, data);
                return facebook::jsi::Value::undefined();
            }
        );
    }
    
    if (propName == "read") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                size_t offset = args[0].asNumber();
                size_t length = args[1].asNumber();
                std::string res = this->read(offset, length);
                return facebook::jsi::String::createFromUtf8(runtime, res);
            }
        );
    }
    
    if (propName == "insertRec") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                std::string key = args[0].getString(runtime).utf8(runtime);
                return this->insertRec(runtime, key, args[1]);
            }
        );
    }
    
    if (propName == "findRec") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                std::string key = args[0].getString(runtime).utf8(runtime);
                return this->findRec(runtime, key);
            }
        );
    }
    
    if (propName == "clearStorage") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return facebook::jsi::Value(this->clearStorage());
            }
        );
    }
    
    if (propName == "setMulti") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return this->setMulti(runtime, args[0]);
            }
        );
    }
    
    if (propName == "getMultiple") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                return this->getMultiple(runtime, args[0]);
            }
        );
    }
    
    if (propName == "remove") {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO, "SecureDB", "getProperty: 'remove' handler");
#endif
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                std::string key = args[0].getString(runtime).utf8(runtime);
                bool result = this->remove(key);
                return facebook::jsi::Value(result);
            }
        );
    }
    
    if (propName == "del") {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO, "SecureDB", "getProperty: 'del' handler");
#endif
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
#ifdef __ANDROID__
                __android_log_print(ANDROID_LOG_INFO, "SecureDB", "del called, count=%zu", count);
#endif
                std::unique_lock lock(rw_mutex_);
                std::string key = args[0].getString(runtime).utf8(runtime);
                bool result = this->remove(key);
#ifdef __ANDROID__
                __android_log_print(ANDROID_LOG_INFO, "SecureDB", "del result=%d", result);
#endif
                return facebook::jsi::Value(result);
            }
        );
    }
    
    if (propName == "rangeQuery") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                std::string startKey = args[0].getString(runtime).utf8(runtime);
                std::string endKey = args[1].getString(runtime).utf8(runtime);
                auto results = this->rangeQuery(runtime, startKey, endKey);
                auto arr = facebook::jsi::Array(runtime, results.size());
                for (size_t i = 0; i < results.size(); i++) {
                    auto obj = facebook::jsi::Object(runtime);
                    obj.setProperty(runtime, "key", facebook::jsi::String::createFromUtf8(runtime, results[i].first));
                    obj.setProperty(runtime, "value", results[i].second);
                    arr.setValueAtIndex(runtime, i, obj);
                }
                return arr;
            }
        );
    }
    
    if (propName == "getAllKeys") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::shared_lock lock(rw_mutex_);
                auto keys = this->getAllKeys();
                auto arr = facebook::jsi::Array(runtime, keys.size());
                for (size_t i = 0; i < keys.size(); i++) {
                    arr.setValueAtIndex(runtime, i, facebook::jsi::String::createFromUtf8(runtime, keys[i]));
                }
                return arr;
            }
        );
    }
    
    if (propName == "deleteAll") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                return facebook::jsi::Value(this->deleteAll());
            }
        );
    }
    
    if (propName == "flush") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                this->flush();
                return facebook::jsi::Value::undefined();
            }
        );
    }

    if (propName == "setMultiAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                return this->setMultiAsync(runtime, args[0]);
            }
        );
    }

    if (propName == "getMultipleAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                return this->getMultipleAsync(runtime, args[0]);
            }
        );
    }

    if (propName == "rangeQueryAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                return this->rangeQueryAsync(runtime, args[0]);
            }
        );
    }

if (propName == "getAllKeysAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                return this->getAllKeysAsync(runtime);
            }
        );
    }

    if (propName == "getDatabasePath") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                return facebook::jsi::String::createFromUtf8(runtime, this->getDatabasePath());
            }
        );
    }

    if (propName == "getWALPath") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                return facebook::jsi::String::createFromUtf8(runtime, this->getWALPath());
            }
        );
    }

    if (propName == "verifyHealth") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                return facebook::jsi::Value(this->verifyHealth());
            }
        );
    }

    if (propName == "repair") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                return facebook::jsi::Value(this->repair());
            }
        );
    }

    if (propName == "setSecureMode") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                bool enable = args[0].asBool();
                this->setSecureMode(enable);
                return facebook::jsi::Value::undefined();
            }
        );
    }

    return facebook::jsi::Value::undefined();
}

std::vector<facebook::jsi::PropNameID> DBEngine::getPropertyNames(facebook::jsi::Runtime& runtime) {
    std::vector<facebook::jsi::PropNameID> names;
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "add"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "echo"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "benchmark"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "initStorage"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "write"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "read"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "insertRec"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "findRec"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "clearStorage"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "setMulti"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "getMultiple"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "remove"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "del"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "rangeQuery"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "getAllKeys"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "deleteAll"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "flush"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "setMultiAsync"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "getMultipleAsync"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "rangeQueryAsync"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "getAllKeysAsync"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "getDatabasePath"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "getWALPath"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "verifyHealth"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "repair"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "setSecureMode"));
    return names;
}

double DBEngine::add(double a, double b) {
    return a + b;
}

std::string DBEngine::echo(const std::string& message) {
    return "Echo: " + message;
}

double DBEngine::benchmark() {
    if (!btree_) return 0;
    auto start = std::chrono::high_resolution_clock::now();
    
    // Perform 1000 small operations
    for (int i = 0; i < 1000; i++) {
        std::string key = "bench_" + std::to_string(i);
        btree_->insert(key, 1000 + i);
    }
    btree_->flush();
    
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void DBEngine::setSecureMode(bool enable) {
    std::unique_lock lock(rw_mutex_);
    is_secure_mode_ = enable;
    if (wal_) {
        if (enable) {
            wal_->openWAL();
        } else {
            wal_->clear();
        }
    }
}

bool DBEngine::verifyHealth() {
    if (!mmap_ || !pbtree_) return false;

    // Check B+ Tree header magic number
    auto header = pbtree_->getHeader();
    if (header.magic != TreeHeader::MAGIC) {
        LOGE("B+ Tree header magic mismatch: 0x%llx != 0x%llx", (unsigned long long)header.magic, (unsigned long long)TreeHeader::MAGIC);
        return false;
    }

    // Check bounds
    if (header.next_free_offset > mmap_->getSize()) {
        LOGE("next_free_offset %llu exceeds mmap size %llu", 
             (unsigned long long)header.next_free_offset, 
             (unsigned long long)mmap_->getSize());
        return false;
    }

    return true;
}

bool DBEngine::repair() {
    if (!mmap_) return false;

    std::unique_lock lock(rw_mutex_);

    std::string db_path = mmap_->getPath();
    std::string corrupt_path = db_path + ".corrupt.bak";

    try {
        // Step 1: Close current mappings
        mmap_->close();
        if (btree_) btree_.reset();
        if (pbtree_) pbtree_.reset();
        if (wal_) wal_.reset();

        // Step 2: Archive corrupted file
        std::rename(db_path.c_str(), corrupt_path.c_str());
        std::remove((db_path + ".idx").c_str());
        std::remove((db_path + ".wal").c_str());

        LOGI("Database archived to %s", corrupt_path.c_str());

        // Step 3: Reinitialize fresh
        next_free_offset_ = 1024 * 1024;
        needs_repair_ = false;

        mmap_ = std::make_unique<MMapRegion>();
        mmap_->init(db_path, 10 * 1024 * 1024);

        if (is_secure_mode_) {
            wal_ = std::make_unique<WALManager>(db_path, crypto_.get());
        }

        pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), wal_.get());
        pbtree_->init();

        btree_ = std::make_unique<BufferedBTree>(pbtree_.get());

        LOGI("Database repair complete");
        return true;

    } catch (const std::exception& e) {
        LOGE("Repair failed: %s", e.what());
        needs_repair_ = true;
        return false;
    }
}

std::string DBEngine::getDatabasePath() const {
    if (mmap_) {
        return mmap_->getPath();
    }
    return "";
}

std::string DBEngine::getWALPath() const {
    if (wal_) {
        return wal_->getWALPath();
    }
    return "";
}

bool DBEngine::initStorage(const std::string& path, size_t size) {
    LOGI("initStorage: path=%s, size=%zu", path.c_str(), size);
    
    // Reset any existing state to ensure clean initialization
    if (btree_) btree_.reset();
    if (pbtree_) pbtree_.reset();
    if (wal_) wal_.reset();
    if (mmap_) {
        mmap_->close();
        mmap_.reset();
    }
    
    if (!mmap_) {
        mmap_ = std::make_unique<MMapRegion>();
    }
    try {
        mmap_->init(path, size);

        // Initialize WAL Manager
        wal_ = std::make_unique<WALManager>(path, crypto_.get());

        // Initialize Persistent B+ Tree
        pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), wal_.get());
        pbtree_->init();

        // Verify health before loading
        if (!verifyHealth()) {
            LOGE("Health check failed, triggering repair");
            needs_repair_ = true;
            return repair();
        }

        // Run WAL recovery (secure mode only)
        if (is_secure_mode_) {
            wal_->recover(mmap_.get());
        }

        // Initialize offset from persisted header
        next_free_offset_ = pbtree_->getHeader().next_free_offset;

        if (!btree_) {
            btree_ = std::make_unique<BufferedBTree>(pbtree_.get());
        }

        LOGI("initStorage success");
        return true;
    } catch(const std::exception& e) {
        LOGE("initStorage exception: %s, triggering repair", e.what());
        needs_repair_ = true;
        return repair();
    }
}

void DBEngine::write(size_t offset, const std::string& data) {
    if (mmap_) {
        mmap_->write(offset, data);
    }
}

std::string DBEngine::read(size_t offset, size_t length) {
    if (mmap_) {
        return mmap_->read(offset, length);
    }
    return "";
}

facebook::jsi::Value DBEngine::insertRec(facebook::jsi::Runtime& runtime, const std::string& key, const facebook::jsi::Value& obj) {
    std::shared_lock lock(rw_mutex_);
    if (!btree_ || !mmap_) {
        LOGE("insertRec: btree or mmap is null");
        return facebook::jsi::Value(false);
    }
    return this->insertRecInternal(runtime, key, obj, true);
}

facebook::jsi::Value DBEngine::insertRecInternal(facebook::jsi::Runtime& runtime, const std::string& key, const facebook::jsi::Value& obj, bool shouldCommit) {
    if (!btree_ || !mmap_) {
        LOGE("insertRecInternal: btree or mmap is null");
        return facebook::jsi::Value(false);
    }
    
    try {
        LOGI("insertRecInternal: key=%s", key.c_str());
        // Step 1: Use reusable ArenaAllocator avoiding std::bad_alloc/new leakages
        arena_.reset();
        BinarySerializer::serialize(runtime, obj, arena_);
        
        // Step 2: Grab sequential offset and encrypt directly into arena
        size_t offset = next_free_offset_;
        size_t serialized_size = arena_.size();
        
        size_t payload_len = serialized_size;
        const uint8_t* payload_ptr = arena_.data();
        std::vector<uint8_t> encrypted;

        if (crypto_) {
            encrypted = crypto_->encrypt(arena_.data(), serialized_size);
            payload_ptr = encrypted.data();
            payload_len = encrypted.size();
        }
        
        uint32_t len32 = static_cast<uint32_t>(payload_len);

        // CRC injection (secure mode only)
        uint32_t crc32 = 0;
        if (is_secure_mode_) {
            crc32 = wal_ ? wal_->calculate_crc32(payload_ptr, payload_len) : 0;
        }

        if (is_secure_mode_ && wal_) {
            wal_->logPageWrite(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
            wal_->logPageWrite(offset + sizeof(uint32_t), payload_ptr, payload_len);
            wal_->logPageWrite(offset + sizeof(uint32_t) + payload_len, reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));
            wal_->sync();
        }

        // Format: [LEN (4)][PAYLOAD][CRC32 (4)]
        mmap_->write(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
        mmap_->write(offset + sizeof(uint32_t), payload_ptr, payload_len);
        mmap_->write(offset + sizeof(uint32_t) + payload_len, reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));
        
        // Push the needle cursor to point cleanly behind the record (including CRC)
        next_free_offset_ += sizeof(uint32_t) + payload_len + (is_secure_mode_ ? sizeof(uint32_t) : 0);
        if (pbtree_) pbtree_->setNextFreeOffset(next_free_offset_);
        
        // Step 4: Trigger the zero-latency queued background logic
        btree_->insert(key, offset);

        if (shouldCommit && is_secure_mode_ && wal_) {
            wal_->logCommit();
            wal_->sync();
        }
        
        LOGI("insertRecInternal: success for key=%s", key.c_str());
        return facebook::jsi::Value(true);
    } catch (const std::exception& e) {
        LOGE("insertRec error: %s", e.what());
        return facebook::jsi::Value(false);
    } catch (...) {
        LOGE("insertRec unknown error");
        return facebook::jsi::Value(false);
    }
}

facebook::jsi::Value DBEngine::findRec(facebook::jsi::Runtime& runtime, const std::string& key) {
    std::shared_lock lock(rw_mutex_);
    if (!btree_ || !mmap_) {
        LOGE("findRec: btree or mmap is null");
        return facebook::jsi::Value::undefined();
    }

    try {
        LOGI("findRec: key=%s", key.c_str());
        size_t offset = btree_->find(key);
        if (offset == 0) {
            LOGI("findRec: key=%s not found", key.c_str());
            return facebook::jsi::Value::undefined();
        }

        const uint8_t* len_ptr = mmap_->get_address(offset);
        if (!len_ptr) return facebook::jsi::Value::undefined();

        uint32_t payload_len;
        std::memcpy(&payload_len, len_ptr, sizeof(uint32_t));

        // Bounds check
        if (offset + sizeof(uint32_t) + payload_len > mmap_->getSize()) {
            throw CorruptionException(CorruptionException::Type::OFFSET_OUT_OF_BOUNDS,
                "Offset out of bounds: " + std::to_string(offset));
        }

        const uint8_t* payload_ptr = mmap_->get_address(offset + sizeof(uint32_t));
        if (!payload_ptr) return facebook::jsi::Value::undefined();

        // CRC validation (secure mode only)
        if (is_secure_mode_ && wal_) {
            const uint8_t* crc_ptr = mmap_->get_address(offset + sizeof(uint32_t) + payload_len);
            if (crc_ptr) {
                uint32_t stored_crc;
                std::memcpy(&stored_crc, crc_ptr, sizeof(uint32_t));
                uint32_t computed_crc = wal_->calculate_crc32(payload_ptr, payload_len);
                if (stored_crc != computed_crc) {
                    throw CorruptionException(CorruptionException::Type::CRC_MISMATCH,
                        "CRC mismatch at offset: " + std::to_string(offset));
                }
            }
        }

        std::shared_ptr<std::vector<uint8_t>> decrypted;
        if (crypto_) {
            auto decrypted_vec = crypto_->decryptAtOffset(payload_ptr, payload_len, offset);
            decrypted = std::make_shared<std::vector<uint8_t>>(std::move(decrypted_vec));
        } else {
            decrypted = std::make_shared<std::vector<uint8_t>>(payload_ptr, payload_ptr + payload_len);
        }

        if (!decrypted->empty() && static_cast<BinaryType>((*decrypted)[0]) == BinaryType::Object) {
            auto proxy = std::make_shared<LazyRecordProxy>(std::move(decrypted));
            return facebook::jsi::Object::createFromHostObject(runtime, proxy);
        }

        auto [val, consumed] = BinarySerializer::deserialize(runtime, decrypted->data(), decrypted->size());

        LOGI("findRec: key=%s success", key.c_str());
        return std::move(val);
    } catch (const CorruptionException& e) {
        LOGE("findRec corruption: %s", e.what());
        needs_repair_ = true;
        return facebook::jsi::Value::undefined();
    } catch (const std::exception& e) {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "SecureDB", "findRec error: %s", e.what());
#endif
        std::cerr << "SecureDB findRec error: " << e.what() << "\n";
        return facebook::jsi::Value::undefined();
    }
}

bool DBEngine::clearStorage() {
    LOGI("clearStorage: starting");
    if (!mmap_) return false;
    
    // Note: Already holding rw_mutex_ from JSI call, don't re-lock
    
    std::string path = mmap_->getPath();
    size_t size = mmap_->getSize();
    
    // 1. Reset all managers and close mapping
    LOGI("clearStorage: resetting managers");
    if (btree_) btree_.reset();
    if (pbtree_) pbtree_.reset();
    if (wal_) wal_.reset();
    if (mmap_) {
        mmap_->close();
        mmap_.reset();
    }
    
    // 2. Remove files
    LOGI("clearStorage: removing files at %s", path.c_str());
    std::remove(path.c_str());
    std::remove((path + ".idx").c_str());
    std::remove((path + ".wal").c_str());
    
    next_free_offset_ = 1024 * 1024; // Reset to default start offset

    // 3. Re-initialize
    LOGI("clearStorage: re-initializing storage");
    bool result = initStorage(path, size);
    LOGI("clearStorage finished with result: %d", result);
    return result;
}

void installDBEngine(facebook::jsi::Runtime& runtime, std::shared_ptr<facebook::react::CallInvoker> js_invoker, std::unique_ptr<SecureCryptoContext> crypto) {
    if (runtime.global().hasProperty(runtime, "NativeDB")) {
        LOGI("installDBEngine: NativeDB already installed, skipping");
        return;
    }
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "SecureDB", "installDBEngine: creating HostObject");
#endif
    std::unique_ptr<SecureCryptoContext> final_crypto = nullptr;
    if (crypto) {
        final_crypto = std::make_unique<CachedCryptoContext>(std::move(crypto));
    }
    
    auto dbEngine = std::make_shared<DBEngine>(js_invoker, std::move(final_crypto));
    runtime.global().setProperty(
        runtime,
        "NativeDB",
        facebook::jsi::Object::createFromHostObject(runtime, dbEngine)
    );
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "SecureDB", "installDBEngine: NativeDB set on global");
#endif
}

facebook::jsi::Value DBEngine::setMulti(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& entries) {
    if (!entries.isObject() || !btree_ || !mmap_) {
        return facebook::jsi::Value(false);
    }
    
    facebook::jsi::Object obj = entries.asObject(runtime);
    auto propNames = obj.getPropertyNames(runtime);
    size_t count = propNames.size(runtime);
    
    // Phase 4 Ultra-Optimization:
    // We avoid 500 individual try/catch and JSI overhead of calling insertRecInternal.
    // We also batch all WAL records into a single buffered write if possible.

    try {
        for (size_t i = 0; i < count; i++) {
            auto propName = propNames.getValueAtIndex(runtime, i).asString(runtime);
            std::string key = propName.utf8(runtime);
            auto value = obj.getProperty(runtime, propName);

            // Serialization
            arena_.reset();
            BinarySerializer::serialize(runtime, value, arena_);

            size_t offset = next_free_offset_;
            size_t serialized_size = arena_.size();

            // Use encryption (but skip WAL for speed)
            uint32_t payload_len = serialized_size;
            const uint8_t* payload_ptr = arena_.data();
            std::vector<uint8_t> encrypted;

            if (crypto_) {
                encrypted = crypto_->encrypt(arena_.data(), serialized_size);
                payload_ptr = encrypted.data();
                payload_len = encrypted.size();
            }

            // CRC injection (secure mode only)
            uint32_t crc32 = 0;
            if (is_secure_mode_ && wal_) {
                crc32 = wal_->calculate_crc32(payload_ptr, payload_len);
            }

            // WAL logging (secure mode only)
            if (is_secure_mode_ && wal_) {
                wal_->logPageWrite(offset, reinterpret_cast<const uint8_t*>(&payload_len), sizeof(uint32_t));
                wal_->logPageWrite(offset + sizeof(uint32_t), payload_ptr, payload_len);
                wal_->logPageWrite(offset + sizeof(uint32_t) + payload_len, reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));
            }

            // Direct MMap Write with CRC (Memory-only, OS handles flushing)
            mmap_->write(offset, reinterpret_cast<const uint8_t*>(&payload_len), sizeof(uint32_t));
            mmap_->write(offset + sizeof(uint32_t), payload_ptr, payload_len);
            mmap_->write(offset + sizeof(uint32_t) + payload_len, reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));

            next_free_offset_ += sizeof(uint32_t) + payload_len + (is_secure_mode_ ? sizeof(uint32_t) : 0);

            // Buffered B-Tree insert (Background worker handles the rest)
            btree_->insert(key, offset);
        }

        if (pbtree_) pbtree_->setNextFreeOffset(next_free_offset_);

        // Sync WAL after batch write (secure mode only)
        if (is_secure_mode_ && wal_) {
            wal_->sync();
        }
        
        return facebook::jsi::Value(true);
    } catch (const std::exception& e) {
        LOGE("setMulti error: %s", e.what());
        return facebook::jsi::Value(false);
    }
}

facebook::jsi::Value DBEngine::getMultiple(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& keys) {
    if (!keys.isObject() || !keys.asObject(runtime).isArray(runtime) || !btree_) {
        return facebook::jsi::Value::undefined();
    }
    
    facebook::jsi::Array keyArray = keys.asObject(runtime).asArray(runtime);
    size_t count = keyArray.size(runtime);
    auto result = facebook::jsi::Object(runtime);
    
    for (size_t i = 0; i < count; i++) {
        auto key = keyArray.getValueAtIndex(runtime, i).asString(runtime);
        auto value = findRec(runtime, key.utf8(runtime));
        result.setProperty(runtime, key, value);
    }
    
    return result;
}

bool DBEngine::remove(const std::string& key) {
    if (!btree_ || !pbtree_ || !mmap_) {
        LOGE("remove: btree, pbtree or mmap is null");
        return false;
    }
    
    LOGI("remove: key=%s", key.c_str());
    size_t offset = btree_->find(key);
    if (offset == 0) {
        LOGI("remove: key=%s not found", key.c_str());
        return false;
    }
    
    btree_->insert(key, 0); // Correctly update buffered tree with 0 offset (deleted)
    btree_->flush(); // Sync deletion to disk immediately
    LOGI("remove: key=%s success", key.c_str());
    return true;
}

std::vector<std::pair<std::string, facebook::jsi::Value>> DBEngine::rangeQuery(
    facebook::jsi::Runtime& runtime,
    const std::string& startKey,
    const std::string& endKey
) {
    std::vector<std::pair<std::string, facebook::jsi::Value>> results;

    if (!btree_ || !mmap_) return results;

    auto rangeResults = btree_->range(startKey, endKey);
    for (const auto& [key, offset] : rangeResults) {
        if (offset > 0) {
            const uint8_t* len_ptr = mmap_->get_address(offset);
            if (!len_ptr) continue;

            uint32_t payload_len;
            std::memcpy(&payload_len, len_ptr, sizeof(uint32_t));

            if (offset + sizeof(uint32_t) + payload_len > mmap_->getSize()) {
                needs_repair_ = true;
                continue;
            }

            const uint8_t* payload_ptr = mmap_->get_address(offset + sizeof(uint32_t));
            if (!payload_ptr) continue;

            // CRC validation (secure mode only)
            if (is_secure_mode_ && wal_) {
                const uint8_t* crc_ptr = mmap_->get_address(offset + sizeof(uint32_t) + payload_len);
                if (crc_ptr) {
                    uint32_t stored_crc;
                    std::memcpy(&stored_crc, crc_ptr, sizeof(uint32_t));
                    uint32_t computed_crc = wal_->calculate_crc32(payload_ptr, payload_len);
                    if (stored_crc != computed_crc) {
                        needs_repair_ = true;
                        continue;
                    }
                }
            }

            std::shared_ptr<std::vector<uint8_t>> decrypted;
            if (crypto_) {
                auto decrypted_vec = crypto_->decryptAtOffset(payload_ptr, payload_len, offset);
                decrypted = std::make_shared<std::vector<uint8_t>>(std::move(decrypted_vec));
            } else {
                decrypted = std::make_shared<std::vector<uint8_t>>(payload_ptr, payload_ptr + payload_len);
            }

            if (!decrypted->empty() && static_cast<BinaryType>((*decrypted)[0]) == BinaryType::Object) {
                auto proxy = std::make_shared<LazyRecordProxy>(std::move(decrypted));
                results.emplace_back(key, facebook::jsi::Object::createFromHostObject(runtime, proxy));
            } else {
                auto [val, consumed] = BinarySerializer::deserialize(runtime, decrypted->data(), decrypted->size());
                results.emplace_back(key, std::move(val));
            }
        }
    }

    return results;
}

std::vector<std::string> DBEngine::getAllKeys() {
    if (!btree_) return {};
    return btree_->getAllKeys();
}

bool DBEngine::deleteAll() {
    return clearStorage();
}

void DBEngine::flush() {
    if (btree_) {
        btree_->flush();
    }
}

facebook::jsi::Value DBEngine::setMultiAsync(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& entries) {
    return facebook::jsi::Promise::promiseReject(runtime, std::runtime_error("setMultiAsync is temporarily disabled for thread-safety fixes"));
}

facebook::jsi::Value DBEngine::getMultipleAsync(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& keys) {
    return facebook::jsi::Promise::promiseReject(runtime, std::runtime_error("getMultipleAsync is temporarily disabled for thread-safety fixes"));
}

facebook::jsi::Value DBEngine::rangeQueryAsync(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& args) {
    return facebook::jsi::Promise::promiseReject(runtime, std::runtime_error("rangeQueryAsync is temporarily disabled for thread-safety fixes"));
}

facebook::jsi::Value DBEngine::getAllKeysAsync(facebook::jsi::Runtime& runtime) {
    return facebook::jsi::Promise::promiseReject(runtime, std::runtime_error("getAllKeysAsync is temporarily disabled for thread-safety fixes"));
}

}