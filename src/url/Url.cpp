#include "url/Url.h"

#include "core/Error.h"
#include "core/StringUtil.h"

namespace kdl {

namespace {

bool isValidPort(const std::string& s, u16& out) {
    u64 value = 0;
    if (!parseU64(s, value) || value == 0 || value > 65535) {
        return false;
    }
    out = static_cast<u16>(value);
    return true;
}

} // namespace

Url Url::parse(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        throw Error(ErrorCode::InvalidUrl, "URL is empty");
    }

    Url url;
    std::string rest = trimmed;

    // Scheme.
    const auto schemePos = rest.find("://");
    if (schemePos != std::string::npos) {
        url.scheme = toLower(rest.substr(0, schemePos));
        rest = rest.substr(schemePos + 3);
    } else {
        url.scheme = "http";
    }

    if (url.scheme != "http" && url.scheme != "https") {
        throw Error(ErrorCode::InvalidUrl, "unsupported scheme: " + url.scheme);
    }

    // Strip the fragment.
    if (const auto hash = rest.find('#'); hash != std::string::npos) {
        rest = rest.substr(0, hash);
    }

    // Split the authority from the path/query.
    std::string authority;
    std::string tail;
    const auto slash = rest.find_first_of("/?");
    if (slash == std::string::npos) {
        authority = rest;
        tail = "/";
    } else {
        authority = rest.substr(0, slash);
        tail = rest.substr(slash);
    }

    // userinfo@host
    if (const auto at = authority.rfind('@'); at != std::string::npos) {
        const std::string userinfo = authority.substr(0, at);
        authority = authority.substr(at + 1);

        const auto colon = userinfo.find(':');
        if (colon == std::string::npos) {
            url.username = urlDecode(userinfo);
        } else {
            url.username = urlDecode(userinfo.substr(0, colon));
            url.password = urlDecode(userinfo.substr(colon + 1));
        }
    }

    // host[:port], including IPv6 literals.
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            throw Error(ErrorCode::InvalidUrl, "unterminated IPv6 literal");
        }
        url.host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                throw Error(ErrorCode::InvalidUrl, "malformed authority");
            }
            if (!isValidPort(authority.substr(close + 2), url.port)) {
                throw Error(ErrorCode::InvalidUrl, "invalid port in authority");
            }
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string::npos) {
            url.host = authority.substr(0, colon);
            if (!isValidPort(authority.substr(colon + 1), url.port)) {
                throw Error(ErrorCode::InvalidUrl, "invalid port in authority");
            }
        } else {
            url.host = authority;
        }
    }

    if (url.host.empty()) {
        throw Error(ErrorCode::InvalidUrl, "missing host");
    }

    // path and query.
    const auto question = tail.find('?');
    if (question == std::string::npos) {
        url.path = tail;
    } else {
        url.path = tail.substr(0, question);
        url.query = tail.substr(question + 1);
    }
    if (url.path.empty()) {
        url.path = "/";
    }

    if (url.port == 0) {
        url.port = (url.scheme == "https") ? 443 : 80;
    }

    return url;
}

u16 Url::effectivePort() const {
    if (port != 0) {
        return port;
    }
    return (scheme == "https") ? 443 : 80;
}

std::string Url::requestTarget() const {
    std::string target = path.empty() ? "/" : path;
    if (!query.empty()) {
        target += "?";
        target += query;
    }
    return target;
}

std::string Url::hostHeader() const {
    const u16 resolvedPort = effectivePort();
    const bool isDefault = (scheme == "https" && resolvedPort == 443) ||
                           (scheme == "http" && resolvedPort == 80);
    if (isDefault) {
        return host;
    }
    return host + ":" + std::to_string(resolvedPort);
}

std::string Url::filenameFromPath() const {
    std::string pathOnly = path;
    if (const auto q = pathOnly.find('?'); q != std::string::npos) {
        pathOnly = pathOnly.substr(0, q);
    }
    if (const auto slash = pathOnly.find_last_of('/'); slash != std::string::npos) {
        pathOnly = pathOnly.substr(slash + 1);
    }
    return urlDecode(pathOnly);
}

std::string Url::toString() const {
    std::string out = scheme + "://";
    if (!username.empty()) {
        out += username;
        if (!password.empty()) {
            out += ":" + password;
        }
        out += "@";
    }
    out += hostHeader();
    out += requestTarget();
    return out;
}

} // namespace kdl
