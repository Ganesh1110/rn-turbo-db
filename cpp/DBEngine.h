#pragma once

#include <jsi/jsi.h>
#include <ReactCommon/CallInvoker.h>
#include <shared_mutex>
#include <memory>
#include <chrono>
#include <string>
#include <iostream>
#include <regex>
#include "MMapRegion.h"
#include "PersistentBPlusTree.h"
#include "BufferedBTree.h"
#include "ArenaAllocator.h"
#include "BinarySerializer.h"
#include "SecureCryptoContext.h"
#include "WALManager.h"
#include "DBScheduler.h"
#include "Compactor.h"
#include "TurboDBError.h"
#include "SyncMetadata.h"
#include "ValueCache.h"

namespace turbo_db {

class LazyRecordProxy;

class DBEngine : public facebook::jsi::HostObject {
public:
    explicit DBEngine(
        std::shared_ptr<facebook::react::CallInvoker> js_invoker,
        std::unique_ptr<SecureCryptoContext> crypto = nullptr
    );

    ~DBEngine();

    facebook::jsi::Value get(facebook::jsi::Runtime& runtime,
                              const facebook::jsi::PropNameID& name) override;

    std::vector<facebook::jsi::PropNameID> getPropertyNames(
        facebook::jsi::Runtime& runtime) override;

    double add(double a, double b);
    std::string echo(const std::string& message);
    double benchmark();

    // Storage init
    bool initStorage(const std::string& path, size_t size, bool enableSync = false);

    // ── Transaction API ──
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // Raw mmap access (legacy)
    void write(size_t offset, const std::string& data);
    std::string read(size_t offset, size_t length);

    // Synchronous API (fast path — still runs on JS thread)
    facebook::jsi::Value insertRec(facebook::jsi::Runtime& runtime,
                                    const std::string& key,
                                    const facebook::jsi::Value& obj);
    facebook::jsi::Value findRec(facebook::jsi::Runtime& runtime,
                                  const std::string& key);
    bool clearStorage();
    void flush();

    // Batch sync ops
    facebook::jsi::Value setMulti(facebook::jsi::Runtime& runtime,
                                   const facebook::jsi::Value& entries);
    facebook::jsi::Value getMultiple(facebook::jsi::Runtime& runtime,
                                      const facebook::jsi::Value& keys);
    bool remove(const std::string& key);

    // ── Fully async ops (non-blocking, scheduled on DBWorker thread) ──
    facebook::jsi::Value setAsync(facebook::jsi::Runtime& runtime,
                                   const facebook::jsi::Value& args);
    facebook::jsi::Value getAsync(facebook::jsi::Runtime& runtime,
                                   const facebook::jsi::Value& args);
    facebook::jsi::Value setMultiAsync(facebook::jsi::Runtime& runtime,
                                        const facebook::jsi::Value& entries);
    facebook::jsi::Value getMultipleAsync(facebook::jsi::Runtime& runtime,
                                          const facebook::jsi::Value& keys);
    facebook::jsi::Value rangeQueryAsync(facebook::jsi::Runtime& runtime,
                                         const facebook::jsi::Value& args);
    facebook::jsi::Value getAllKeysAsync(facebook::jsi::Runtime& runtime);
    facebook::jsi::Value removeAsync(facebook::jsi::Runtime& runtime,
                                      const facebook::jsi::Value& args);

    // ── R3: Data Management Features ──
    // Native TTL — sidecar key pattern: "__ttl:<user_key>" → uint64_t expiry ms
    static constexpr const char* TTL_PREFIX = "__ttl:";

    facebook::jsi::Value setWithTTLAsync(facebook::jsi::Runtime& runtime,
                                         const facebook::jsi::Value& args);
    facebook::jsi::Value cleanupExpiredAsync(facebook::jsi::Runtime& runtime);

    // Native Prefix Search
    facebook::jsi::Value prefixSearchAsync(facebook::jsi::Runtime& runtime,
                                           const facebook::jsi::Value& args);

    // Regex Search (keys only, std::regex)
    facebook::jsi::Value regexSearchAsync(facebook::jsi::Runtime& runtime,
                                          const facebook::jsi::Value& args);

    // Import / Export
    facebook::jsi::Value exportDBAsync(facebook::jsi::Runtime& runtime);
    facebook::jsi::Value importDBAsync(facebook::jsi::Runtime& runtime,
                                       const facebook::jsi::Value& args);

