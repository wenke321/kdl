#pragma once

// ---------------------------------------------------------------------------
// Minimal fixed-size thread pool.
//
// Tasks are plain std::function<void()>. wait() blocks until every submitted
// task has finished executing, which is exactly what the downloader needs:
// submit one task per segment, then wait for all of them.
// ---------------------------------------------------------------------------

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace kdl {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threadCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueues a task. Ignored if the pool is shutting down.
    void submit(std::function<void()> task);

    // Blocks until the queue is empty and no worker is executing a task.
    void wait();

    std::size_t threadCount() const noexcept { return workers_.size(); }

private:
    void workerLoop();
    void shutdown();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable hasTask_;
    std::condition_variable idle_;
    std::size_t active_ = 0;
    bool stop_ = false;
};

} // namespace kdl
