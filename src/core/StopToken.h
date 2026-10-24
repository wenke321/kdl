#pragma once

// ---------------------------------------------------------------------------
// A tiny, copyable, thread-safe cancellation token.
//
// The token can be copied freely; all copies share the same internal state.
// Workers poll stopRequested() and abort their work cooperatively.
// ---------------------------------------------------------------------------

#include <atomic>
#include <memory>

namespace kdl {

class StopToken {
public:
    StopToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    void requestStop() const noexcept {
        flag_->store(true, std::memory_order_relaxed);
    }

    bool stopRequested() const noexcept {
        return flag_->load(std::memory_order_relaxed);
    }

    void reset() const noexcept {
        flag_->store(false, std::memory_order_relaxed);
    }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

} // namespace kdl
