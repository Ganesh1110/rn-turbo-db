#include "PersistentBPlusTree.h"
#include "WALManager.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <functional>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TurboDB_Tree", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TurboDB_Tree", __VA_ARGS__)
#else
#define LOGI(...) do {} while(0)
#define LOGE(...) do {} while(0)
#endif

namespace turbo_db {

struct FreeBlock { uint64_t next; };

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
    : mmap_(mmap), wal_(wal), cache_(512) {}

bool PersistentBPlusTree::needsMigration() const {
    return needs_migration_;
}

void PersistentBPlusTree::initWithConfig(const BTreeNodeConfig& config) {
    config_ = config;
    init();
}

void PersistentBPlusTree::init() {
    std::unique_lock lock(tree_mutex_);
    std::string header_bytes = mmap_->read(0, sizeof(TreeHeader));
    std::memcpy(&header_, header_bytes.data(), sizeof(TreeHeader));

    const bool is_fresh = (header_.magic != TreeHeader::MAGIC);
    const bool wrong_version = !is_fresh && (header_.format_version != TREE_FORMAT_VERSION);

    if (wrong_version) {
        LOGI("PersistentBPlusTree: format version mismatch (disk=%u, code=%u) — migration required",
             header_.format_version, TREE_FORMAT_VERSION);
        needs_migration_ = true;
        // Fall through: treat as fresh DB so we can at least initialize safely
    }

    if (is_fresh || wrong_version) {
        std::memset(&header_, 0, sizeof(TreeHeader));
        header_.magic          = TreeHeader::MAGIC;
        header_.root_offset    = 4096;
        header_.node_count     = 1;
        header_.height         = 1;
        header_.next_free_offset = 1024 * 1024;
        header_.free_list_head = 0;
        header_.format_version = TREE_FORMAT_VERSION;

        BTreeNode root_node;
        std::memset(&root_node, 0, sizeof(BTreeNode));
        root_node.is_leaf = true;
        write_node(header_.root_offset, root_node);

        // Write header with checksum
        header_.checksum = 0;
        header_.checksum = calculate_crc32(
            reinterpret_cast<const uint8_t*>(&header_), sizeof(TreeHeader));
        std::string encoded(reinterpret_cast<const char*>(&header_), sizeof(TreeHeader));
        if (wal_) {
            wal_->logPageWrite(0, encoded);
            wal_->logCommit();
            wal_->sync();
        } else {
            mmap_->write(0, encoded);
            mmap_->sync(0, sizeof(TreeHeader));
        }
    } else {
        // Validate existing header checksum
        uint32_t expected_crc = header_.checksum;
        header_.checksum = 0;
        uint32_t actual_crc = calculate_crc32(
            reinterpret_cast<const uint8_t*>(&header_), sizeof(TreeHeader));
        header_.checksum = expected_crc;

        if (expected_crc != actual_crc) {
            LOGE("PersistentBPlusTree: header checksum mismatch! "
                 "expected=0x%08X actual=0x%08X", expected_crc, actual_crc);
        }
    }
}

void PersistentBPlusTree::checkpoint() {
    // Note: Caller must hold tree_mutex_ (insert calls this)
    header_.checksum = 0;
    header_.checksum = calculate_crc32(
        reinterpret_cast<const uint8_t*>(&header_), sizeof(TreeHeader));
    std::string encoded(reinterpret_cast<const char*>(&header_), sizeof(TreeHeader));

    if (wal_) {
        wal_->logPageWrite(0, encoded);
    }
    
    // Always write to mmap for immediate visibility on next boot
    mmap_->write(0, encoded);
    mmap_->sync(0, sizeof(TreeHeader));
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
        // Nodes are stored at 4096-byte aligned offsets starting after the header page
        offset = 4096 + (static_cast<uint64_t>(header_.node_count) * sizeof(BTreeNode));
        
        // Safety check: Don't grow node area into the data record area (starts at 1MB)
        if (offset + sizeof(BTreeNode) >= 1024 * 1024) {
            LOGE("PersistentBPlusTree: Out of index space! B-Tree region exceeded 1MB.");
            throw std::runtime_error("Database index area full (Max 1MB). Reduce data volume or increase index boundary.");
        }
        
        header_.node_count++;
    }

