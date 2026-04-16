#include "CachedCryptoContext.h"

namespace secure_db {

CachedCryptoContext::CachedCryptoContext(std::unique_ptr<SecureCryptoContext> inner, size_t cache_pages)
    : inner_(std::move(inner)), max_cache_pages_(cache_pages) {}

CachedCryptoContext::~CachedCryptoContext() = default;

std::vector<uint8_t> CachedCryptoContext::encrypt(const uint8_t* plaintext, size_t length) {
    return inner_->encrypt(plaintext, length);
}

std::vector<uint8_t> CachedCryptoContext::decrypt(const uint8_t* ciphertext, size_t length) {
    return inner_->decrypt(ciphertext, length);
}

std::vector<uint8_t> CachedCryptoContext::decryptAtOffset(const uint8_t* ciphertext, size_t length, uint64_t record_offset) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::vector<uint8_t> cached;
    if (lookupCache(record_offset, cached)) {
        return cached;
    }
    
    std::vector<uint8_t> decrypted = inner_->decrypt(ciphertext, length);
    insertCache(record_offset, decrypted.data(), decrypted.size());
    
    return decrypted;
}

void CachedCryptoContext::encryptInto(const uint8_t* plaintext, size_t length, 
                                      uint8_t* out_buffer, size_t& out_length) {
    inner_->encryptInto(plaintext, length, out_buffer, out_length);
}

bool CachedCryptoContext::decryptInto(const uint8_t* ciphertext, size_t length,
                                      uint8_t* out_buffer, size_t& out_length) {
    return inner_->decryptInto(ciphertext, length, out_buffer, out_length);
}

bool CachedCryptoContext::decryptIntoAtOffset(const uint8_t* ciphertext, size_t length, uint64_t record_offset,
                                               uint8_t* out_buffer, size_t& out_length) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_map_.find(record_offset);
    if (it != cache_map_.end()) {
        lru_cache_.splice(lru_cache_.begin(), lru_cache_, it->second);
        out_length = it->second->decrypted_data.size();
        std::memcpy(out_buffer, it->second->decrypted_data.data(), out_length);
        return true;
    }

    if (inner_->decryptInto(ciphertext, length, out_buffer, out_length)) {
        insertCache(record_offset, out_buffer, out_length);
        return true;
    }
    return false;
}

bool CachedCryptoContext::lookupCache(uint64_t record_offset, std::vector<uint8_t>& out) {
    auto it = cache_map_.find(record_offset);
    if (it == cache_map_.end()) return false;
    
    lru_cache_.splice(lru_cache_.begin(), lru_cache_, it->second);
    out = it->second->decrypted_data;
    return true;
}

void CachedCryptoContext::insertCache(uint64_t record_offset, const uint8_t* data, size_t len) {
    if (lru_cache_.size() >= max_cache_pages_) {
        auto& oldest = lru_cache_.back();
        cache_map_.erase(oldest.record_offset);
        lru_cache_.pop_back();
    }
    
    CacheEntry entry;
    entry.record_offset = record_offset;
    entry.decrypted_data.assign(data, data + len);
    
    lru_cache_.push_front(std::move(entry));
    cache_map_[record_offset] = lru_cache_.begin();
}

void CachedCryptoContext::clearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    lru_cache_.clear();
    cache_map_.clear();
}

void CachedCryptoContext::invalidatePage(uint64_t record_offset) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_map_.find(record_offset);
    if (it != cache_map_.end()) {
        lru_cache_.erase(it->second);
        cache_map_.erase(it);
    }
}

} // namespace secure_db
