#include "WALManager.h"
#include "MMapRegion.h"
#include <iostream>
#include <filesystem>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace secure_db {

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

void WALManager::logPageWrite(uint64_t offset, const std::string& data) {
    logPageWrite(offset, reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void WALManager::logPageWrite(uint64_t offset, const uint8_t* data, size_t length) {
    const uint8_t* payload_ptr = data;
    size_t payload_len = length;
    std::vector<uint8_t> encrypted;

    if (crypto_) {
        encrypted = crypto_->encrypt(data, length);
        payload_ptr = encrypted.data();
        payload_len = encrypted.size();
    }

    WALRecordHeader header;
    header.length = sizeof(WALRecordHeader) + payload_len;
    header.type = WALRecordType::PAGE_WRITE;
    header.offset = offset;
    header.checksum = calculate_crc32(payload_ptr, payload_len);

    appendRecord(header, payload_ptr, payload_len);
}

void WALManager::logCommit() {
    WALRecordHeader header;
    header.length = sizeof(WALRecordHeader);
    header.type = WALRecordType::COMMIT;
    header.offset = 0;
    header.checksum = 0;

    appendRecord(header, nullptr, 0);
    wal_file_.flush();
}

void WALManager::checkpoint() {
    // In a full implementation, this would:
    // 1. Flush all main DB pages (msync).
    // 2. Truncate WAL.
    wal_file_.flush();
    clear();
}

bool WALManager::sync() {
    if (!wal_file_.is_open()) return false;

    wal_file_.flush();

#ifdef __ANDROID__
    if (wal_fd_ >= 0) {
        fsync(wal_fd_);
    }
#else
    // iOS/macOS: Get file descriptor and fsync
    int fd = ::open(wal_path_.c_str(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        ::close(fd);
    }
#endif
    return true;
}

void WALManager::clear() {
    if (wal_file_.is_open()) {
        wal_file_.close();
    }
    std::remove(wal_path_.c_str());
    wal_file_.open(wal_path_, std::ios::binary | std::ios::app);
}

void WALManager::recover(MMapRegion* mmap) {
    std::ifstream reader(wal_path_, std::ios::binary);
    if (!reader.is_open()) return;

    std::cout << "Starting WAL Recovery for " << wal_path_ << "...\n";

    // 1st Pass: Identify committed transaction ranges
    // For this prototype, we'll replay everything valid, but usually we'd only replay committed blocks.
    
    while (reader.peek() != EOF) {
        WALRecordHeader header;
        reader.read(reinterpret_cast<char*>(&header), sizeof(WALRecordHeader));
        if (reader.gcount() < sizeof(WALRecordHeader)) break;

        if (header.type == WALRecordType::PAGE_WRITE) {
            if (header.length < sizeof(WALRecordHeader)) {
                std::cerr << "WAL Recovery: Invalid record length. Skipping.\n";
                continue;
            }
            size_t payload_len = header.length - sizeof(WALRecordHeader);
            std::vector<char> payload_buffer(payload_len);
            reader.read(payload_buffer.data(), payload_len);
            if (reader.gcount() != static_cast<std::streamsize>(payload_len)) {
                std::cerr << "WAL Recovery: Read " << reader.gcount() << " bytes, expected " << payload_len << "\n";
                continue;
            }

            uint32_t current_crc = calculate_crc32(reinterpret_cast<const uint8_t*>(payload_buffer.data()), payload_len);
            if (current_crc == header.checksum) {
                std::string decrypted = payload_buffer.data();
                if (crypto_) {
                    std::vector<uint8_t> dec_bytes = crypto_->decrypt(
                        reinterpret_cast<const uint8_t*>(payload_buffer.data()), payload_len);
                    decrypted.assign(reinterpret_cast<const char*>(dec_bytes.data()), dec_bytes.size());
                }

                std::cout << "Replaying write at offset " << header.offset << " (" << decrypted.size() << " bytes)\n";
                mmap->write(header.offset, decrypted);
            } else {
                std::cerr << "WAL Recovery: Checksum mismatch at offset " << header.offset << ". Skipping corrupted log.\n";
            }
        } else if (header.type == WALRecordType::COMMIT) {
            std::cout << "Transaction COMMIT found in WAL.\n";
        }
    }
    
    reader.close();
    mmap->sync(); // Ensure replayed data is flushed
    checkpoint();  // Clear WAL after recovery
}

}
