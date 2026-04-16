#pragma once

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <vector>

namespace secure_db {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 2);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<typename F>
    void enqueue(F&& task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            tasks_.push(std::forward<F>(task));
        }
        cv_.notify_one();
    }

    void wait_until_empty();
    void shutdown();

private:
    void worker_thread();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
    std::atomic<size_t> pending_tasks_;
};

}