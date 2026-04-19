#include "BatchWALManager.h"
#include <chrono>

namespace turbo_db {

BatchWALManager::BatchWALManager(WALManager* wal_manager, size_t max_batch_size, size_t flush_interval_ms)
    : wal_manager_(wal_manager), max_batch_size_(max_batch_size), flush_interval_ms_(flush_interval_ms) {}

BatchWALManager::~BatchWALManager() {
    stopAutoFlush();
    flush();
}

void BatchWALManager::logPageWrite(uint64_t offset, const uint8_t* data, size_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (pending_writes_.size() >= max_batch_size_) {
        flushInternal();
    }
    
    PendingWrite write;
    write.offset = offset;
    write.data.assign(data, data + length);
    pending_writes_.push_back(std::move(write));
    
    if (pending_writes_.size() >= max_batch_size_ / 2) {
        cv_.notify_one();
    }
}

void BatchWALManager::logCommit() {
    flush();
    if (wal_manager_) {
        wal_manager_->logCommit();
    }
}

void BatchWALManager::flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (pending_writes_.empty() || flushing_.exchange(true)) {
        return;
    }
    
    auto writes_to_flush = std::move(pending_writes_);
    pending_writes_.clear();
    
    lock.unlock();
    
    for (const auto& write : writes_to_flush) {
        if (wal_manager_) {
            wal_manager_->logPageWrite(write.offset, write.data.data(), write.data.size());
        }
    }
    
    flushing_ = false;
    lock.lock();
    cv_.notify_one();
}

void BatchWALManager::startAutoFlush() {
    stop_ = false;
    flush_thread_ = std::thread(&BatchWALManager::autoFlushWorker, this);
}

void BatchWALManager::stopAutoFlush() {
    stop_ = true;
    cv_.notify_one();
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
}

size_t BatchWALManager::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_writes_.size();
}

bool BatchWALManager::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_writes_.empty();
}

void BatchWALManager::autoFlushWorker() {
    while (!stop_) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        cv_.wait_for(lock, std::chrono::milliseconds(flush_interval_ms_), 
            [this] { return stop_ || pending_writes_.size() >= max_batch_size_ / 2; });
        
        if (stop_) break;
        
        if (!pending_writes_.empty()) {
            auto writes_to_flush = std::move(pending_writes_);
            lock.unlock();
            
            for (const auto& write : writes_to_flush) {
                if (wal_manager_) {
                    wal_manager_->logPageWrite(write.offset, write.data.data(), write.data.size());
                }
            }
        }
    }
}

void BatchWALManager::flushInternal() {
    if (pending_writes_.empty() || flushing_.exchange(true)) {
        return;
    }
    
    auto writes_to_flush = std::move(pending_writes_);
    pending_writes_.clear();
    
    for (const auto& write : writes_to_flush) {
        if (wal_manager_) {
            wal_manager_->logPageWrite(write.offset, write.data.data(), write.data.size());
        }
    }
    
    flushing_ = false;
}

}