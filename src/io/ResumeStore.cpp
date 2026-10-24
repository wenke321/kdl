#include "io/ResumeStore.h"

#include <windows.h>

#include <sstream>
#include <string>

#include "core/Error.h"
#include "core/StringUtil.h"

namespace kdl {

namespace {

constexpr const char* kMagic = "KDLRESUME 1";

std::string encodeU64(u64 value) {
    if (value == kUnknownSize) {
        return "-1";
    }
    return std::to_string(value);
}

// Reads a whole file into a std::string. Returns false if it cannot be read.
bool readFile(const std::wstring& path, std::string& out) {
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(handle, &size) || size.QuadPart < 0) {
        ::CloseHandle(handle);
        return false;
    }

    out.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t total = 0;
    while (total < out.size()) {
        DWORD read = 0;
        const DWORD chunk = static_cast<DWORD>(out.size() - total);
        if (!::ReadFile(handle, &out[total], chunk, &read, nullptr) || read == 0) {
            break;
        }
        total += read;
    }
    ::CloseHandle(handle);
    out.resize(total);
    return true;
}

// Writes `content` to `path` atomically (write temp + MoveFileEx replace).
void writeFileAtomic(const std::wstring& path, const std::string& content) {
    const std::wstring tempPath = path + L".tmp";

    HANDLE handle = ::CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw Error(ErrorCode::Io, "cannot create resume temp file: " +
                                       std::to_string(::GetLastError()));
    }

    const char* cursor = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        DWORD written = 0;
        const DWORD chunk =
            static_cast<DWORD>(remaining > 0x40000000ull ? 0x40000000ull : remaining);
        if (!::WriteFile(handle, cursor, chunk, &written, nullptr)) {
            ::CloseHandle(handle);
            ::DeleteFileW(tempPath.c_str());
            throw Error(ErrorCode::Io, "cannot write resume temp file: " +
                                           std::to_string(::GetLastError()));
        }
        cursor += written;
        remaining -= written;
    }
    ::FlushFileBuffers(handle);
    ::CloseHandle(handle);

    if (!::MoveFileExW(tempPath.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tempPath.c_str());
        throw Error(ErrorCode::Io, "cannot commit resume file: " +
                                       std::to_string(::GetLastError()));
    }
}

} // namespace

ResumeStore::ResumeStore(std::wstring path) : path_(std::move(path)) {}

bool ResumeStore::load(ResumeData& out) const {
    out = ResumeData{};

    std::string content;
    if (!readFile(path_, content) || content.empty()) {
        return false;
    }

    const std::vector<std::string> lines = splitLines(content);
    if (lines.empty() || trim(lines[0]) != kMagic) {
        return false;
    }

    std::vector<ResumeSegment> segments;
    bool sawSize = false;

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string line = trim(lines[i]);
        if (line.empty()) {
            continue;
        }

        const auto equals = line.find('=');
        if (equals != std::string::npos) {
            const std::string key = trim(line.substr(0, equals));
            const std::string value = line.substr(equals + 1);

            if (key == "url") {
                out.url = value;
            } else if (key == "etag") {
                out.etag = value;
            } else if (key == "last_modified") {
                out.lastModified = value;
            } else if (key == "size") {
                u64 parsed = 0;
                if (value == "-1") {
                    out.totalSize = kUnknownSize;
                    sawSize = true;
                } else if (parseU64(value, parsed)) {
                    out.totalSize = parsed;
                    sawSize = true;
                }
            } else if (key == "count") {
                // Informational; the segment lines drive the parse.
            }
            continue;
        }

        // Segment line: "<start> <end> <downloaded>".
        std::istringstream stream(line);
        unsigned long long start = 0;
        unsigned long long end = 0;
        unsigned long long downloaded = 0;
        if (!(stream >> start >> end >> downloaded)) {
            return false; // corrupt
        }
        ResumeSegment segment;
        segment.start = static_cast<u64>(start);
        segment.end = static_cast<u64>(end);
        segment.downloaded = static_cast<u64>(downloaded);
        segments.push_back(segment);
    }

    if (!sawSize || segments.empty()) {
        return false;
    }

    out.segments = std::move(segments);
    return true;
}

void ResumeStore::save(const ResumeData& data) const {
    std::ostringstream out;
    out << kMagic << "\n";
    out << "url=" << data.url << "\n";
    out << "etag=" << data.etag << "\n";
    out << "last_modified=" << data.lastModified << "\n";
    out << "size=" << encodeU64(data.totalSize) << "\n";
    out << "count=" << data.segments.size() << "\n";
    for (const ResumeSegment& segment : data.segments) {
        out << segment.start << ' ' << segment.end << ' ' << segment.downloaded << "\n";
    }
    writeFileAtomic(path_, out.str());
}

void ResumeStore::remove() const noexcept {
    ::DeleteFileW(path_.c_str());
}

} // namespace kdl
