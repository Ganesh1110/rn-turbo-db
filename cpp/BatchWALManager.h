#pragma once

#include "WALManager.h"
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <functional>

namespace turbo_db {

class BatchWALManager {
public:
    BatchWALManager(WALManager* wal_manager, size_t max_batch_size = 64, size_t flush_interval_ms = 10);
    ~BatchWALManager();

    void logPageWrite(uint64_t offset, const uint8_t* data, size_t length);
    void logCommit();
    void flush();
    void startAutoFlush();
    void stopAutoFlush();
    size_t pendingCount() const;
    bool empty() const;

private:
    struct PendingWrite {
        uint64_t offset;
        std::vector<uint8_t> data;
    };

    void autoFlushWorker();
    void flushInternal();

    WALManager* wal_manager_;
    std::deque<PendingWrite> pending_writes_;
    std::atomic<bool> flushing_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread flush_thread_;
    
    size_t max_batch_size_;
    size_t flush_interval_ms_;
};

}