#include "net/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <string>
#include <vector>
#include <utility>

#include "core/Error.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "url/Url.h"

#pragma comment(lib, "winhttp.lib")

namespace kdl {

namespace {

// RAII wrapper around a WinHTTP handle.
class WinHandle {
public:
    WinHandle() = default;
    explicit WinHandle(HINTERNET handle) : handle_(handle) {}
    ~WinHandle() { reset(); }

    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;

    WinHandle(WinHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    WinHandle& operator=(WinHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void reset(HINTERNET handle = nullptr) {
        if (handle_ && handle_ != handle) {
            ::WinHttpCloseHandle(handle_);
        }
        handle_ = handle;
    }

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

std::string lastErrorMessage(const char* context) {
    const DWORD code = ::GetLastError();
    wchar_t* buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::string detail;
    if (length > 0 && buffer) {
        detail = trim(toUtf8(std::wstring(buffer, length)));
    }
    if (buffer) {
        ::LocalFree(buffer);
    }

    char codeText[32];
    std::snprintf(codeText, sizeof(codeText), "0x%08lX", static_cast<unsigned long>(code));

    std::string message = context ? context : "WinHTTP error";
    message += " (Win32 error ";
    message += codeText;
    if (!detail.empty()) {
        message += ": ";
        message += detail;
    }
    message += ")";
    return message;
}

// Parse a raw "Content-Range: bytes 0-499/1234" value.
void parseContentRange(const std::string& value, HttpResponse& response) {
    // Expected: bytes <start>-<end>/<total>
    const std::string lowered = toLower(trim(value));
    if (!startsWith(lowered, "bytes")) {
        return;
    }
    const std::string spec = trim(lowered.substr(5));
    const auto slash = spec.find('/');
    if (slash == std::string::npos) {
        return;
    }
    const std::string rangePart = spec.substr(0, slash);
    const std::string totalPart = spec.substr(slash + 1);

    const auto dash = rangePart.find('-');
    if (dash == std::string::npos) {
        return;
    }

    u64 start = 0;
    u64 end = 0;
    if (parseU64(rangePart.substr(0, dash), start)) {
        response.rangeStart = start;
    }
    if (parseU64(rangePart.substr(dash + 1), end)) {
        response.rangeEnd = end;
    }
    if (totalPart != "*") {
        u64 total = 0;
        if (parseU64(totalPart, total)) {
            response.rangeTotal = total;
        }
    }
}

void parseRawHeaders(const std::wstring& raw, HttpResponse& response) {
    const std::string text = toUtf8(raw);
    for (const std::string& line : splitLines(text)) {
        const std::string trimmed = line.find('\r') != std::string::npos
                                        ? line.substr(0, line.find('\r'))
                                        : line;
        if (trimmed.empty()) {
            continue;
        }
        const auto colon = trimmed.find(':');
        if (colon == std::string::npos) {
            continue; // status line or malformed header
        }
        const std::string name = toLower(trim(trimmed.substr(0, colon)));
        const std::string value = trim(trimmed.substr(colon + 1));
        if (name.empty()) {
            continue;
        }
        response.headers.emplace(name, value);
    }
}

bool headerSaysBytes(const HttpResponse& response) {
    const std::string value = toLower(response.header("accept-ranges"));
    return value.find("bytes") != std::string::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct HttpClient::Impl {
    HINTERNET session = nullptr;
    std::string userAgent;

    // Connection handles are cached per host:port. WinHTTP keeps the underlying
    // TCP/TLS socket alive for as long as the connection handle lives, so
    // re-using the handle turns a series of small range requests into one
    // keep-alive connection instead of a fresh handshake per request.
    std::vector<std::pair<std::string, WinHandle>> connections;

    ~Impl() {
        if (session) {
            ::WinHttpCloseHandle(session);
        }
    }

    HINTERNET connectionFor(const Url& url) {
        const std::string key = url.host + ":" + std::to_string(url.effectivePort());
        for (auto& entry : connections) {
            if (entry.first == key) {
                return entry.second.get();
            }
        }

        WinHandle connection(::WinHttpConnect(session, toWide(url.host).c_str(),
                                              url.effectivePort(), 0));
        if (!connection) {
            throw Error(ErrorCode::Network, lastErrorMessage("WinHttpConnect failed"));
        }

        HINTERNET handle = connection.get();
        connections.emplace_back(key, std::move(connection));
        return handle;
    }
};

// ---------------------------------------------------------------------------
// HttpResponse helpers
// ---------------------------------------------------------------------------

std::string HttpResponse::header(const std::string& lowerName) const {
    const auto it = headers.find(toLower(lowerName));
    if (it == headers.end()) {
        return std::string();
    }
    return it->second;
}

// ---------------------------------------------------------------------------
// HttpClient
// ---------------------------------------------------------------------------

HttpClient::HttpClient(const std::string& proxy, const std::string& userAgent)
    : impl_(new Impl()) {
    impl_->userAgent = userAgent;

    const std::wstring wideAgent = toWide(userAgent);
    impl_->session = ::WinHttpOpen(
        wideAgent.c_str(),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!impl_->session) {
        throw Error(ErrorCode::Network, lastErrorMessage("WinHttpOpen failed"));
    }

    // Follow redirects (but never downgrade HTTPS to plain HTTP).
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    ::WinHttpSetOption(impl_->session, WINHTTP_OPTION_REDIRECT_POLICY,
                       &redirectPolicy, sizeof(redirectPolicy));

    if (!proxy.empty()) {
        const std::wstring wideProxy = toWide(proxy);
        WINHTTP_PROXY_INFO info{};
        info.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        info.lpszProxy = const_cast<wchar_t*>(wideProxy.c_str());
        info.lpszProxyBypass = nullptr;
        if (!::WinHttpSetOption(impl_->session, WINHTTP_OPTION_PROXY, &info, sizeof(info))) {
            throw Error(ErrorCode::Network, lastErrorMessage("setting proxy failed"));
        }
    }
}

HttpClient::~HttpClient() {
    delete impl_;
}

HttpResponse HttpClient::perform(const HttpRequest& request, const DataSink& sink) {
    const Url url = Url::parse(request.url);

    const HINTERNET connection = impl_->connectionFor(url);
    const std::wstring method = toWide(request.method);
    const std::wstring target = toWide(url.requestTarget());

    DWORD flags = 0;
    if (url.scheme == "https") {
        flags |= WINHTTP_FLAG_SECURE;
    }

    WinHandle req(::WinHttpOpenRequest(connection, method.c_str(), target.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req) {
        throw Error(ErrorCode::Network, lastErrorMessage("WinHttpOpenRequest failed"));
    }

    ::WinHttpSetTimeouts(req.get(), request.connectTimeoutMs, request.connectTimeoutMs,
                         request.receiveTimeoutMs, request.receiveTimeoutMs);

    // Assemble user supplied + range headers.
    std::wstring headerBlock;
    if (request.rangeStart.has_value()) {
        headerBlock += L"Range: bytes=" + std::to_wstring(*request.rangeStart) + L"-";
        if (request.rangeEnd.has_value()) {
            headerBlock += std::to_wstring(*request.rangeEnd);
        }
        headerBlock += L"\r\n";
    }
    for (const HttpHeader& header : request.headers) {
        headerBlock += toWide(header.name) + L": " + toWide(header.value) + L"\r\n";
    }
    if (!headerBlock.empty()) {
        if (!::WinHttpAddRequestHeaders(req.get(), headerBlock.c_str(),
                                        static_cast<DWORD>(-1),
                                        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            throw Error(ErrorCode::Network, lastErrorMessage("adding request headers failed"));
        }
    }

    if (!::WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        throw Error(ErrorCode::Network, lastErrorMessage("WinHttpSendRequest failed"));
    }

    if (!::WinHttpReceiveResponse(req.get(), nullptr)) {
        throw Error(ErrorCode::Network, lastErrorMessage("WinHttpReceiveResponse failed"));
    }

    HttpResponse response;

    // Status code.
    {
        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!::WinHttpQueryHeaders(req.get(),
                                   WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                   WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                   WINHTTP_NO_HEADER_INDEX)) {
            throw Error(ErrorCode::HttpProtocol, lastErrorMessage("reading status code failed"));
        }
        response.statusCode = static_cast<int>(status);
    }

    // Raw headers.
    {
        DWORD size = 0;
        ::WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                              WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                              WINHTTP_NO_HEADER_INDEX);
        if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 0) {
            std::wstring raw(size / sizeof(wchar_t), L'\0');
            if (::WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                      WINHTTP_HEADER_NAME_BY_INDEX, &raw[0], &size,
                                      WINHTTP_NO_HEADER_INDEX)) {
                parseRawHeaders(raw, response);
            }
        }
    }

    // Final URL (after redirects).
    {
        DWORD size = 0;
        ::WinHttpQueryOption(req.get(), WINHTTP_OPTION_URL, nullptr, &size);
        if (size > 0) {
            std::wstring value(size / sizeof(wchar_t), L'\0');
            if (::WinHttpQueryOption(req.get(), WINHTTP_OPTION_URL, &value[0], &size)) {
                response.finalUrl = toUtf8(value.c_str());
            }
        }
    }

    // Derived header values.
    if (const std::string value = response.header("content-length"); !value.empty()) {
        u64 parsed = 0;
        if (parseU64(value, parsed)) {
            response.contentLength = parsed;
        }
    }
    if (const std::string value = response.header("content-range"); !value.empty()) {
        parseContentRange(value, response);
    }
    response.acceptRanges = headerSaysBytes(response);
    response.etag = response.header("etag");
    response.lastModified = response.header("last-modified");
    response.contentType = response.header("content-type");
    response.contentDisposition = response.header("content-disposition");

    if (!sink) {
        return response;
    }

    // Stream the body.
    //
    // WinHttpQueryDataAvailable is deliberately *not* used here: calling it
    // before every read costs an extra blocking round-trip per chunk and
    // measurably lowers throughput. Instead we read straight into a large
    // buffer, bounded by the declared length when one is available.
    std::vector<char> buffer(256 * 1024);
    const bool bounded = response.contentLength.has_value();
    const u64 expected = response.contentLength.value_or(0);
    u64 received = 0;

    for (;;) {
        DWORD toRead = static_cast<DWORD>(buffer.size());
        if (bounded) {
            if (received >= expected) {
                break;
            }
            const u64 remaining = expected - received;
            if (remaining < toRead) {
                toRead = static_cast<DWORD>(remaining);
            }
        }

        DWORD read = 0;
        if (!::WinHttpReadData(req.get(), buffer.data(), toRead, &read)) {
            throw Error(ErrorCode::Network, lastErrorMessage("WinHttpReadData failed"));
        }
        if (read == 0) {
            break; // end of body / connection closed
        }

        received += read;
        if (!sink(buffer.data(), static_cast<std::size_t>(read))) {
            break; // consumer requested an abort
        }
    }

    return response;
}

} // namespace kdl
