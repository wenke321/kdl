#pragma once

// ---------------------------------------------------------------------------
// A byte range of the target file together with its download progress.
//
// Each segment is written by exactly one worker at a time. `downloaded` is a
// plain atomic counter so that a reporter thread may read it concurrently for
// the progress bar and for periodic resume snapshots without a data race.
// ---------------------------------------------------------------------------

#include <atomic>
#include <string>

#include "core/Types.h"

namespace kdl {

enum class SegmentState {
    Pending,
    Downloading,
    Completed,
    Failed,
};

struct Segment {
    int index = 0;
    int source = 0;                 // index into Downloader::sources_
    u64 start = 0;
    u64 end = 0;                    // inclusive; kUnknownSize => "until EOF"
    std::atomic<u64> downloaded{0}; // bytes committed by this segment so far
    SegmentState state = SegmentState::Pending;
    std::string error;              // populated when state == Failed

    // Total number of bytes this segment is responsible for.
    u64 expectedLength() const {
        if (end == kUnknownSize) {
            return kUnknownSize;
        }
        return end - start + 1;
    }

    u64 progress() const {
        return downloaded.load(std::memory_order_relaxed);
    }

    bool isComplete() const {
        if (end == kUnknownSize) {
            return false; // completion is detected by EOF, not by length
        }
        return progress() >= expectedLength();
    }
};

} // namespace kdl
