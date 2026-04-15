#include "WALManager.h"
#include "MMapRegion.h"
#include <iostream>
#include <filesystem>
#include <cstring>

namespace secure_db {

WALManager::WALManager(const std::string& db_path, SecureCryptoContext* crypto) 
    : wal_path_(db_path + ".wal"), crypto_(crypto) {
    wal_file_.open(wal_path_, std::ios::binary | std::ios::app);
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

void WALManager::appendRecord(const WALRecordHeader& header, const std::string& payload) {
    if (!wal_file_.is_open()) return;
    
    wal_file_.write(reinterpret_cast<const char*>(&header), sizeof(WALRecordHeader));
    if (!payload.empty()) {
        wal_file_.write(payload.data(), payload.size());
    }
}

void WALManager::logPageWrite(uint64_t offset, const std::string& data) {
    std::string payload = data;
    if (crypto_) {
        std::vector<uint8_t> encrypted = crypto_->encrypt(
            reinterpret_cast<const uint8_t*>(data.data()), data.size());
        payload.assign(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    }

    WALRecordHeader header;
    header.length = sizeof(WALRecordHeader) + payload.size();
    header.type = WALRecordType::PAGE_WRITE;
    header.offset = offset;
    header.checksum = calculate_crc32(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    appendRecord(header, payload);
}

void WALManager::logCommit() {
    WALRecordHeader header;
    header.length = sizeof(WALRecordHeader);
    header.type = WALRecordType::COMMIT;
    header.offset = 0;
    header.checksum = 0;

    appendRecord(header, "");
    wal_file_.flush();
}

void WALManager::checkpoint() {
    // In a full implementation, this would:
    // 1. Flush all main DB pages (msync).
    // 2. Truncate WAL.
    wal_file_.flush();
    clear();
}

void WALManager::clear() {
    if (wal_file_.is_open()) {
        wal_file_.close();
    }
    std::filesystem::remove(wal_path_);
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
            std::string payload(header.length - sizeof(WALRecordHeader), '\0');
            reader.read(payload.data(), payload.size());
            
            uint32_t current_crc = calculate_crc32(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
            if (current_crc == header.checksum) {
                std::string decrypted = payload;
                if (crypto_) {
                    std::vector<uint8_t> dec_bytes = crypto_->decrypt(
                        reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
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
