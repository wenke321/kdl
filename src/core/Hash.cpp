#include "core/Hash.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <vector>

#include "core/Error.h"
#include "core/StringUtil.h"

#pragma comment(lib, "bcrypt.lib")

namespace kdl {

namespace {

// RAII wrappers around the CNG handles we use.
class AlgorithmHandle {
public:
    AlgorithmHandle() {
        if (::BCryptOpenAlgorithmProvider(&handle_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
            throw Error(ErrorCode::Internal, "BCryptOpenAlgorithmProvider(SHA256) failed");
        }
    }
    ~AlgorithmHandle() {
        if (handle_) {
            ::BCryptCloseAlgorithmProvider(handle_, 0);
        }
    }
    BCRYPT_ALG_HANDLE get() const { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
};

class HashHandle {
public:
    HashHandle(BCRYPT_ALG_HANDLE algorithm, std::vector<u8>& scratch) {
        if (::BCryptCreateHash(algorithm, &handle_, scratch.data(),
                               static_cast<ULONG>(scratch.size()), nullptr, 0, 0) < 0) {
            throw Error(ErrorCode::Internal, "BCryptCreateHash failed");
        }
    }
    ~HashHandle() {
        if (handle_) {
            ::BCryptDestroyHash(handle_);
        }
    }
    BCRYPT_HASH_HANDLE get() const { return handle_; }

private:
    BCRYPT_HASH_HANDLE handle_ = nullptr;
};

class FileHandle {
public:
    explicit FileHandle(HANDLE handle) : handle_(handle) {}
    ~FileHandle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
    }
    HANDLE get() const { return handle_; }

private:
    HANDLE handle_;
};

constexpr std::size_t kReadChunk = 1u << 20; // 1 MiB

} // namespace

std::string sha256File(const std::wstring& path) {
    // Share write access as well: the download's FileWriter may still hold the
    // file open (and must not be blocked by our reader).
    const FileHandle file(::CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        const DWORD code = ::GetLastError();
        throw Error(ErrorCode::Io, "cannot open file for hashing (Win32 error " +
                                       std::to_string(static_cast<unsigned long>(code)) + ")");
    }

    const AlgorithmHandle algorithm;

    DWORD objectSize = 0;
    DWORD returned = 0;
    if (::BCryptGetProperty(algorithm.get(), BCRYPT_OBJECT_LENGTH,
                            reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                            &returned, 0) < 0) {
        throw Error(ErrorCode::Internal, "BCryptGetProperty(OBJECT_LENGTH) failed");
    }

    std::vector<u8> scratch(objectSize);
    const HashHandle hash(algorithm.get(), scratch);

    std::vector<u8> buffer(kReadChunk);
    for (;;) {
        DWORD read = 0;
        if (!::ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                        nullptr)) {
            throw Error(ErrorCode::Io, "read failed while hashing");
        }
        if (read == 0) {
            break;
        }
        if (::BCryptHashData(hash.get(), buffer.data(), read, 0) < 0) {
            throw Error(ErrorCode::Internal, "BCryptHashData failed");
        }
    }

    std::array<u8, 32> digest{};
    if (::BCryptFinishHash(hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        throw Error(ErrorCode::Internal, "BCryptFinishHash failed");
    }

    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (const u8 byte : digest) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

std::string normalizeHex(const std::string& value) {
    std::string out = trim(value);
    for (char& c : out) {
        if (c >= 'A' && c <= 'F') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool isValidSha256Hex(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (const char c : value) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return false;
        }
    }
    return true;
}

} // namespace kdl
