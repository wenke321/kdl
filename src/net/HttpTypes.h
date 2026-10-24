#pragma once

// ---------------------------------------------------------------------------
// Transport-agnostic HTTP request / response value types.
//
// These are intentionally free of WinHTTP types so the rest of the code base
// never has to include <winhttp.h>.
// ---------------------------------------------------------------------------

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.h"

namespace kdl {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::vector<HttpHeader> headers;

    // When set, a "Range: bytes=<start>-[<end>]" header is emitted.
    std::optional<u64> rangeStart;
    std::optional<u64> rangeEnd;

    int connectTimeoutMs = 15000;
    int receiveTimeoutMs = 30000;
};

struct HttpResponse {
    int statusCode = 0;

    // Header names are stored lower-case; duplicate names are preserved.
    std::multimap<std::string, std::string> headers;

    std::string finalUrl;               // URL after redirects

    std::optional<u64> contentLength;   // Content-Length (0 allowed)
    bool acceptRanges = false;          // "Accept-Ranges: bytes"

    // Parsed Content-Range: "bytes <start>-<end>/<total>".
    std::optional<u64> rangeStart;
    std::optional<u64> rangeEnd;
    std::optional<u64> rangeTotal;

    std::string etag;
    std::string lastModified;
    std::string contentType;
    std::string contentDisposition;

    // First value of a header, or an empty string if absent.
    std::string header(const std::string& lowerName) const;

    bool isSuccess() const { return statusCode >= 200 && statusCode < 300; }
    bool isPartialContent() const { return statusCode == 206; }
};

} // namespace kdl