    BTreeNode node;
    std::memset(&node, 0, sizeof(BTreeNode));
    node.is_leaf = is_leaf;
    write_node(offset, node);

    return offset;
}

void PersistentBPlusTree::write_node(uint64_t offset, const BTreeNode& node) {
    cache_.put(offset, node);

    // WAL stores the already-encrypted (or plaintext if no crypto) node data.
    // No additional encryption here — the WAL simply mirrors what goes to mmap.
    if (wal_) {
        wal_->logPageWrite(offset, reinterpret_cast<const uint8_t*>(&node), sizeof(BTreeNode));
    }

    mmap_->write(offset, reinterpret_cast<const uint8_t*>(&node), sizeof(BTreeNode));
}

BTreeNode PersistentBPlusTree::read_node(uint64_t offset) {
    BTreeNode node;
    if (cache_.get(offset, node)) {
        return node; // LRU cache hit
    }

    const uint8_t* ptr = mmap_->get_address(offset);
    if (ptr) {
        std::memcpy(&node, ptr, sizeof(BTreeNode));
        cache_.put(offset, node);
    } else {
        std::memset(&node, 0, sizeof(BTreeNode));
    }
    return node;
}

void PersistentBPlusTree::insert(const std::string& key, size_t data_offset, bool shouldCheckpoint) {
    std::unique_lock lock(tree_mutex_);
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

bool PersistentBPlusTree::updateOffset(const std::string& key, size_t new_offset) {
    // Used by Compactor: update value (offset) for existing key without changing structure.
    std::unique_lock lock(tree_mutex_);
    uint64_t curr_off = header_.root_offset;

    while (true) {
        BTreeNode node = read_node(curr_off);
        uint32_t i = 0;
        while (i < node.num_keys && key > std::string(node.keys[i])) {
            i++;
        }
        if (i < node.num_keys && key == std::string(node.keys[i])) {
            node.values[i] = new_offset;
            write_node(curr_off, node);
            return true;
        }
        if (node.is_leaf) return false;
        curr_off = node.children[i];
    }
}

void PersistentBPlusTree::split_child(
    uint64_t parent_off, BTreeNode& parent,
    uint32_t child_idx, uint64_t child_off, BTreeNode& child)
{
    uint64_t sibling_off = allocate_node(child.is_leaf);
    BTreeNode sibling = read_node(sibling_off);

    uint32_t t = BTreeNode::MAX_KEYS / 2;
    sibling.num_keys = BTreeNode::MAX_KEYS - t - 1;

    for (uint32_t j = 0; j < sibling.num_keys; j++) {
        std::strncpy(sibling.keys[j], child.keys[j + t + 1], BTreeNode::KEY_SIZE - 1);
        sibling.keys[j][BTreeNode::KEY_SIZE - 1] = '\0';
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
        std::strncpy(parent.keys[j], parent.keys[j - 1], BTreeNode::KEY_SIZE - 1);
        parent.keys[j][BTreeNode::KEY_SIZE - 1] = '\0';
        parent.values[j] = parent.values[j - 1];
    }

    std::strncpy(parent.keys[child_idx], child.keys[t], BTreeNode::KEY_SIZE - 1);
    parent.keys[child_idx][BTreeNode::KEY_SIZE - 1] = '\0';
    parent.values[child_idx] = child.values[t];
    parent.num_keys++;

    write_node(parent_off, parent);
    write_node(child_off, child);
    write_node(sibling_off, sibling);
}

void PersistentBPlusTree::insert_non_full(
    uint64_t node_off, const std::string& key, size_t data_offset)
{
    BTreeNode node = read_node(node_off);

    if (node.is_leaf) {
        int i = static_cast<int>(node.num_keys) - 1;
        // Check if key already exists (upsert)
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
                std::strncpy(node.keys[i + 1], node.keys[i], BTreeNode::KEY_SIZE - 1);
                node.keys[i + 1][BTreeNode::KEY_SIZE - 1] = '\0';
                node.values[i + 1] = node.values[i];
                i--;
            }
            std::strncpy(node.keys[i + 1], key.c_str(), BTreeNode::KEY_SIZE - 1);
            node.keys[i + 1][BTreeNode::KEY_SIZE - 1] = '\0';
            node.values[i + 1] = data_offset;
            node.num_keys++;
        }
        write_node(node_off, node);
    } else {
        int i = static_cast<int>(node.num_keys) - 1;
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
    std::shared_lock lock(tree_mutex_);
    uint64_t curr_off = header_.root_offset;
    while (true) {
        BTreeNode node = read_node(curr_off);
        uint32_t i = 0;
        while (i < node.num_keys && key > std::string(node.keys[i])) {
            i++;
        }
        if (i < node.num_keys && key == std::string(node.keys[i])) {
            return static_cast<size_t>(node.values[i]);
        }
        if (node.is_leaf) return 0;
        curr_off = node.children[i];
    }
}

