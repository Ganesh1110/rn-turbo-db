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

namespace secure_db {

DBEngine::DBEngine(std::unique_ptr<SecureCryptoContext> crypto) 
    : start_time_(std::chrono::high_resolution_clock::now()),
      crypto_(std::move(crypto)),
      next_free_offset_(1024 * 1024),
      arena_(1024 * 1024) { // 1MB reusable buffer
}

facebook::jsi::Value DBEngine::get(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::PropNameID& name
) {
    std::string propName = name.utf8(runtime);
    
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
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value& thisValue, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                std::unique_lock lock(rw_mutex_);
                std::string key = args[0].getString(runtime).utf8(runtime);
                return facebook::jsi::Value(this->remove(key));
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
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "rangeQuery"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "getAllKeys"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "deleteAll"));
    names.push_back(facebook::jsi::PropNameID::forAscii(runtime, "flush"));
    return names;
}

double DBEngine::add(double a, double b) {
    return a + b;
}

std::string DBEngine::echo(const std::string& message) {
    return "Echo: " + message;
}

double DBEngine::benchmark() {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - start_time_).count();
    return elapsed;
}

bool DBEngine::initStorage(const std::string& path, size_t size) {
    if (!mmap_) {
        mmap_ = std::make_unique<MMapRegion>();
    }
    try {
        mmap_->init(path, size);
        
        // Initialize WAL Manager
        if (!wal_) {
            wal_ = std::make_unique<WALManager>(path, crypto_.get());
        }
        
        // Phase 6: Run recovery before loading B+ Tree
        wal_->recover(mmap_.get());
        
        // Initialize Phase 4 Tiers natively chained over the mmap pointer
        if (!pbtree_) {
            pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), wal_.get());
            pbtree_->init(); // Reads/writes Header mapping offset 0
            
            // Phase 6: Initialize offset from persisted header
            next_free_offset_ = pbtree_->getHeader().next_free_offset;
        }
        if (!btree_) {
            btree_ = std::make_unique<BufferedBTree>(pbtree_.get());
        }
        
        return true;
    } catch(const std::exception& e) {
        std::cerr << "DBEngine initStorage error: " << e.what() << "\n";
        return false;
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
    return this->insertRecInternal(runtime, key, obj, true);
}

facebook::jsi::Value DBEngine::insertRecInternal(facebook::jsi::Runtime& runtime, const std::string& key, const facebook::jsi::Value& obj, bool shouldCommit) {
    if (!btree_ || !mmap_) {
        LOGE("insertRecInternal: btree or mmap is null");
        return facebook::jsi::Value(false);
    }
    
    try {
        // Step 1: Use reusable ArenaAllocator avoiding std::bad_alloc/new leakages
        arena_.reset();
        BinarySerializer::serialize(runtime, obj, arena_);
        
        // Step 2: Grab sequential offset and encrypt directly into arena
        size_t offset = next_free_offset_;
        size_t serialized_size = arena_.size();
        
        // Reserve space for: nonce (24) + ciphertext + mac (16)
        size_t encrypted_max = 24 + serialized_size + 16;
        arena_.reserve(encrypted_max);
        
        size_t encrypted_len = 0;
        if (crypto_) {
            crypto_->encryptInto(arena_.data(), serialized_size, 
                                 arena_.data() + serialized_size, encrypted_len);
        } else {
            std::memcpy(arena_.data() + serialized_size, arena_.data(), serialized_size);
            encrypted_len = serialized_size;
        }
        
        size_t payload_len = encrypted_len;
        uint32_t len32 = static_cast<uint32_t>(payload_len);
        
        char len_buf[4];
        std::memcpy(len_buf, &len32, 4);
        
        const char* data_start = reinterpret_cast<const char*>(arena_.data() + (crypto_ ? serialized_size : 0));
        
        if (wal_) {
            wal_->logPageWrite(offset, std::string(len_buf, 4));
            wal_->logPageWrite(offset + 4, std::string(data_start, payload_len));
        }
        
        mmap_->write(offset, std::string(len_buf, 4));
        mmap_->write(offset + 4, std::string(data_start, payload_len));
        
        // Push the needle cursor to point cleanly behind the record
        next_free_offset_ += 4 + payload_len;
        pbtree_->setNextFreeOffset(next_free_offset_);
        
        // Step 4: Trigger the zero-latency queued background logic
        btree_->insert(key, offset);
        
        if (shouldCommit && wal_) {
            wal_->logCommit();
        }
        
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
    if (!btree_ || !mmap_) return facebook::jsi::Value::undefined();
    
    try {
        size_t offset = btree_->find(key);
        if (offset == 0) return facebook::jsi::Value::undefined();
        
        std::string len_bytes = mmap_->read(offset, sizeof(uint32_t));
        uint32_t payload_len;
        std::memcpy(&payload_len, len_bytes.data(), sizeof(uint32_t));
        
        std::string data_bytes = mmap_->read(offset + sizeof(uint32_t), payload_len);
        
        std::shared_ptr<std::vector<uint8_t>> decrypted;
        if (crypto_) {
            auto decrypted_vec = crypto_->decryptAtOffset(reinterpret_cast<const uint8_t*>(data_bytes.data()), payload_len, offset);
            decrypted = std::make_shared<std::vector<uint8_t>>(std::move(decrypted_vec));
        } else {
            decrypted = std::make_shared<std::vector<uint8_t>>(data_bytes.begin(), data_bytes.end());
        }
        
        if (!decrypted->empty() && static_cast<BinaryType>((*decrypted)[0]) == BinaryType::Object) {
            auto proxy = std::make_shared<LazyRecordProxy>(std::move(decrypted));
            return facebook::jsi::Object::createFromHostObject(runtime, proxy);
        }

        auto [val, consumed] = BinarySerializer::deserialize(runtime, decrypted->data(), decrypted->size());
        
        return std::move(val);
    } catch (const std::exception& e) {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "SecureDB", "findRec error: %s", e.what());
#endif
        std::cerr << "SecureDB findRec error: " << e.what() << "\n";
        return facebook::jsi::Value::undefined();
    }
}

