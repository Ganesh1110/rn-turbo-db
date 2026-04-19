#pragma once

#include <deque>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <thread>

namespace turbo_db {

struct WriteBatchEntry {
    std::string key;
    std::vector<uint8_t> data;
    bool is_encrypted;
};

class WriteBuffer {
public:
    explicit WriteBuffer(size_t max_buffer_size = 4096, size_t flush_interval_ms = 100);
    ~WriteBuffer();

    using FlushCallback = std::function<void(const std::vector<WriteBatchEntry>&)>;
    
    void setFlushCallback(FlushCallback callback);
    void add(const std::string& key, const uint8_t* data, size_t length, bool is_encrypted = false);
    void flush();
    void startAutoFlush();
    void stopAutoFlush();
    size_t size() const;
    bool empty() const;

private:
    void autoFlushWorker();

    std::deque<WriteBatchEntry> buffer_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> flushing_{false};
    std::thread flush_thread_;
    
    size_t max_buffer_size_;
    size_t flush_interval_ms_;
    FlushCallback flush_callback_;
};

}