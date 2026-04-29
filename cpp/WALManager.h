#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include "SecureCryptoContext.h"

namespace turbo_db {

class CorruptionException : public std::runtime_error {
public:
    enum class Type {
        CRC_MISMATCH,
        HEADER_CORRUPT,
        TREE_CORRUPT,
        OFFSET_OUT_OF_BOUNDS
    };

    explicit CorruptionException(Type type, const std::string& msg)
        : std::runtime_error(msg), type_(type) {}

    Type getType() const { return type_; }

private:
    Type type_;
};

enum class WALRecordType : uint8_t {
    PAGE_WRITE  = 1,
    COMMIT      = 2,
    CHECKPOINT  = 3,
    TX_BEGIN    = 4,  // NEW: explicit transaction begin marker
    BATCH_WRITE = 5  // NEW: batch multiple page writes in one record
};

#pragma pack(push, 1)
struct WALRecordHeader {
    uint32_t length;     // Total record length including header
    WALRecordType type;
    uint64_t offset;     // Target offset in main DB file
    uint32_t checksum;   // CRC32 of payload (0 for COMMIT/CHECKPOINT/TX_BEGIN)
};
#pragma pack(pop)

class WALManager {
public:
    WALManager(const std::string& db_path, SecureCryptoContext* crypto);
    ~WALManager();

    // Open WAL for writing, returns true on success
    bool openWAL();

    // Log an explicit transaction begin (optional but enables cleaner recovery)
    void logBegin();

    // Log a write operation to the WAL.
    // NOTE: data must be ALREADY encrypted if crypto is in use.
    // WALManager does NOT re-encrypt — it only stores what is passed.
    void logPageWrite(uint64_t offset, const std::string& data);
    void logPageWrite(uint64_t offset, const uint8_t* data, size_t length);

    // NEW: Batch multiple page writes into a single WAL record (reduces fsync)
    void logBatchWrite(const std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& writes);

    // Log a commit marker (flushes internally)
    void logCommit();

    // Checkpoint: mark WAL clean and truncate
    void checkpoint();

    // Force sync WAL to disk for crash safety (fdatasync)
    bool sync();

    /**
     * 2-Pass commit-aware recovery:
     *   Pass 1: scan WAL, collect committed transaction ranges.
     *   Pass 2: replay only PAGE_WRITE records inside committed ranges.
     * Uncommitted / partial transactions at the tail are silently discarded.
     */
    void recover(class MMapRegion* mmap);

    /**
     * Attempt recovery and return whether any data was replayed.
     */
    bool recoverSafe(class MMapRegion* mmap);

    // Get current WAL file path
    std::string getWALPath() const { return wal_path_; }

    // Archive WAL to <wal_path>.bak — used before repair, never deletes
    bool archiveWAL();

    // Clear WAL (truncate), used after successful checkpoint
    void clear();

    uint32_t calculate_crc32(const uint8_t* data, size_t length);

private:
    struct TxRange {
        std::streampos begin_pos; // file position of first PAGE_WRITE in this tx
        std::streampos commit_pos; // file position of COMMIT record
    };

    std::string wal_path_;
    std::ofstream wal_file_;
    SecureCryptoContext* crypto_; // kept for reference but NOT used for re-encryption
#ifdef __ANDROID__
    int wal_fd_ = -1;
#endif

    void appendRecord(const WALRecordHeader& header, const uint8_t* payload, size_t length);
};

} // namespace turbo_db
