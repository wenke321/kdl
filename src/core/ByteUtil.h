#pragma once

// ---------------------------------------------------------------------------
// Human-readable formatting helpers for byte counts and durations.
// ---------------------------------------------------------------------------

#include <string>

#include "core/Types.h"

namespace kdl {

// 1536 -> "1.50 KiB"
std::string formatBytes(u64 bytes);

// 12.34 -> "12.3s", 3725 -> "1h 02m 05s"
std::string formatDuration(double seconds);

// Bytes/second -> "1.50 MiB/s"
std::string formatRate(double bytesPerSecond);

} // namespace kdl
