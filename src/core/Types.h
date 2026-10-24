#pragma once

// ---------------------------------------------------------------------------
// Common primitive aliases used across the whole project.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace kdl {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;

// Sentinel used to mark a segment whose end offset is unknown
// (server did not advertise a Content-Length / Content-Range).
inline constexpr u64 kUnknownSize = ~static_cast<u64>(0);

} // namespace kdl
