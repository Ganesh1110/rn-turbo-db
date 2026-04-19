#include "LRUCache.h"

namespace turbo_db {

LRUCache::LRUCache(size_t max_size) 
    : max_size_(max_size), current_size_(0) {}

LRUCache::~LRUCache() = default;

void LRUCache::put(const std::string& key, const uint8_t* data, size_t length, size_t offset, bool is_encrypted) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        current_size_ -= it->second.second.data.size();
        lru_order_.erase(it->second.first);
    }
    
    CacheEntry entry;
    entry.data.assign(data, data + length);
    entry.offset = offset;
    entry.length = length;
    entry.is_encrypted = is_encrypted;
    
    current_size_ += entry.data.size();
    lru_order_.push_front(key);
    cache_[key] = {lru_order_.begin(), std::move(entry)};
    
    evictIfNeeded();
}

LRUCache::CacheEntry* LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        hits_++;
        moveToFront(key);
        return &it->second.second;
    }
    
    misses_++;
    return nullptr;
}

bool LRUCache::contains(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.find(key) != cache_.end();
}

void LRUCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        current_size_ -= it->second.second.data.size();
        lru_order_.erase(it->second.first);
        cache_.erase(it);
    }
}

void LRUCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    lru_order_.clear();
    current_size_ = 0;
}

size_t LRUCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

void LRUCache::resetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    hits_ = 0;
    misses_ = 0;
}

void LRUCache::evictIfNeeded() {
    while (current_size_ > max_size_ && !lru_order_.empty()) {
        auto last = lru_order_.back();
        auto it = cache_.find(last);
        if (it != cache_.end()) {
            current_size_ -= it->second.second.data.size();
            cache_.erase(it);
        }
        lru_order_.pop_back();
    }
}

void LRUCache::moveToFront(const std::string& key) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        lru_order_.erase(it->second.first);
        lru_order_.push_front(key);
        it->second.first = lru_order_.begin();
    }
}

}