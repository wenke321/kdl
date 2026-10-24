#pragma once

// ---------------------------------------------------------------------------
// Thread-safe random access file writer.
//
// The target file is opened once and pre-sized to the total download length;
// worker threads then commit their chunks at explicit offsets. All writes are
// serialized through an internal mutex so a single shared handle can safely be
// used from many threads.
// ---------------------------------------------------------------------------

#include <mutex>
#include <string>

#include "core/Types.h"

namespace kdl {

class FileWriter {
public:
    FileWriter() = default;
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    // Opens (creating if necessary) the file at `path`.
    //   * preallocateSize != kUnknownSize -> the file is resized to that length.
    //   * keepExisting == true -> an existing file is preserved (resume mode).
    // Throws kdl::Error(Io) on failure.
    void open(const std::wstring& path, u64 preallocateSize, bool keepExisting);

    // Writes `size` bytes at absolute `offset`. Throws kdl::Error(Io) on failure.
    void writeAt(u64 offset, const char* data, std::size_t size);

    // Truncates/sizes the file to exactly `size` bytes (used for unknowns).
    void resize(u64 size);

    void flush();

    bool isOpen() const noexcept { return handle_ != nullptr; }
    u64 size() const noexcept { return size_; }

private:
    void* handle_ = nullptr; // HANDLE
    u64 size_ = 0;
    std::mutex mutex_;
};

} // namespace kdl
