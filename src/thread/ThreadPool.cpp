#include "thread/ThreadPool.h"

#include <stdexcept>

#include "core/Logger.h"

namespace kdl {

ThreadPool::ThreadPool(std::size_t threadCount) {
    if (threadCount == 0) {
        throw std::invalid_argument("ThreadPool requires at least one thread");
    }
    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            return;
        }
        tasks_.push(std::move(task));
    }
    hasTask_.notify_one();
}

void ThreadPool::wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return tasks_.empty() && active_ == 0; });
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            return;
        }
        stop_ = true;
    }
    hasTask_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            hasTask_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

            if (tasks_.empty()) {
                if (stop_) {
                    return;
                }
                continue;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
            ++active_;
        }

        try {
            task();
        } catch (const std::exception& e) {
            KDL_LOG_ERROR("worker task threw: %s", e.what());
        } catch (...) {
            KDL_LOG_ERROR("worker task threw an unknown exception");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
        }
        idle_.notify_all();
    }
}

} // namespace kdl
