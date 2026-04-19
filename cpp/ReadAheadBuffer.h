#pragma once

#include <deque>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace turbo_db {

struct ReadAheadEntry {
    std::string key;
    std::vector<uint8_t> data;
    size_t offset;
    size_t length;
};

class ReadAheadBuffer {
public:
    explicit ReadAheadBuffer(size_t max_size = 2048, size_t prefetch_count = 16);
    ~ReadAheadBuffer() = default;

    void prefetch(const std::vector<std::string>& keys, 
                  std::function<std::vector<uint8_t>(const std::string&)> fetcher);
    
    ReadAheadEntry* get(const std::string& key);
    bool contains(const std::string& key) const;
    void clear();
    size_t size() const;

private:
    void evictOldest();

    size_t max_size_;
    size_t prefetch_count_;
    std::unordered_map<std::string, ReadAheadEntry> buffer_;
    std::deque<std::string> access_order_;
    mutable std::mutex mutex_;
};

}