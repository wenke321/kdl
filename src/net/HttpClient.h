#pragma once

// ---------------------------------------------------------------------------
// WinHTTP based HTTP/HTTPS client.
//
// One HttpClient instance owns one WinHTTP session and is NOT thread-safe:
// create one instance per worker thread. Redirects, proxies and TLS are
// handled by the WinHTTP stack.
// ---------------------------------------------------------------------------

#include <functional>
#include <string>

#include "core/Types.h"
#include "net/HttpTypes.h"

namespace kdl {

class HttpClient {
public:
    // A data sink receives chunks of the response body.
    // Returning false aborts the transfer (used for cancellation / probing).
    using DataSink = std::function<bool(const char* data, std::size_t size)>;

    explicit HttpClient(const std::string& proxy = std::string(),
                        const std::string& userAgent = "KevinDownloader/1.0");
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Performs the request synchronously. If `sink` is non-null it is invoked
    // for every received body chunk; if it is null the body is drained and
    // discarded. Throws kdl::Error on transport / protocol failures.
    HttpResponse perform(const HttpRequest& request, const DataSink& sink);

private:
    struct Impl;
    Impl* impl_; // hides <winhttp.h> from consumers
};

} // namespace kdl
