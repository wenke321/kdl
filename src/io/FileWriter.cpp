#include "io/FileWriter.h"

#include <windows.h>

#include <string>
#include <vector>

#include "core/Error.h"
#include "core/StringUtil.h"

namespace kdl {

namespace {

std::string winErrorText(const char* context, DWORD code) {
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

    std::string message = context ? context : "file error";
    if (!detail.empty()) {
        message += ": ";
        message += detail;
    }
    return message;
}

} // namespace

FileWriter::~FileWriter() {
    if (handle_) {
        ::CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

void FileWriter::open(const std::wstring& path, u64 preallocateSize, bool keepExisting) {
    if (handle_) {
        ::CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }

    const DWORD disposition = keepExisting ? OPEN_ALWAYS : CREATE_ALWAYS;
    HANDLE handle = ::CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        disposition,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        throw Error(ErrorCode::Io,
                    winErrorText("cannot open output file", ::GetLastError()));
    }

    handle_ = handle;

    if (preallocateSize != kUnknownSize) {
        LARGE_INTEGER distance{};
        distance.QuadPart = static_cast<LONGLONG>(preallocateSize);
        if (!::SetFilePointerEx(handle, distance, nullptr, FILE_BEGIN) ||
            !::SetEndOfFile(handle)) {
            const DWORD code = ::GetLastError();
            ::CloseHandle(handle);
            handle_ = nullptr;
            throw Error(ErrorCode::Io,
                        winErrorText("cannot size output file", code));
        }
        size_ = preallocateSize;
    } else {
        LARGE_INTEGER current{};
        current.QuadPart = 0;
        ::SetFilePointerEx(handle, current, nullptr, FILE_END);
        LARGE_INTEGER position{};
        ::SetFilePointerEx(handle, current, &position, FILE_CURRENT);
        size_ = static_cast<u64>(position.QuadPart);
    }
}

void FileWriter::writeAt(u64 offset, const char* data, std::size_t size) {
    if (size == 0) {
        return;
    }
    if (!handle_) {
        throw Error(ErrorCode::Io, "write attempted on a closed FileWriter");
    }

    std::lock_guard<std::mutex> lock(mutex_);

    LARGE_INTEGER distance{};
    distance.QuadPart = static_cast<LONGLONG>(offset);
    if (!::SetFilePointerEx(static_cast<HANDLE>(handle_), distance, nullptr, FILE_BEGIN)) {
        throw Error(ErrorCode::Io,
                    winErrorText("seek failed", ::GetLastError()));
    }

    const char* cursor = data;
    std::size_t remaining = size;
    while (remaining > 0) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(
            remaining > 0x40000000ull ? 0x40000000ull : remaining);
        if (!::WriteFile(static_cast<HANDLE>(handle_), cursor, chunk, &written, nullptr)) {
            throw Error(ErrorCode::Io,
                        winErrorText("write failed", ::GetLastError()));
        }
        if (written == 0) {
            throw Error(ErrorCode::Io, "write failed: zero bytes written");
        }
        cursor += written;
        remaining -= written;
    }

    if (offset + size > size_) {
        size_ = offset + size;
    }
}

void FileWriter::resize(u64 size) {
    if (!handle_) {
        throw Error(ErrorCode::Io, "resize attempted on a closed FileWriter");
    }
    std::lock_guard<std::mutex> lock(mutex_);

    LARGE_INTEGER distance{};
    distance.QuadPart = static_cast<LONGLONG>(size);
    if (!::SetFilePointerEx(static_cast<HANDLE>(handle_), distance, nullptr, FILE_BEGIN) ||
        !::SetEndOfFile(static_cast<HANDLE>(handle_))) {
        throw Error(ErrorCode::Io,
                    winErrorText("resize failed", ::GetLastError()));
    }
    size_ = size;
}

void FileWriter::flush() {
    if (!handle_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ::FlushFileBuffers(static_cast<HANDLE>(handle_));
}

} // namespace kdl
