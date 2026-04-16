#include "ThreadPool.h"

namespace secure_db {

ThreadPool::ThreadPool(size_t num_threads) : stop_(false), pending_tasks_(0) {
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void ThreadPool::wait_until_empty() {
    while (pending_tasks_.load() > 0) {
        std::this_thread::yield();
    }
}

void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            
            if (stop_ && tasks_.empty()) {
                return;
            }
            
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
                ++pending_tasks_;
            } else {
                continue;
            }
        }
        
        if (task) {
            task();
            --pending_tasks_;
        }
    }
}

}