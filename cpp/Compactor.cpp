#include "Compactor.h"
#include "MMapRegion.h"
#include "PersistentBPlusTree.h"
#include "SecureCryptoContext.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TurboDB_Compact", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TurboDB_Compact", __VA_ARGS__)
#else
#define LOGI(...) do {} while(0)
#define LOGE(...) do {} while(0)
#endif

namespace turbo_db {

Compactor::Compactor(DBEngine* engine, MMapRegion* mmap, BufferedBTree* btree, std::string db_path, SecureCryptoContext* crypto)
    : engine_(engine), mmap_(mmap), btree_(btree), db_path_(std::move(db_path)), crypto_(crypto) {}

double Compactor::getFragmentationRatio(size_t live_bytes, size_t total_file_bytes) const {
    if (total_file_bytes == 0) return 0.0;
    double dead = static_cast<double>(total_file_bytes) - static_cast<double>(live_bytes);
    return dead / static_cast<double>(total_file_bytes);
}

bool Compactor::shouldCompact(size_t live_bytes, size_t total_file_bytes) const {
    return getFragmentationRatio(live_bytes, total_file_bytes) > FRAGMENTATION_THRESHOLD;
}

void Compactor::trackLiveBytes(size_t delta, bool add) {
    if (add) {
        live_bytes_.fetch_add(delta, std::memory_order_relaxed);
    } else {
        size_t cur = live_bytes_.load(std::memory_order_relaxed);
        if (cur >= delta) {
            live_bytes_.fetch_sub(delta, std::memory_order_relaxed);
        } else {
            live_bytes_.store(0, std::memory_order_relaxed);
        }
    }
}

void Compactor::runCompaction(
    MMapRegion* mmap,
    PersistentBPlusTree* tree,
    std::function<void(bool success, size_t bytes_saved)> onDone
) {
    if (!mmap || !tree) {
        if (onDone) onDone(false, 0);
        return;
    }

    const std::string tmp_path = db_path_ + ".compact.tmp";
    const size_t old_size = mmap->getSize();

    LOGI("Compactor: starting, file=%s size=%zu", db_path_.c_str(), old_size);

    try {
        // 1. Collect all live (key → offset) pairs from the index
        //    tree->getAllKeysWithOffsets() traverses B+ Tree in sorted order
        auto all_keys = tree->getAllKeys();

        // 2. Write a new compact mmap file
        //    Start well past the tree header region (same 1MB offset as original init)
        const size_t data_start_offset = 1024 * 1024;
        size_t write_cursor = data_start_offset;

        // New file: same size initially, compaction shrinks it conceptually
        // We write to a temp file and rename atomically
        {
            std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                LOGE("Compactor: failed to open temp file %s", tmp_path.c_str());
                if (onDone) onDone(false, 0);
                return;
            }

            // Pad header region
            std::vector<char> pad(data_start_offset, 0);
            out.write(pad.data(), data_start_offset);

            for (const auto& key : all_keys) {
                size_t old_offset = tree->find(key);
                if (old_offset == 0) continue; // deleted / tombstoned

                // Read record from mmap
                const uint8_t* len_ptr = mmap->get_address(old_offset);
                if (!len_ptr) continue;

                uint32_t payload_len;
                std::memcpy(&payload_len, len_ptr, sizeof(uint32_t));

                if (old_offset + sizeof(uint32_t) + payload_len > mmap->getSize()) {
                    LOGE("Compactor: record out of bounds for key=%s, skipping", key.c_str());
                    continue; // corrupted record — skip
                }

                const uint8_t* payload_ptr = mmap->get_address(old_offset + sizeof(uint32_t));
                if (!payload_ptr) continue;

                // Re-read optional CRC (4 bytes after payload)
                uint32_t crc32 = 0;
                const uint8_t* crc_ptr = mmap->get_address(old_offset + sizeof(uint32_t) + payload_len);
                if (crc_ptr) {
                    std::memcpy(&crc32, crc_ptr, sizeof(uint32_t));
                }

                // Write record to compact file: [LEN][PAYLOAD][CRC]
                out.write(reinterpret_cast<const char*>(&payload_len), sizeof(uint32_t));
                out.write(reinterpret_cast<const char*>(payload_ptr), payload_len);
                out.write(reinterpret_cast<const char*>(&crc32), sizeof(uint32_t));

                // Update tree index to new offset
                tree->updateOffset(key, write_cursor);

                write_cursor += sizeof(uint32_t) + payload_len + sizeof(uint32_t);
            }

            out.flush();
            out.close();
        }

        // 3. Atomic swap: rename tmp → original DB path
        //    Back up old file first
        const std::string backup_path = db_path_ + ".before_compact.bak";
        std::rename(db_path_.c_str(), backup_path.c_str());
        std::rename(tmp_path.c_str(), db_path_.c_str());

        // 4. Remove backup (compaction succeeded)
        std::remove(backup_path.c_str());

        size_t bytes_saved = (write_cursor < old_size) ? (old_size - write_cursor) : 0;
        live_bytes_.store(write_cursor, std::memory_order_relaxed);

        LOGI("Compactor: done, saved %zu bytes (%.1f%%)",
             bytes_saved,
             old_size > 0 ? (100.0 * bytes_saved / old_size) : 0.0);

        if (onDone) onDone(true, bytes_saved);

    } catch (const std::exception& e) {
        LOGE("Compactor: exception: %s", e.what());
        // Clean up temp file on failure
        std::remove(tmp_path.c_str());
        if (onDone) onDone(false, 0);
    }
}

} // namespace turbo_db
