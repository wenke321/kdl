#include "core/Error.h"

namespace kdl {

const char* errorCodeName(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:           return "Ok";
        case ErrorCode::InvalidUrl:   return "InvalidUrl";
        case ErrorCode::Network:      return "Network";
        case ErrorCode::HttpProtocol: return "HttpProtocol";
        case ErrorCode::Io:           return "Io";
        case ErrorCode::Cancelled:    return "Cancelled";
        case ErrorCode::Timeout:      return "Timeout";
        case ErrorCode::Redirect:     return "Redirect";
        case ErrorCode::Unsupported:  return "Unsupported";
        case ErrorCode::Verify:       return "Verify";
        case ErrorCode::Internal:     return "Internal";
    }
    return "Unknown";
}

} // namespace kdl
