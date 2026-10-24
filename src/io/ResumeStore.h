#pragma once

// ---------------------------------------------------------------------------
// Persistence of in-progress download state (".kdlpart" sidecar file).
//
// Format (UTF-8, line oriented):
//
//   KDLRESUME 1
//   url=<final url>
//   etag=<etag or empty>
//   last_modified=<value or empty>
//   size=<total bytes or -1 if unknown>
//   count=<segment count>
//   <start> <end> <downloaded>          (one line per segment)
//
// Writes go through a temporary file that is atomically renamed into place so
// a crash mid-save can never corrupt the previous state.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "core/Types.h"

namespace kdl {

struct ResumeSegment {
    u64 start = 0;
    u64 end = 0;          // inclusive
    u64 downloaded = 0;
};

struct ResumeData {
    std::string url;
    std::string etag;
    std::string lastModified;
    u64 totalSize = kUnknownSize;
    std::vector<ResumeSegment> segments;
};

class ResumeStore {
public:
    explicit ResumeStore(std::wstring path);

    // Loads state from disk. Returns false if the file is missing or corrupt.
    bool load(ResumeData& out) const;

    // Atomically writes `data` to disk. Throws kdl::Error(Io) on failure.
    void save(const ResumeData& data) const;

    // Deletes the sidecar file if present (never throws).
    void remove() const noexcept;

    const std::wstring& path() const noexcept { return path_; }

private:
    std::wstring path_;
};

} // namespace kdl
