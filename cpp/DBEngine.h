#pragma once

#include <jsi/jsi.h>
#include <shared_mutex>
#include <memory>
#include <chrono>
#include <string>
#include <iostream>
#include "MMapRegion.h"
#include "PersistentBPlusTree.h"
#include "BufferedBTree.h"
#include "ArenaAllocator.h"
#include "BinarySerializer.h"
#include "SecureCryptoContext.h"

namespace secure_db {

class DBEngine : public facebook::jsi::HostObject {
public:
    explicit DBEngine(std::unique_ptr<SecureCryptoContext> crypto = nullptr);
    
    ~DBEngine() {
        if (mmap_) {
            mmap_->sync();
            std::cout << "DBEngine: MMap synced and destroyed" << std::endl;
        } else {
            std::cout << "DBEngine: Destroyed" << std::endl;
        }
    }
    
    facebook::jsi::Value get(facebook::jsi::Runtime& runtime, 
                              const facebook::jsi::PropNameID& name) override;
    
    std::vector<facebook::jsi::PropNameID> getPropertyNames(facebook::jsi::Runtime& runtime) override;
    
    double add(double a, double b);
    std::string echo(const std::string& message);
    double benchmark();
    
    // Phase 3 MMap Methods
    bool initStorage(const std::string& path, size_t size);
    void write(size_t offset, const std::string& data);
    std::string read(size_t offset, size_t length);
    
    // Phase 4 Methods
    facebook::jsi::Value insertRec(facebook::jsi::Runtime& runtime, const std::string& key, const facebook::jsi::Value& obj);
    facebook::jsi::Value findRec(facebook::jsi::Runtime& runtime, const std::string& key);
    bool clearStorage();
    void flush();
    
    // Batch Operations
    facebook::jsi::Value setMulti(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& entries);
    facebook::jsi::Value getMultiple(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& keys);
    bool remove(const std::string& key);
    
    // Query Operations
    std::vector<std::pair<std::string, facebook::jsi::Value>> rangeQuery(
        facebook::jsi::Runtime& runtime, 
        const std::string& startKey, 
        const std::string& endKey
    );
    
    std::vector<std::string> getAllKeys();
    bool deleteAll();
    
private:
    facebook::jsi::Value insertRecInternal(facebook::jsi::Runtime& runtime, const std::string& key, const facebook::jsi::Value& obj, bool shouldCommit);
    
    mutable std::shared_mutex rw_mutex_;
    std::chrono::high_resolution_clock::time_point start_time_;
    std::unique_ptr<SecureCryptoContext> crypto_;
    std::unique_ptr<MMapRegion> mmap_;
    std::unique_ptr<PersistentBPlusTree> pbtree_;
    std::unique_ptr<BufferedBTree> btree_;
    std::unique_ptr<WALManager> wal_;
    size_t next_free_offset_;
    ArenaAllocator arena_;
};

void installDBEngine(facebook::jsi::Runtime& runtime, std::unique_ptr<SecureCryptoContext> crypto = nullptr);

}