    // Blob Support (base64 encode/decode bridging)
    facebook::jsi::Value setBlobAsync(facebook::jsi::Runtime& runtime,
                                      const facebook::jsi::Value& args);
    facebook::jsi::Value getBlobAsync(facebook::jsi::Runtime& runtime,
                                      const facebook::jsi::Value& args);

    // ── Sync Engine API ──
    facebook::jsi::Value getLocalChangesAsync(facebook::jsi::Runtime& runtime,
                                              const facebook::jsi::Value& args);
    facebook::jsi::Value applyRemoteChangesAsync(facebook::jsi::Runtime& runtime,
                                                 const facebook::jsi::Value& args);
    facebook::jsi::Value markPushedAsync(facebook::jsi::Runtime& runtime,
                                         const facebook::jsi::Value& args);

    // Query ops (sync)
    std::vector<std::pair<std::string, facebook::jsi::Value>> rangeQuery(
        facebook::jsi::Runtime& runtime,
        const std::string& startKey,
        const std::string& endKey);

    std::vector<std::string> getAllKeys();
    std::vector<std::string> getAllKeysPaged(int limit, int offset);

    bool deleteAll();

    // Health & diagnostics
    bool verifyHealth();
    bool repair();
    facebook::jsi::Value getStats(facebook::jsi::Runtime& runtime);
    std::string getDatabasePath() const;
    std::string getWALPath() const;
    void setSecureMode(bool enable);

private:
    // Promise helpers
    facebook::jsi::Value createPromise(
        facebook::jsi::Runtime& runtime,
        std::function<void(
            facebook::jsi::Runtime& rt,
            std::shared_ptr<facebook::jsi::Function> resolve,
            std::shared_ptr<facebook::jsi::Function> reject)> executor);

    // Core insert (must be called on DBWorker or under unique_lock)
    bool insertRecInternal(facebook::jsi::Runtime& runtime,
                            const std::string& key,
                            const std::vector<uint8_t>& plain_bytes,
                            bool shouldCommit,
                            bool is_tombstone);

    // Byte-level insert called from async path (no jsi::Runtime needed)
    // If outOffset is provided, returns the offset where data was written
    bool insertRecBytes(const std::string& key,
                         const std::vector<uint8_t>& bytes,
                         bool shouldCommit = true,
                         bool is_tombstone = false,
                         SyncMetadata* explicit_meta = nullptr,
                         size_t* outOffset = nullptr);

    bool repairInternal();

    // Internal Sync Utils
    uint64_t nextLogicalClock() {
        return logical_clock_.fetch_add(1, std::memory_order_relaxed);
    }
    void initializeLogicalClock();
    void writeOpLog(uint64_t clock, const std::string& key);

    mutable std::shared_mutex rw_mutex_;
    std::chrono::high_resolution_clock::time_point start_time_;
    std::unique_ptr<SecureCryptoContext> crypto_;
    std::unique_ptr<MMapRegion> mmap_;
    std::unique_ptr<PersistentBPlusTree> pbtree_;
    std::unique_ptr<BufferedBTree> btree_;
    
    // Sync OpLog tree
    std::unique_ptr<PersistentBPlusTree> oplog_pbtree_;
    BufferedBTree* oplog_btree_ = nullptr;

    std::unique_ptr<WALManager> wal_;
    std::unique_ptr<DBScheduler> scheduler_;
    std::unique_ptr<Compactor> compactor_;

    size_t next_free_offset_;
    ArenaAllocator arena_;
    ValueCache value_cache_;
    std::shared_ptr<facebook::react::CallInvoker> js_invoker_;
    bool is_secure_mode_ = true;
    bool sync_enabled_ = false;
    bool needs_repair_ = false;
    std::atomic<uint64_t> logical_clock_{1};

    // ── Transaction state ──
    bool in_transaction_ = false;
    std::vector<std::pair<std::string, size_t>> tx_writes_;  // Track key→offset written in tx
    std::vector<std::string> tx_deletes_;                     // Track keys deleted in tx

    bool isTombstone(size_t offset);
};

void installDBEngine(
    facebook::jsi::Runtime& runtime,
    std::shared_ptr<facebook::react::CallInvoker> js_invoker,
    std::unique_ptr<SecureCryptoContext> crypto = nullptr
);

std::shared_ptr<DBEngine> getDBEngine();

} // namespace turbo_db