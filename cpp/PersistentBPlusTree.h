#pragma once
#include "MMapRegion.h"
#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <shared_mutex>

namespace turbo_db {

uint32_t calculate_crc32(const uint8_t* data, size_t length);

// ─────────────────────────────────────────────────────────────────────────────
// Format versioning: bump TREE_FORMAT_VERSION whenever BTreeNode layout changes.
// initStorage() detects a version mismatch and rebuilds the database.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint32_t TREE_FORMAT_VERSION = 3; // Bumped due to node alignment & record alignment changes

#pragma pack(push, 1)
struct TreeHeader {
    static constexpr uint64_t MAGIC = 0x42504C5402; // 'BPLT' + version byte
    uint64_t magic;
    uint64_t root_offset;
    uint64_t free_list_head;
    uint64_t next_free_offset;
    uint32_t height;
    uint32_t node_count;
    uint32_t format_version; // NEW: format version for migration detection
    uint32_t checksum;       // CRC32 of header (with checksum field zeroed)
};
#pragma pack(pop)

struct BTreeNodeConfig {
    uint32_t max_keys = 32;
    uint32_t key_size = 256; // Was 64 — now 255 chars + null terminator

    BTreeNodeConfig() = default;
    BTreeNodeConfig(uint32_t mk, uint32_t ks) : max_keys(mk), key_size(ks) {}
};

struct BTreeNode {
    bool is_leaf;
    uint32_t num_keys;
    static constexpr size_t MAX_KEYS = 32;
    static constexpr size_t KEY_SIZE = 256; // ← WAS 64, NOW 256
    // Allow MAX_KEYS + 1 to handle temporary state during splits
    char keys[MAX_KEYS + 1][KEY_SIZE];
    uint64_t values[MAX_KEYS + 1];
    uint64_t children[MAX_KEYS + 2];
};

// ─────────────────────────────────────────────────────────────────────────────
// Proper LRU Node Cache
// Uses a doubly-linked list (std::list) for O(1) recency tracking and an
// unordered_map for O(1) lookup. Max capacity is configurable.
// Thread-safe via an internal mutex.
// ─────────────────────────────────────────────────────────────────────────────
class LRUNodeCache {
public:
    static constexpr size_t DEFAULT_CAPACITY = 512; // ~2.5 MB for 256-byte keys

    explicit LRUNodeCache(size_t capacity = DEFAULT_CAPACITY)
        : capacity_(capacity > 0 ? capacity : 1) {}

    bool get(uint64_t offset, BTreeNode& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(offset);
        if (it == map_.end()) return false;
        // Move accessed entry to front (most recently used)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        out = it->second->node;
        return true;
    }

    void put(uint64_t offset, const BTreeNode& node) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(offset);
        if (it != map_.end()) {
            // Update in-place and move to front
            it->second->node = node;
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
            return;
        }
        // Evict LRU entry if at capacity
        if (lru_list_.size() >= capacity_) {
            auto lru = std::prev(lru_list_.end());
            map_.erase(lru->key);
            lru_list_.erase(lru);
        }
        lru_list_.push_front({offset, node});
        map_[offset] = lru_list_.begin();
    }

    void remove(uint64_t offset) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(offset);
        if (it != map_.end()) {
            lru_list_.erase(it->second);
            map_.erase(it);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        lru_list_.clear();
        map_.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lru_list_.size();
    }

private:
    struct Entry {
        uint64_t key;
        BTreeNode node;
    };

    size_t capacity_;
    std::list<Entry> lru_list_;
    std::unordered_map<uint64_t, std::list<Entry>::iterator> map_;
    mutable std::mutex mutex_;
};

// ─────────────────────────────────────────────────────────────────────────────
// PersistentBPlusTree
// ─────────────────────────────────────────────────────────────────────────────
class PersistentBPlusTree {
public:
    PersistentBPlusTree(MMapRegion* mmap, class WALManager* wal = nullptr);

    void init();
    void initWithConfig(const BTreeNodeConfig& config);

    /**
     * Returns true if the on-disk format version differs from TREE_FORMAT_VERSION.
     * Caller (DBEngine) should rebuild the index if this returns true.
     */
    bool needsMigration() const;

    void insert(const std::string& key, size_t data_offset, bool shouldCheckpoint = true);

    size_t find(const std::string& key);

    /**
     * In-place update of the offset for an existing key without changing sort order.
     * Used by Compactor to update offsets after compaction.
     */
    bool updateOffset(const std::string& key, size_t new_offset);

    std::vector<std::pair<std::string, size_t>> range(
        const std::string& start_key, const std::string& end_key);

    std::vector<std::string> getAllKeys();

    /**
     * Tree-level paged enumeration — O(limit) not O(N).
     * Returns 'limit' keys starting at the 'offset'-th key in sorted order.
     */
    std::vector<std::string> getKeysPaged(int limit, int offset);

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

    // Query depth for monitoring
    int getTreeDepth() const {
        std::shared_lock lock(tree_mutex_);
        return static_cast<int>(header_.height);
    }

    void clear();

private:
    MMapRegion* mmap_;
    class WALManager* wal_;
    TreeHeader header_;
    BTreeNodeConfig config_;
    LRUNodeCache cache_; // ← replaces BTreeNodeCache
    mutable std::shared_mutex tree_mutex_;
    bool needs_migration_ = false;

    uint64_t allocate_node(bool is_leaf);
    void write_node(uint64_t offset, const BTreeNode& node);
    BTreeNode read_node(uint64_t offset);

    void split_child(uint64_t parent_off, BTreeNode& parent,
                     uint32_t child_idx, uint64_t child_off, BTreeNode& child);
    void insert_non_full(uint64_t node_off, const std::string& key, size_t data_offset);

    // Internal in-order traversal helper for paged enumeration
    void traverseInOrder(uint64_t node_off,
                         int& skip_remaining, int& collect_remaining,
                         std::vector<std::string>& out);
};

} // namespace turbo_db
