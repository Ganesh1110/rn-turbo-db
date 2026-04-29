#include "WALManager.h"
#include "MMapRegion.h"
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <stdexcept>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TurboDB_WAL", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TurboDB_WAL", __VA_ARGS__)
#else
#define LOGI(...) do {} while(0)
#define LOGE(...) do {} while(0)
#endif

namespace turbo_db {

WALManager::WALManager(const std::string& db_path, SecureCryptoContext* crypto)
    : wal_path_(db_path + ".wal"), crypto_(crypto)
#ifdef __ANDROID__
    , wal_fd_(-1)
#endif
{
    openWAL();
}

bool WALManager::openWAL() {
    wal_file_.open(wal_path_, std::ios::binary | std::ios::app | std::ios::out);
    if (!wal_file_.is_open()) return false;

#ifdef __ANDROID__
    wal_fd_ = ::open(wal_path_.c_str(), O_WRONLY | O_APPEND);
#endif
    return true;
}

WALManager::~WALManager() {
    if (wal_file_.is_open()) {
        wal_file_.close();
    }
#ifdef __ANDROID__
    if (wal_fd_ >= 0) {
        ::close(wal_fd_);
        wal_fd_ = -1;
    }
#endif
}

uint32_t WALManager::calculate_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

void WALManager::appendRecord(const WALRecordHeader& header, const uint8_t* payload, size_t length) {
    if (!wal_file_.is_open()) return;
    wal_file_.write(reinterpret_cast<const char*>(&header), sizeof(WALRecordHeader));
    if (payload && length > 0) {
        wal_file_.write(reinterpret_cast<const char*>(payload), length);
    }
}

void WALManager::logBegin() {
    WALRecordHeader header;
    header.length   = sizeof(WALRecordHeader);
    header.type     = WALRecordType::TX_BEGIN;
    header.offset   = 0;
    header.checksum = 0;
    appendRecord(header, nullptr, 0);
}

void WALManager::logPageWrite(uint64_t offset, const std::string& data) {
    logPageWrite(offset, reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void WALManager::logPageWrite(uint64_t offset, const uint8_t* data, size_t length) {
    // ⚠️ IMPORTANT: data is already encrypted by the caller (insertRecInternal /
    // setMulti). WALManager intentionally does NOT re-encrypt — that was the
    // double-encryption bug. We store exactly what we receive.
    WALRecordHeader header;
    header.length   = static_cast<uint32_t>(sizeof(WALRecordHeader) + length);
    header.type     = WALRecordType::PAGE_WRITE;
    header.offset   = offset;
    header.checksum = calculate_crc32(data, length);
    appendRecord(header, data, length);
}

// ── Batch Write: Multiple page writes in one WAL record ─────────────────
void WALManager::logBatchWrite(const std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& writes) {
    if (writes.empty() || !wal_file_.is_open()) return;

    // Calculate total payload size:
    //   uint32_t num_ops + for each: uint64_t offset + uint32_t len + payload
    size_t payload_size = sizeof(uint32_t); // num_ops
    for (const auto& [offset, data] : writes) {
        payload_size += sizeof(uint64_t) + sizeof(uint32_t) + data.size();
    }

    // Build payload
    std::vector<uint8_t> payload(payload_size);
    size_t pos = 0;

    // Write number of operations
    uint32_t num_ops = static_cast<uint32_t>(writes.size());
    std::memcpy(payload.data() + pos, &num_ops, sizeof(uint32_t));
    pos += sizeof(uint32_t);

    // Write each operation
    for (const auto& [offset, data] : writes) {
        std::memcpy(payload.data() + pos, &offset, sizeof(uint64_t));
        pos += sizeof(uint64_t);
        uint32_t len = static_cast<uint32_t>(data.size());
        std::memcpy(payload.data() + pos, &len, sizeof(uint32_t));
        pos += sizeof(uint32_t);
        std::memcpy(payload.data() + pos, data.data(), data.size());
        pos += data.size();
    }

    // Write WAL record
    WALRecordHeader header;
    header.length   = static_cast<uint32_t>(sizeof(WALRecordHeader) + payload_size);
    header.type     = WALRecordType::BATCH_WRITE;
    header.offset   = 0; // Not used for batch
    header.checksum = calculate_crc32(payload.data(), payload_size);
    appendRecord(header, payload.data(), payload_size);
}

void WALManager::logCommit() {
    WALRecordHeader header;
    header.length   = sizeof(WALRecordHeader);
    header.type     = WALRecordType::COMMIT;
    header.offset   = 0;
    header.checksum = 0;
    appendRecord(header, nullptr, 0);
    wal_file_.flush();
}

void WALManager::checkpoint() {
    wal_file_.flush();
    clear();
}

bool WALManager::sync() {
    if (!wal_file_.is_open()) return false;
    wal_file_.flush();

#ifdef __ANDROID__
    if (wal_fd_ >= 0) {
        fdatasync(wal_fd_);
    }
#else
    // iOS/macOS: open separately for fsync (std::ofstream doesn't expose fd)
    int fd = ::open(wal_path_.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);

    }
#endif
    return true;
}

bool WALManager::archiveWAL() {
    if (wal_file_.is_open()) {
        wal_file_.close();
    }
    std::string bak = wal_path_ + ".bak";
    // Remove old backup if it exists
    std::remove(bak.c_str());
    int rc = std::rename(wal_path_.c_str(), bak.c_str());
    LOGI("WAL archived to %s (rc=%d)", bak.c_str(), rc);
    return rc == 0;
}

void WALManager::clear() {
    if (wal_file_.is_open()) {
        wal_file_.close();
    }
    std::remove(wal_path_.c_str());
    wal_file_.open(wal_path_, std::ios::binary | std::ios::app);
#ifdef __ANDROID__
    if (wal_fd_ >= 0) ::close(wal_fd_);
    wal_fd_ = ::open(wal_path_.c_str(), O_WRONLY | O_APPEND);
#endif
}

// ---------------------------------------------------------------------------
// 2-Pass Commit-Aware Recovery
// ---------------------------------------------------------------------------

void WALManager::recover(MMapRegion* mmap) {
    recoverSafe(mmap);
}

bool WALManager::recoverSafe(MMapRegion* mmap) {
    std::ifstream reader(wal_path_, std::ios::binary);
    if (!reader.is_open()) {
        LOGI("WAL: no WAL file found at %s, nothing to recover", wal_path_.c_str());
        return false;
    }

    LOGI("WAL Recovery: starting 2-pass recovery for %s", wal_path_.c_str());

    // -------------------------------------------------------------------------
    // Pass 1: Identify committed transaction boundaries
    // -------------------------------------------------------------------------
    // A committed transaction is: [TX_BEGIN?...][PAGE_WRITE*][COMMIT]
    // We collect the file position ranges of all such committed groups.
    // -------------------------------------------------------------------------
    std::vector<TxRange> committed_ranges;
    std::streampos tx_start = 0;
    bool in_tx = false;
    size_t total_records = 0;
    size_t committed_txs = 0;

    reader.seekg(0, std::ios::beg);

    while (reader.peek() != std::char_traits<char>::eof()) {
        std::streampos record_start = reader.tellg();

        WALRecordHeader hdr;
        reader.read(reinterpret_cast<char*>(&hdr), sizeof(WALRecordHeader));
        if (reader.gcount() < static_cast<std::streamsize>(sizeof(WALRecordHeader))) break;

        total_records++;

        if (hdr.type == WALRecordType::TX_BEGIN) {
            tx_start = record_start;
            in_tx = true;
            // No payload for TX_BEGIN
        } else if (hdr.type == WALRecordType::PAGE_WRITE || hdr.type == WALRecordType::BATCH_WRITE) {
            if (!in_tx) {
                // Implicit transaction start (legacy records without TX_BEGIN)
                tx_start = record_start;
                in_tx = true;
            }
            size_t payload_len = 0;
            if (hdr.length > sizeof(WALRecordHeader)) {
                payload_len = hdr.length - sizeof(WALRecordHeader);
            }
            reader.seekg(static_cast<std::streamoff>(payload_len), std::ios::cur);
            if (!reader.good()) break;
        } else if (hdr.type == WALRecordType::COMMIT) {
            if (in_tx) {
                committed_ranges.push_back({tx_start, record_start});
                committed_txs++;
            }
            in_tx = false;
        } else if (hdr.type == WALRecordType::CHECKPOINT) {
            // Checkpoint means everything before here was already applied
            committed_ranges.clear();
            in_tx = false;
        } else {
            // Unknown record type — WAL is corrupt from here; stop
            LOGE("WAL Recovery: unknown record type 0x%02x at pos, stopping pass 1",
                 static_cast<uint8_t>(hdr.type));
            break;
        }
    }

    if (in_tx) {
        LOGI("WAL Recovery: last transaction was NOT committed (tail crash), discarding");
    }

    LOGI("WAL Recovery: pass 1 done — %zu records, %zu committed transactions",
         total_records, committed_txs);

    if (committed_ranges.empty()) {
        LOGI("WAL Recovery: no committed transactions found, nothing to replay");
        reader.close();
        return false;
    }

    // -------------------------------------------------------------------------
    // Pass 2: Replay only PAGE_WRITE records inside committed ranges
    // -------------------------------------------------------------------------
    size_t replayed = 0;
    size_t skipped_crc = 0;

    for (const auto& range : committed_ranges) {
        reader.clear();
        reader.seekg(range.begin_pos, std::ios::beg);

        while (reader.tellg() < range.commit_pos && reader.good()) {
            WALRecordHeader hdr;
            reader.read(reinterpret_cast<char*>(&hdr), sizeof(WALRecordHeader));
            if (reader.gcount() < static_cast<std::streamsize>(sizeof(WALRecordHeader))) break;

            if (hdr.type == WALRecordType::TX_BEGIN) {
                // No payload, skip
                continue;
            }

            if (hdr.type == WALRecordType::PAGE_WRITE) {
                // ... existing single page write handling ...
                size_t payload_len = 0;
                if (hdr.length > sizeof(WALRecordHeader)) {
                    payload_len = hdr.length - sizeof(WALRecordHeader);
                }

                if (payload_len == 0) continue;

                std::vector<uint8_t> payload(payload_len);
                reader.read(reinterpret_cast<char*>(payload.data()), payload_len);
                if (static_cast<size_t>(reader.gcount()) != payload_len) {
                    LOGE("WAL Recovery: short read on PAGE_WRITE payload, skipping");
                    break;
                }

                // CRC validation
                uint32_t computed = calculate_crc32(payload.data(), payload_len);
                if (computed != hdr.checksum) {
                    LOGE("WAL Recovery: CRC mismatch at offset %llu — skipping",
                         static_cast<unsigned long long>(hdr.offset));
                    skipped_crc++;
                    continue;
                }

                if (hdr.offset + payload_len <= mmap->getSize()) {
                    mmap->write(hdr.offset, payload.data(), payload_len);
                    replayed++;
                }
            } else if (hdr.type == WALRecordType::BATCH_WRITE) {
                // Replay batched writes
                size_t payload_len = 0;
                if (hdr.length > sizeof(WALRecordHeader)) {
                    payload_len = hdr.length - sizeof(WALRecordHeader);
                }

                if (payload_len == 0) continue;

                std::vector<uint8_t> payload(payload_len);
                reader.read(reinterpret_cast<char*>(payload.data()), payload_len);
                if (static_cast<size_t>(reader.gcount()) != payload_len) {
                    LOGE("WAL Recovery: short read on BATCH_WRITE payload, skipping");
                    break;
                }

                // CRC validation
                uint32_t computed = calculate_crc32(payload.data(), payload_len);
                if (computed != hdr.checksum) {
                    LOGE("WAL Recovery: CRC mismatch on BATCH_WRITE — skipping");
                    skipped_crc++;
                    continue;
                }

                // Parse batch: num_ops, then for each: offset (uint64) + len (uint32) + data
                size_t pos = 0;
                uint32_t num_ops = 0;
                if (payload_len >= sizeof(uint32_t)) {
                    std::memcpy(&num_ops, payload.data() + pos, sizeof(uint32_t));
                    pos += sizeof(uint32_t);
                }

                for (uint32_t i = 0; i < num_ops; i++) {
                    if (pos + sizeof(uint64_t) + sizeof(uint32_t) > payload_len) break;

                    uint64_t offset;
                    std::memcpy(&offset, payload.data() + pos, sizeof(uint64_t));
                    pos += sizeof(uint64_t);

                    uint32_t len;
                    std::memcpy(&len, payload.data() + pos, sizeof(uint32_t));
                    pos += sizeof(uint32_t);

                    if (pos + len > payload_len) break;

                    if (offset + len <= mmap->getSize()) {
                        mmap->write(offset, payload.data() + pos, len);
                        replayed++;
                    }
                    pos += len;
                }
            }
        }
    }

    reader.close();
    mmap->sync(); // Flush all replayed pages to disk

    LOGI("WAL Recovery: complete — %zu records replayed, %zu CRC failures",
         replayed, skipped_crc);

    // Checkpoint (clear WAL) after successful recovery
    checkpoint();

    return replayed > 0;
}

} // namespace turbo_db
