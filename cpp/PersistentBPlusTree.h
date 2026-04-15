#pragma once
#include "MMapRegion.h"
#include <string>
#include <cstdint>
#include <vector>

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
    char keys[MAX_KEYS][KEY_SIZE];
    uint64_t values[MAX_KEYS];
    uint64_t children[MAX_KEYS + 1];
};

class PersistentBPlusTree {
public:
    PersistentBPlusTree(MMapRegion* mmap, class WALManager* wal = nullptr);
    
    void init();
    void initWithConfig(const BTreeNodeConfig& config);
    
    void insert(const std::string& key, size_t data_offset);
    
    size_t find(const std::string& key);
    
    std::vector<std::pair<std::string, size_t>> range(const std::string& start_key, const std::string& end_key);
    
    void checkpoint();

    const TreeHeader& getHeader() const { return header_; }
    void setNextFreeOffset(uint64_t offset) { header_.next_free_offset = offset; }
    const BTreeNodeConfig& getConfig() const { return config_; }

private:
    MMapRegion* mmap_;
    class WALManager* wal_;
    TreeHeader header_;
    BTreeNodeConfig config_;
    
    uint64_t allocate_node(bool is_leaf);
    void write_node(uint64_t offset, const BTreeNode& node);
    BTreeNode read_node(uint64_t offset);

    void split_child(uint64_t parent_off, BTreeNode& parent, uint32_t child_idx, uint64_t child_off, BTreeNode& child);
    void insert_non_full(uint64_t node_off, const std::string& key, size_t data_offset);
};

}
