#include "BufferedBTree.h"

namespace secure_db {

BufferedBTree::BufferedBTree(PersistentBPlusTree* tree) : tree_(tree) {}

void BufferedBTree::insert(const std::string& key, size_t data_offset) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    // If it already exists in the pending buffer, update the pointer natively without disk I/O
    bool found = false;
    for (auto& op : write_buffer_) {
        if (op.key == key) {
            op.data_offset = data_offset;
            found = true;
            break;
        }
    }
    
    if (!found) {
        write_buffer_.push_back({key, data_offset});
    }
    
    // Check batched threshold
    if (write_buffer_.size() >= BATCH_SIZE) {
        // Flush buffer sequentially for this Phase 4 prototype.
        // Moving this loop to a std::thread background worker achieves true Zero-Latency inserts.
        for (const auto& op : write_buffer_) {
            tree_->insert(op.key, op.data_offset, false);
        }
        tree_->checkpoint();
        write_buffer_.clear();
    }
}

size_t BufferedBTree::find(const std::string& key) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    // LIFO (Last-In-First-Out) search in the uncommitted buffer ensures we get
    // the absolute freshest data before falling back to the disk blocks
    for (auto it = write_buffer_.rbegin(); it != write_buffer_.rend(); ++it) {
        if (it->key == key) {
            return it->data_offset;
        }
    }
    
    // Fallback traversing the heavy on-disk tree
    return tree_->find(key);
}

void BufferedBTree::flush() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (write_buffer_.empty()) return;
    
    for (const auto& op : write_buffer_) {
        tree_->insert(op.key, op.data_offset, false);
    }
    tree_->checkpoint();
    write_buffer_.clear();
}

std::vector<std::string> BufferedBTree::getAllKeys() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    std::vector<std::string> disk_keys = tree_->getAllKeys();
    
    // Combine with buffer (and handle duplicates)
    std::vector<std::string> results = disk_keys;
    for (const auto& op : write_buffer_) {
        if (std::find(results.begin(), results.end(), op.key) == results.end()) {
            results.push_back(op.key);
        }
    }
    return results;
}

std::vector<std::pair<std::string, size_t>> BufferedBTree::range(const std::string& start_key, const std::string& end_key) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    auto results = tree_->range(start_key, end_key);
    
    // Merge with buffer
    for (const auto& op : write_buffer_) {
        if (op.key >= start_key && op.key <= end_key) {
            // Update if already in results, else insert
            bool found = false;
            for (auto& res : results) {
                if (res.first == op.key) {
                    res.second = op.data_offset;
                    found = true;
                    break;
                }
            }
            if (!found) {
                results.push_back({op.key, op.data_offset});
            }
        }
    }
    
    // Sort results as it might be unordered due to buffer merge
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    
    return results;
}

}
