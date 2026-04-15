#pragma once
#include <deque>
#include <string>
#include <mutex>
#include "PersistentBPlusTree.h"

namespace secure_db {

struct InsertOperation {
    std::string key;
    size_t data_offset;
};

// Proxies writes to the PersistentBPlusTree by batching them.
// Prevents node-split rebalancing latency from stalling the UI thread.
class BufferedBTree {
public:
    static constexpr size_t BATCH_SIZE = 64;

    BufferedBTree(PersistentBPlusTree* tree);
    
    // Pushes into the deque, flushing only if capacity triggers
    void insert(const std::string& key, size_t data_offset);
    
    // Checks memory buffer first, then cascades to disk
    size_t find(const std::string& key);
    
    // Force commit to disk tree
    void flush();
    
    // Get all keys from buffer and tree
    std::vector<std::string> getAllKeys();

    // Range query across buffer and disk tree
    std::vector<std::pair<std::string, size_t>> range(const std::string& start_key, const std::string& end_key);

private:
    PersistentBPlusTree* tree_;
    std::deque<InsertOperation> write_buffer_;
    std::mutex buffer_mutex_;
};

}