bool DBEngine::clearStorage() {
    if (!mmap_) return false;
    
    std::string path = mmap_->getPath();
    size_t size = mmap_->getSize();
    
    // 1. Close current mapping and reset all managers
    mmap_->close();
    btree_.reset();
    pbtree_.reset();
    if (wal_) {
        wal_.reset();
    }
    
    // 2. Remove files
    std::remove(path.c_str());
    std::remove((path + ".wal").c_str());
    
    // 3. Re-initialize
    return initStorage(path, size);
}

void installDBEngine(facebook::jsi::Runtime& runtime, std::unique_ptr<SecureCryptoContext> crypto) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "SecureDB", "installDBEngine: creating HostObject");
#endif
    std::unique_ptr<SecureCryptoContext> final_crypto = nullptr;
    if (crypto) {
        final_crypto = std::make_unique<CachedCryptoContext>(std::move(crypto));
    }
    
    auto dbEngine = std::make_shared<DBEngine>(std::move(final_crypto));
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
    if (!entries.isObject()) {
        return facebook::jsi::Value(false);
    }
    
    facebook::jsi::Object obj = entries.asObject(runtime);
    auto propNames = obj.getPropertyNames(runtime);
    size_t count = propNames.size(runtime);
    
    for (size_t i = 0; i < count; i++) {
        auto key = propNames.getValueAtIndex(runtime, i).asString(runtime);
        auto value = obj.getProperty(runtime, key);
        insertRecInternal(runtime, key.utf8(runtime), value, false);
    }
    
    if (wal_) {
        wal_->logCommit();
    }
    
    return facebook::jsi::Value(true);
}

facebook::jsi::Value DBEngine::getMultiple(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& keys) {
    if (!keys.isObject() || !keys.asObject(runtime).isArray(runtime)) {
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
    if (!btree_ || !pbtree_) return false;
    
    size_t offset = btree_->find(key);
    if (offset == 0) return false;
    
    pbtree_->insert(key, 0);
    return true;
}

std::vector<std::pair<std::string, facebook::jsi::Value>> DBEngine::rangeQuery(
    facebook::jsi::Runtime& runtime, 
    const std::string& startKey, 
    const std::string& endKey
) {
    std::vector<std::pair<std::string, facebook::jsi::Value>> results;
    
    if (!btree_) return results;
    
    auto rangeResults = btree_->range(startKey, endKey);
    for (const auto& [key, offset] : rangeResults) {
        if (offset > 0) {
            std::string len_bytes = mmap_->read(offset, sizeof(uint32_t));
            uint32_t payload_len;
            std::memcpy(&payload_len, len_bytes.data(), sizeof(uint32_t));
            
            std::string data_bytes = mmap_->read(offset + sizeof(uint32_t), payload_len);
            
            std::shared_ptr<std::vector<uint8_t>> decrypted;
            if (crypto_) {
                auto decrypted_vec = crypto_->decryptAtOffset(reinterpret_cast<const uint8_t*>(data_bytes.data()), payload_len, offset);
                decrypted = std::make_shared<std::vector<uint8_t>>(std::move(decrypted_vec));
            } else {
                decrypted = std::make_shared<std::vector<uint8_t>>(data_bytes.begin(), data_bytes.end());
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

}