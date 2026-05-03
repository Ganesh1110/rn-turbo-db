#pragma once
#include <deque>
#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include "PersistentBPlusTree.h"

namespace turbo_db {

struct InsertOperation {
    std::string key;
    size_t data_offset;
};

// Proxies writes to the PersistentBPlusTree by batching them.
// Prevents node-split rebalancing latency from stalling the UI thread.
class BufferedBTree {
public:
    static constexpr size_t BATCH_SIZE = 1024;

    BufferedBTree(PersistentBPlusTree* tree);
    ~BufferedBTree();
    
    // Pushes into the deque, triggers background flush if BATCH_SIZE exceeded
    void insert(const std::string& key, size_t data_offset);
    
    // Checks memory buffer first, then cascades to disk
    size_t find(const std::string& key);
    
    // Force commit to disk tree
    void flush();

    // Clear write buffer and disk tree
    void clear();
    
    // Get all keys from buffer and tree
    std::vector<std::string> getAllKeys();

    // Range query across buffer and disk tree
    std::vector<std::pair<std::string, size_t>> range(const std::string& start_key, const std::string& end_key);

    /**
     * True native prefix scan — merges in-buffer keys with disk B+Tree prefix results.
     * More efficient than range(prefix, prefix+'\uffff') for large datasets.
     */
    std::vector<std::pair<std::string, size_t>> prefixSearch(const std::string& prefix);

private:
    void worker_thread();

    PersistentBPlusTree* tree_;
    std::deque<InsertOperation> write_buffer_;
    std::deque<InsertOperation> flushing_buffer_;
    std::mutex buffer_mutex_;
    
    // Worker state
    std::thread worker_;
    std::condition_variable cv_;
    std::atomic<bool> stop_worker_{false};
    std::atomic<bool> flush_requested_{false};
};

}
