#include "ReadAheadBuffer.h"

namespace turbo_db {

ReadAheadBuffer::ReadAheadBuffer(size_t max_size, size_t prefetch_count)
    : max_size_(max_size), prefetch_count_(prefetch_count) {}

void ReadAheadBuffer::prefetch(const std::vector<std::string>& keys, 
                                std::function<std::vector<uint8_t>(const std::string&)> fetcher) {
    if (keys.empty() || !fetcher) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& key : keys) {
        if (buffer_.find(key) != buffer_.end()) continue;
        
        try {
            auto data = fetcher(key);
            if (!data.empty()) {
                ReadAheadEntry entry;
                entry.key = key;
                entry.data = std::move(data);
                entry.length = entry.data.size();
                
                size_t entry_size = entry.key.size() + entry.data.size();
                while (currentBufferSize() + entry_size > max_size_ && !buffer_.empty()) {
                    evictOldest();
                }
                
                buffer_[key] = std::move(entry);
                access_order_.push_back(key);
            }
        } catch (...) {
            // Skip keys that fail to fetch
        }
    }
}

ReadAheadBuffer::ReadAheadEntry* ReadAheadBuffer::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = buffer_.find(key);
    if (it != buffer_.end()) {
        // Move to end (most recently used)
        for (auto it2 = access_order_.begin(); it2 != access_order_.end(); ++it2) {
            if (*it2 == key) {
                access_order_.erase(it2);
                break;
            }
        }
        access_order_.push_back(key);
        return &it->second;
    }
    return nullptr;
}

bool ReadAheadBuffer::contains(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.find(key) != buffer_.end();
}

void ReadAheadBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    access_order_.clear();
}

size_t ReadAheadBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

void ReadAheadBuffer::evictOldest() {
    if (!access_order_.empty()) {
        auto oldest = access_order_.front();
        access_order_.pop_front();
        
        auto it = buffer_.find(oldest);
        if (it != buffer_.end()) {
            buffer_.erase(it);
        }
    }
}

size_t ReadAheadBuffer::currentBufferSize() const {
    size_t total = 0;
    for (const auto& entry : buffer_) {
        total += entry.second.key.size() + entry.second.data.size();
    }
    return total;
}

}