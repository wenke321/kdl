#pragma once

// ---------------------------------------------------------------------------
// A resizable counting semaphore ("how many workers may be inside the section
// right now"). Used to give the downloader an adjustable degree of parallelism
// while keeping a fixed-size thread pool: workers block here until a slot is
// free, and the controller raises or lowers the limit at runtime.
//
// Not a general purpose utility - it is deliberately tiny and only promises
// what the downloader needs: acquire/release with a mutable limit.
// ---------------------------------------------------------------------------

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace kdl {

class ConcurrencyLimiter {
public:
    explicit ConcurrencyLimiter(std::size_t limit) : limit_(limit == 0 ? 1 : limit) {}

    ConcurrencyLimiter(const ConcurrencyLimiter&) = delete;
    ConcurrencyLimiter& operator=(const ConcurrencyLimiter&) = delete;

    // Blocks until a slot is available, then occupies it.
    void acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return active_ < limit_; });
        ++active_;
    }

    // Frees the slot held by the calling thread.
    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ > 0) {
                --active_;
            }
        }
        cv_.notify_one();
    }

    // Changes the limit; waking every waiter keeps the response immediate.
    void setLimit(std::size_t limit) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            limit_ = limit == 0 ? 1 : limit;
        }
        cv_.notify_all();
    }

    std::size_t limit() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return limit_;
    }

    std::size_t active() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t limit_;
    std::size_t active_ = 0;
};

} // namespace kdl
