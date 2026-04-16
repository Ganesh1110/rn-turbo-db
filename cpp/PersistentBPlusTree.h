#pragma once
#include "MMapRegion.h"
#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <shared_mutex>

namespace secure_db {

uint32_t calculate_crc32(const uint8_t* data, size_t length);

#pragma pack(push, 1)
struct TreeHeader {
    uint64_t magic;
    uint64_t root_offset;
    uint64_t free_list_head;
    uint64_t next_free_offset;
    uint32_t height;
    uint32_t node_count;
    uint32_t checksum;
};
#pragma pack(pop)

struct BTreeNodeConfig {
    uint32_t max_keys = 32;
    uint32_t key_size = 64;
    
    BTreeNodeConfig() = default;
    BTreeNodeConfig(uint32_t mk, uint32_t ks) : max_keys(mk), key_size(ks) {}
};

struct BTreeNode {
    bool is_leaf;
    uint32_t num_keys;
    static constexpr size_t MAX_KEYS = 32;
    static constexpr size_t KEY_SIZE = 64;
    // Allow MAX_KEYS + 1 to handle temporary state during splits/inserts
    char keys[MAX_KEYS + 1][KEY_SIZE];
    uint64_t values[MAX_KEYS + 1];
    uint64_t children[MAX_KEYS + 2];
};

// Hot Node Cache for B+ Tree Pages
class BTreeNodeCache {
public:
    static constexpr size_t MAX_CACHE_SIZE = 1024; // ~4MB RAM footprint

    bool get(uint64_t offset, BTreeNode& node) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(offset);
        if (it != cache_.end()) {
            node = *it->second;
            return true;
        }
        return false;
    }

    void put(uint64_t offset, const BTreeNode& node) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.size() >= MAX_CACHE_SIZE) cache_.clear();
        cache_[offset] = std::make_shared<BTreeNode>(node);
    }

    void remove(uint64_t offset) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.erase(offset);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }

private:
    std::unordered_map<uint64_t, std::shared_ptr<BTreeNode>> cache_;
    std::mutex mutex_;
};

class PersistentBPlusTree {
public:
    PersistentBPlusTree(MMapRegion* mmap, class WALManager* wal = nullptr);
    
    void init();
    void initWithConfig(const BTreeNodeConfig& config);
    
    void insert(const std::string& key, size_t data_offset, bool shouldCheckpoint = true);
    
    size_t find(const std::string& key);
    
    std::vector<std::pair<std::string, size_t>> range(const std::string& start_key, const std::string& end_key);
    
    std::vector<std::string> getAllKeys();
    
    void checkpoint();

    const TreeHeader& getHeader() const { 
        std::shared_lock lock(tree_mutex_);
        return header_; 
    }
    
    void setNextFreeOffset(uint64_t offset) { 
        std::unique_lock lock(tree_mutex_);
        header_.next_free_offset = offset; 
    }
    
    const BTreeNodeConfig& getConfig() const { return config_; }

private:
    MMapRegion* mmap_;
    class WALManager* wal_;
    TreeHeader header_;
    BTreeNodeConfig config_;
    BTreeNodeCache cache_;
    mutable std::shared_mutex tree_mutex_;
    
    uint64_t allocate_node(bool is_leaf);
    void write_node(uint64_t offset, const BTreeNode& node);
    BTreeNode read_node(uint64_t offset);

    void split_child(uint64_t parent_off, BTreeNode& parent, uint32_t child_idx, uint64_t child_off, BTreeNode& child);
    void insert_non_full(uint64_t node_off, const std::string& key, size_t data_offset);
};

}
