#pragma once

// ---------------------------------------------------------------------------
// Unified error handling.
//
// The project uses exceptions as its single error channel for unrecoverable
// conditions, but every exception carries a machine-readable ErrorCode so that
// callers can react to specific failures (e.g. an invalid range means the
// resource changed and the download must be restarted).
// ---------------------------------------------------------------------------

#include <stdexcept>
#include <string>

#include "core/Types.h"

namespace kdl {

enum class ErrorCode {
    Ok = 0,
    InvalidUrl,      // malformed URL
    Network,         // socket / WinHTTP transport failure
    HttpProtocol,    // unexpected HTTP status / malformed response
    Io,              // file system failure
    Cancelled,       // aborted by the user
    Timeout,         // operation timed out
    Redirect,        // too many redirects
    Unsupported,     // server / feature not supported
    Verify,          // post-download integrity check failed
    Internal,        // programming error
};

class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

const char* errorCodeName(ErrorCode code) noexcept;

} // namespace kdl
