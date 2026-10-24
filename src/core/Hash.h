#pragma once

// ---------------------------------------------------------------------------
// Cryptographic hashing helpers built on Windows CNG (bcrypt).
//
// Used to verify a finished download against a known digest, which is the
// only way to be sure that a multi-connection transfer assembled correctly.
// ---------------------------------------------------------------------------

#include <string>

#include "core/Types.h"

namespace kdl {

// Streams the file at `path` through SHA-256 and returns the digest as 64
// lower-case hex characters. Throws kdl::Error(Io) if the file is unreadable.
std::string sha256File(const std::wstring& path);

// True when `value` looks like a SHA-256 digest (exactly 64 hex characters).
bool isValidSha256Hex(const std::string& value);

// Trims surrounding whitespace and lower-cases the digest.
std::string normalizeHex(const std::string& value);

} // namespace kdl
