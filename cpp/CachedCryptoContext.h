#pragma once
#include "SecureCryptoContext.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <cstring>

namespace secure_db {

class CachedCryptoContext : public SecureCryptoContext {
public:
    CachedCryptoContext(std::unique_ptr<SecureCryptoContext> inner, size_t cache_pages = 64);
    ~CachedCryptoContext() override;
    
    std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t length) override;
    std::vector<uint8_t> decrypt(const uint8_t* ciphertext, size_t length) override;
    
    void encryptInto(const uint8_t* plaintext, size_t length, 
                     uint8_t* out_buffer, size_t& out_length) override;
    bool decryptInto(const uint8_t* ciphertext, size_t length,
                     uint8_t* out_buffer, size_t& out_length) override;

    std::vector<uint8_t> decryptAtOffset(const uint8_t* ciphertext, size_t length, uint64_t record_offset);
    bool decryptIntoAtOffset(const uint8_t* ciphertext, size_t length, uint64_t record_offset,
                             uint8_t* out_buffer, size_t& out_length);

    void invalidatePage(uint64_t record_offset);
    void clearCache();

private:
    std::unique_ptr<SecureCryptoContext> inner_;
    
    struct CacheEntry {
        uint64_t record_offset;
        std::vector<uint8_t> decrypted_data;
    };
    
    std::list<CacheEntry> lru_cache_;
    std::unordered_map<uint64_t, std::list<CacheEntry>::iterator> cache_map_;
    std::mutex cache_mutex_;
    size_t max_cache_pages_;
    
    bool lookupCache(uint64_t record_offset, std::vector<uint8_t>& out);
    void insertCache(uint64_t record_offset, const uint8_t* data, size_t len);
};

}
