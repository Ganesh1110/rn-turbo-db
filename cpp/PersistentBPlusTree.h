#pragma once
#include "MMapRegion.h"
#include <string>
#include <cstdint>
#include <vector>

namespace secure_db {

// Fast CRC32 to satisfy the Checksum requirement without a huge OpenSSL dependency
uint32_t calculate_crc32(const uint8_t* data, size_t length);

#pragma pack(push, 1)
struct TreeHeader {
    uint64_t magic;          // e.g. 0x42504C54 ('BPLT')
    uint64_t root_offset;    // Offset in mmap where root node lives
    uint64_t free_list_head; // Linked list of freed 4KB blocks
    uint64_t next_free_offset; // Next sequential write offset for data records
    uint32_t height;
    uint32_t node_count;
    uint32_t checksum;       // CRC32 of everything above this line
};
#pragma pack(pop)

// Represents a 4KB B-Tree Node natively on disk
struct BTreeNode {
    bool is_leaf;
    uint32_t num_keys;
    // For simplicity in this architectural phase, we define a fixed capacity
    // Real implementation would dynamically pack keys into the 4096 bytes
    static constexpr size_t MAX_KEYS = 32;
    char keys[MAX_KEYS][64]; // Fixed size keys for the prototype
    uint64_t values[MAX_KEYS]; // Data offsets
    uint64_t children[MAX_KEYS + 1]; // Child node offsets
};

class PersistentBPlusTree {
public:
    PersistentBPlusTree(MMapRegion* mmap, class WALManager* wal = nullptr);
    
    // Format the MMap region or load existing header / verify checksum
    void init();
    
    // Performs insertion, causing node splits if pages fill up
    void insert(const std::string& key, size_t data_offset);
    
    // O(log N) memory traversal
    size_t find(const std::string& key);
    
    // Atomic header flush
    void checkpoint();

    // Metadata accessors
    const TreeHeader& getHeader() const { return header_; }
    void setNextFreeOffset(uint64_t offset) { header_.next_free_offset = offset; }

private:
    MMapRegion* mmap_;
    class WALManager* wal_;
    TreeHeader header_;
    
    // Internal node manager
    uint64_t allocate_node(bool is_leaf);
    void write_node(uint64_t offset, const BTreeNode& node);
    BTreeNode read_node(uint64_t offset);

    void split_child(uint64_t parent_off, BTreeNode& parent, uint32_t child_idx, uint64_t child_off, BTreeNode& child);
    void insert_non_full(uint64_t node_off, const std::string& key, size_t data_offset);
};

}
