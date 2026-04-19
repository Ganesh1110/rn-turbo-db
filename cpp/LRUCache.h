#pragma once

#include <unordered_map>
#include <list>
#include <string>
#include <mutex>
#include <memory>
#include <vector>

namespace turbo_db {

class LRUCache {
public:
    explicit LRUCache(size_t max_size = 1024);
    ~LRUCache();

    struct CacheEntry {
        std::vector<uint8_t> data;
        size_t offset;
        size_t length;
        bool is_encrypted;
    };

    void put(const std::string& key, const uint8_t* data, size_t length, size_t offset, bool is_encrypted = false);
    CacheEntry* get(const std::string& key);
    bool contains(const std::string& key) const;
    void remove(const std::string& key);
    void clear();
    size_t size() const;
    size_t hits() const { return hits_; }
    size_t misses() const { return misses_; }
    void resetStats();

private:
    void evictIfNeeded();
    void moveToFront(const std::string& key);

    size_t max_size_;
    size_t current_size_;
    std::unordered_map<std::string, std::pair<std::list<std::string>::iterator, CacheEntry>> cache_;
    std::list<std::string> lru_order_;
    mutable std::mutex mutex_;
    size_t hits_ = 0;
    size_t misses_ = 0;
};

}