std::vector<std::pair<std::string, size_t>> PersistentBPlusTree::range(
    const std::string& start_key, const std::string& end_key)
{
    std::shared_lock lock(tree_mutex_);
    std::vector<std::pair<std::string, size_t>> results;

    if (header_.root_offset == 0) return results;

    std::function<void(uint64_t)> traverse = [&](uint64_t node_off) {
        if (node_off == 0) return;
        BTreeNode node = read_node(node_off);

        for (uint32_t i = 0; i < node.num_keys; i++) {
            std::string key(node.keys[i]);
            if (key >= start_key && key <= end_key) {
                results.emplace_back(key, static_cast<size_t>(node.values[i]));
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
    std::shared_lock lock(tree_mutex_);
    std::vector<std::string> keys;
    if (header_.root_offset == 0) return keys;

    std::function<void(uint64_t)> traverse = [&](uint64_t node_off) {
        if (node_off == 0) return;
        BTreeNode node = read_node(node_off);

        for (uint32_t i = 0; i <= node.num_keys; i++) {
            if (!node.is_leaf && i < node.num_keys + 1) {
                traverse(node.children[i]);
            }
            if (i < node.num_keys) {
                std::string key(node.keys[i]);
                if (!key.empty()) {
                    keys.push_back(key);
                }
            }
        }
    };

    traverse(header_.root_offset);
    return keys;
}

// ─────────────────────────────────────────────────────────────────────────────
// O(limit) paged in-order traversal using skip counter
// No full key load — stops as soon as 'limit' keys are collected
// ─────────────────────────────────────────────────────────────────────────────
void PersistentBPlusTree::traverseInOrder(
    uint64_t node_off,
    int& skip_remaining,
    int& collect_remaining,
    std::vector<std::string>& out)
{
    if (node_off == 0 || collect_remaining <= 0) return;

    BTreeNode node = read_node(node_off);

    for (uint32_t i = 0; i <= node.num_keys && collect_remaining > 0; i++) {
        // Visit left child first (in-order)
        if (!node.is_leaf && i < node.num_keys + 1) {
            traverseInOrder(node.children[i], skip_remaining, collect_remaining, out);
        }
        if (i < node.num_keys && collect_remaining > 0) {
            std::string key(node.keys[i]);
            if (!key.empty()) {
                if (skip_remaining > 0) {
                    skip_remaining--;
                } else {
                    out.push_back(key);
                    collect_remaining--;
                }
            }
        }
    }
}

std::vector<std::string> PersistentBPlusTree::getKeysPaged(int limit, int offset) {
    std::shared_lock lock(tree_mutex_);
    std::vector<std::string> result;
    if (header_.root_offset == 0 || limit <= 0) return result;

    int skip = offset;
    int collect = limit;
    result.reserve(static_cast<size_t>(limit));

    traverseInOrder(header_.root_offset, skip, collect, result);
    return result;
}

} // namespace turbo_db
