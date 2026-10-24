#pragma once

// ---------------------------------------------------------------------------
// Minimal but robust HTTP/HTTPS URL parser.
//
// Supports:
//   http://host/path?query#frag
//   https://host:8443/path
//   http://user:pass@host/path
//   http://[::1]:8080/path        (IPv6 literals)
// ---------------------------------------------------------------------------

#include <string>

#include "core/Types.h"

namespace kdl {

struct Url {
    std::string scheme;   // "http" or "https" (lower-case)
    std::string host;     // host name or IPv6 literal without brackets
    u16         port = 0; // 0 -> default for the scheme
    std::string path;     // always begins with '/'
    std::string query;    // without the leading '?'
    std::string username;
    std::string password;

    // Throws kdl::Error(InvalidUrl) on malformed input.
    static Url parse(const std::string& input);

    // Port to actually connect to (defaults applied).
    u16 effectivePort() const;

    // Request target for an HTTP request line: "/path?query".
    std::string requestTarget() const;

    // Value for the Host header, including a non-default port.
    std::string hostHeader() const;

    // Best-effort file name derived from the path ("/a/b/c.zip" -> "c.zip").
    // Returns an empty string when no usable name can be extracted.
    std::string filenameFromPath() const;

    // Full re-serialized URL (useful for logging).
    std::string toString() const;
};

} // namespace kdl
