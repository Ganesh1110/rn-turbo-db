#include "WriteBuffer.h"
#include <chrono>

namespace turbo_db {

WriteBuffer::WriteBuffer(size_t max_buffer_size, size_t flush_interval_ms)
    : max_buffer_size_(max_buffer_size), flush_interval_ms_(flush_interval_ms) {}

WriteBuffer::~WriteBuffer() {
    stopAutoFlush();
    flush();
}

void WriteBuffer::setFlushCallback(FlushCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_callback_ = std::move(callback);
}

void WriteBuffer::add(const std::string& key, const uint8_t* data, size_t length, bool is_encrypted) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (buffer_.size() >= max_buffer_size_) {
        lock.unlock();
        flush();
        lock.lock();
    }
    
    WriteBatchEntry entry;
    entry.key = key;
    entry.data.assign(data, data + length);
    entry.is_encrypted = is_encrypted;
    buffer_.push_back(std::move(entry));
    
    if (buffer_.size() >= max_buffer_size_ / 2) {
        cv_.notify_one();
    }
}

void WriteBuffer::flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (buffer_.empty() || flushing_.exchange(true)) {
        return;
    }
    
    std::deque<WriteBatchEntry> entries_to_flush;
    entries_to_flush.swap(buffer_);
    
    lock.unlock();
    
    if (flush_callback_) {
        flush_callback_(std::vector<WriteBatchEntry>(entries_to_flush.begin(), entries_to_flush.end()));
    }
    
    flushing_ = false;
    lock.lock();
    cv_.notify_one();
}

void WriteBuffer::startAutoFlush() {
    stop_ = false;
    flush_thread_ = std::thread(&WriteBuffer::autoFlushWorker, this);
}

void WriteBuffer::stopAutoFlush() {
    stop_ = true;
    cv_.notify_one();
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
}

size_t WriteBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

bool WriteBuffer::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.empty();
}

void WriteBuffer::autoFlushWorker() {
    while (!stop_) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (cv_.wait_for(lock, std::chrono::milliseconds(flush_interval_ms_), 
            [this] { return stop_ || buffer_.size() >= max_buffer_size_ / 2; })) {
            if (stop_) break;
        }
        
        if (!buffer_.empty()) {
            std::deque<WriteBatchEntry> entries_to_flush;
            entries_to_flush.swap(buffer_);
            lock.unlock();
            
            if (flush_callback_) {
                flush_callback_(std::vector<WriteBatchEntry>(entries_to_flush.begin(), entries_to_flush.end()));
            }
        }
    }
}

}