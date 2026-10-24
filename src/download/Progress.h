#pragma once

// ---------------------------------------------------------------------------
// Shared, lock-free download progress counters.
//
// `downloaded` is updated by every worker with relaxed atomic adds; the
// reporter thread reads it periodically to render a progress line.
// ---------------------------------------------------------------------------

#include <atomic>

#include "core/Types.h"

namespace kdl {

struct ProgressState {
    std::atomic<u64> downloaded{0};
    std::atomic<u64> total{0};       // kUnknownSize when not known up-front
    std::atomic<bool> finished{false};

    void reset(u64 totalBytes) {
        downloaded.store(0, std::memory_order_relaxed);
        total.store(totalBytes, std::memory_order_relaxed);
        finished.store(false, std::memory_order_relaxed);
    }
};

} // namespace kdl
