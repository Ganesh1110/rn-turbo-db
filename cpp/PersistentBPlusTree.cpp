#include "PersistentBPlusTree.h"
#include "WALManager.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <functional>

namespace secure_db {

struct FreeBlock {
    uint64_t next;
};

uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

PersistentBPlusTree::PersistentBPlusTree(MMapRegion* mmap, WALManager* wal) 
    : mmap_(mmap), wal_(wal) {}

void PersistentBPlusTree::initWithConfig(const BTreeNodeConfig& config) {
    config_ = config;
    init();
}

void PersistentBPlusTree::init() {
    std::string header_bytes = mmap_->read(0, sizeof(TreeHeader));
    std::memcpy(&header_, header_bytes.data(), sizeof(TreeHeader));
    
    if (header_.magic != 0x42504C54) {
        std::memset(&header_, 0, sizeof(TreeHeader));
        header_.magic = 0x42504C54;
        header_.root_offset = 4096;
        header_.node_count = 1;
        header_.height = 1;
        header_.next_free_offset = 1024 * 1024; // Align with DBEngine start offset
        header_.free_list_head = 0;
        
        BTreeNode root_node;
        std::memset(&root_node, 0, sizeof(BTreeNode));
        root_node.is_leaf = true;
        write_node(header_.root_offset, root_node);
        
        checkpoint();
    } else {
        uint32_t expected_crc = header_.checksum;
        header_.checksum = 0;
        uint32_t actual_crc = calculate_crc32(reinterpret_cast<uint8_t*>(&header_), sizeof(TreeHeader));
        header_.checksum = expected_crc;
        
        if (expected_crc != actual_crc) {
            std::cerr << "CRITICAL: B+Tree Checksum Mismatch!\n";
        }
    }
}

void PersistentBPlusTree::checkpoint() {
    header_.checksum = 0;
    header_.checksum = calculate_crc32(reinterpret_cast<uint8_t*>(&header_), sizeof(TreeHeader));
    std::string encoded(reinterpret_cast<const char*>(&header_), sizeof(TreeHeader));
    
    if (wal_) {
        wal_->logPageWrite(0, encoded);
    } else {
        mmap_->write(0, encoded);
        mmap_->sync(0, sizeof(TreeHeader));
    }
}

uint64_t PersistentBPlusTree::allocate_node(bool is_leaf) {
    uint64_t offset;
    if (header_.free_list_head != 0) {
        offset = header_.free_list_head;
        std::string bytes = mmap_->read(offset, sizeof(FreeBlock));
        FreeBlock fb;
        std::memcpy(&fb, bytes.data(), sizeof(FreeBlock));
        header_.free_list_head = fb.next;
    } else {
        offset = 4096 + (header_.node_count * 4096);
        header_.node_count++;
    }
    
    BTreeNode node;
    std::memset(&node, 0, sizeof(BTreeNode));
    node.is_leaf = is_leaf;
    write_node(offset, node);
    
    return offset;
}

void PersistentBPlusTree::write_node(uint64_t offset, const BTreeNode& node) {
    std::string bytes(reinterpret_cast<const char*>(&node), sizeof(BTreeNode));
    if (wal_) {
        wal_->logPageWrite(offset, bytes);
    } else {
        mmap_->write(offset, bytes);
    }
}

BTreeNode PersistentBPlusTree::read_node(uint64_t offset) {
    std::string bytes = mmap_->read(offset, sizeof(BTreeNode));
    BTreeNode node;
    std::memcpy(&node, bytes.data(), sizeof(BTreeNode));
    return node;
}

void PersistentBPlusTree::insert(const std::string& key, size_t data_offset, bool shouldCheckpoint) {
    uint64_t root_off = header_.root_offset;
    BTreeNode root = read_node(root_off);
    
    if (root.num_keys == BTreeNode::MAX_KEYS) {
        uint64_t new_root_off = allocate_node(false);
        BTreeNode new_root = read_node(new_root_off);
        new_root.children[0] = root_off;
        
        split_child(new_root_off, new_root, 0, root_off, root);
        
        header_.root_offset = new_root_off;
        header_.height++;
        
        insert_non_full(new_root_off, key, data_offset);
    } else {
        insert_non_full(root_off, key, data_offset);
    }
    
    if (shouldCheckpoint) {
        checkpoint();
    }
}

void PersistentBPlusTree::split_child(uint64_t parent_off, BTreeNode& parent, uint32_t child_idx, uint64_t child_off, BTreeNode& child) {
    uint64_t sibling_off = allocate_node(child.is_leaf);
    BTreeNode sibling = read_node(sibling_off);
    
    uint32_t t = BTreeNode::MAX_KEYS / 2;
    sibling.num_keys = BTreeNode::MAX_KEYS - t - 1;
    
    for (uint32_t j = 0; j < sibling.num_keys; j++) {
        std::strncpy(sibling.keys[j], child.keys[j + t + 1], 63);
        sibling.keys[j][63] = '\0';
        sibling.values[j] = child.values[j + t + 1];
    }
    
    if (!child.is_leaf) {
        for (uint32_t j = 0; j <= sibling.num_keys; j++) {
            sibling.children[j] = child.children[j + t + 1];
        }
    }
    
    child.num_keys = t;
    
    for (uint32_t j = parent.num_keys; j > child_idx; j--) {
        parent.children[j + 1] = parent.children[j];
    }
    parent.children[child_idx + 1] = sibling_off;
    
    for (uint32_t j = parent.num_keys; j > child_idx; j--) {
        std::strncpy(parent.keys[j], parent.keys[j - 1], 63);
        parent.keys[j][63] = '\0';
        parent.values[j] = parent.values[j - 1];
    }
    
    std::strncpy(parent.keys[child_idx], child.keys[t], 63);
    parent.keys[child_idx][63] = '\0';
    parent.values[child_idx] = child.values[t];
    parent.num_keys++;
    
    write_node(parent_off, parent);
    write_node(child_off, child);
    write_node(sibling_off, sibling);
}

void PersistentBPlusTree::insert_non_full(uint64_t node_off, const std::string& key, size_t data_offset) {
    BTreeNode node = read_node(node_off);
    
    if (node.is_leaf) {
        int i = node.num_keys - 1;
        // Check if key already exists first
        bool found = false;
        for (uint32_t j = 0; j < node.num_keys; j++) {
            if (key == std::string(node.keys[j])) {
                node.values[j] = data_offset;
                found = true;
                break;
            }
        }
        
        if (!found) {
            while (i >= 0 && key < std::string(node.keys[i])) {
                std::strncpy(node.keys[i + 1], node.keys[i], 63);
                node.keys[i + 1][63] = '\0';
                node.values[i + 1] = node.values[i];
                i--;
            }
            std::strncpy(node.keys[i + 1], key.c_str(), 63);
            node.keys[i + 1][63] = '\0';
            node.values[i + 1] = data_offset;
            node.num_keys++;
        }
        write_node(node_off, node);
    } else {
        int i = node.num_keys - 1;
        while (i >= 0 && key < std::string(node.keys[i])) {
            i--;
        }
        i++;
        uint64_t child_off = node.children[i];
        BTreeNode child = read_node(child_off);
        
        if (child.num_keys == BTreeNode::MAX_KEYS) {
            split_child(node_off, node, i, child_off, child);
            if (key > std::string(node.keys[i])) {
                i++;
            }
        }
        insert_non_full(node.children[i], key, data_offset);
    }
}

size_t PersistentBPlusTree::find(const std::string& key) {
    uint64_t curr_off = header_.root_offset;
    while (true) {
        BTreeNode node = read_node(curr_off);
        uint32_t i = 0;
        while (i < node.num_keys && key > std::string(node.keys[i])) {
            i++;
        }
        
        if (i < node.num_keys && key == std::string(node.keys[i])) {
            return node.values[i];
        }
        
        if (node.is_leaf) {
            return 0;
        }
        curr_off = node.children[i];
    }
}

std::vector<std::pair<std::string, size_t>> PersistentBPlusTree::range(const std::string& start_key, const std::string& end_key) {
    std::vector<std::pair<std::string, size_t>> results;
    
    if (header_.root_offset == 0) return results;
    
    std::function<void(uint64_t)> traverse = [&](uint64_t node_off) {
        BTreeNode node = read_node(node_off);
        
        for (uint32_t i = 0; i < node.num_keys; i++) {
            std::string key(node.keys[i]);
            if (key >= start_key && key <= end_key) {
                results.emplace_back(key, node.values[i]);
            }
        }
        
        if (!node.is_leaf) {
            for (uint32_t i = 0; i <= node.num_keys; i++) {
                traverse(node.children[i]);
            }
        }
    };
    
    traverse(header_.root_offset);
    return results;
}

std::vector<std::string> PersistentBPlusTree::getAllKeys() {
    std::vector<std::string> keys;
    if (header_.root_offset == 0) return keys;
    
    std::function<void(uint64_t)> traverse = [&](uint64_t node_off) {
        if (node_off == 0) return;
        BTreeNode node = read_node(node_off);
        
        for (uint32_t i = 0; i <= node.num_keys; i++) {
            if (!node.is_leaf) {
                traverse(node.children[i]);
            }
            if (i < node.num_keys) {
                keys.push_back(std::string(node.keys[i]));
            }
        }
    };
    
    traverse(header_.root_offset);
    return keys;
}

}
