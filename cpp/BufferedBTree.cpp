#include "BufferedBTree.h"
#include <algorithm>
#include <map>

namespace secure_db {

BufferedBTree::BufferedBTree(PersistentBPlusTree* tree) : tree_(tree) {
    worker_ = std::thread(&BufferedBTree::worker_thread, this);
}

BufferedBTree::~BufferedBTree() {
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        stop_worker_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void BufferedBTree::insert(const std::string& key, size_t data_offset) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    // Check main buffer
    bool found = false;
    for (auto& op : write_buffer_) {
        if (op.key == key) {
            op.data_offset = data_offset;
            found = true;
            break;
        }
    }
    
    // Also check flushing buffer to avoid stale state
    if (!found) {
        for (auto& op : flushing_buffer_) {
            if (op.key == key) {
                op.data_offset = data_offset;
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        write_buffer_.push_back({key, data_offset});
    }
    
    if (write_buffer_.size() >= BATCH_SIZE) {
        flush_requested_ = true;
        cv_.notify_one();
    }
}

void BufferedBTree::worker_thread() {
    while (!stop_worker_) {
        std::deque<InsertOperation> to_flush;
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            cv_.wait(lock, [this] { return stop_worker_ || flush_requested_ || write_buffer_.size() >= BATCH_SIZE; });
            
            if (stop_worker_ && write_buffer_.empty()) break;
            
            if (!write_buffer_.empty()) {
                flushing_buffer_ = std::move(write_buffer_);
                write_buffer_ = std::deque<InsertOperation>();
                to_flush = flushing_buffer_; // Copy for the actual disk write
            }
            flush_requested_ = false;
        }
        
        if (!to_flush.empty()) {
            for (const auto& op : to_flush) {
                tree_->insert(op.key, op.data_offset, false);
            }
            tree_->checkpoint();
            
            // Clear flushing buffer after disk write is complete
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                flushing_buffer_.clear();
            }
        }
    }
}

size_t BufferedBTree::find(const std::string& key) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    // 1. Check current write buffer (newest)
    for (auto it = write_buffer_.rbegin(); it != write_buffer_.rend(); ++it) {
        if (it->key == key) return it->data_offset;
    }
    
    // 2. Check flushing buffer (middle ground)
    for (auto it = flushing_buffer_.rbegin(); it != flushing_buffer_.rend(); ++it) {
        if (it->key == key) return it->data_offset;
    }
    
    // 3. Fallback to disk tree
    return tree_->find(key);
}

void BufferedBTree::flush() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    flush_requested_ = true;
    cv_.notify_one();
}

std::vector<std::string> BufferedBTree::getAllKeys() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    // Use a map to keep track of the latest offset for each key
    std::map<std::string, size_t> key_map;
    
    // 1. Get all keys from disk
    std::vector<std::string> disk_keys = tree_->getAllKeys();
    for (const auto& key : disk_keys) {
        key_map[key] = tree_->find(key);
    }
    
    // 2. Overlay flushing buffer
    for (const auto& op : flushing_buffer_) {
        key_map[op.key] = op.data_offset;
    }
    
    // 3. Overlay write buffer
    for (const auto& op : write_buffer_) {
        key_map[op.key] = op.data_offset;
    }
    
    // 4. Collect keys with non-zero offsets
    std::vector<std::string> results;
    for (const auto& [key, offset] : key_map) {
        if (offset > 0) {
            results.push_back(key);
        }
    }
    
    return results;
}

std::vector<std::pair<std::string, size_t>> BufferedBTree::range(const std::string& start_key, const std::string& end_key) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    // Use a map for merging and sorting
    std::map<std::string, size_t> key_map;
    
    // 1. Get from disk
    auto disk_results = tree_->range(start_key, end_key);
    for (const auto& res : disk_results) {
        key_map[res.first] = res.second;
    }
    
    // 2. Overlay flushing buffer
    for (const auto& op : flushing_buffer_) {
        if (op.key >= start_key && op.key <= end_key) {
            key_map[op.key] = op.data_offset;
        }
    }
    
    // 3. Overlay write buffer
    for (const auto& op : write_buffer_) {
        if (op.key >= start_key && op.key <= end_key) {
            key_map[op.key] = op.data_offset;
        }
    }
    
    // 4. Collect results with non-zero offsets
    std::vector<std::pair<std::string, size_t>> results;
    for (const auto& [key, offset] : key_map) {
        if (offset > 0) {
            results.push_back({key, offset});
        }
    }
    
    return results;
}

}
