#pragma once

// ---------------------------------------------------------------------------
// High level download orchestrator.
//
// run() performs the whole lifecycle:
//   1. probe the resource (size / range support / validators),
//   2. resolve the output path,
//   3. load (or discard) previous resume state,
//   4. split the file into segments and download them in parallel,
//   5. persist progress so an interrupted run can resume,
//   6. finalize / clean up.
// ---------------------------------------------------------------------------

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/StopToken.h"
#include "core/Types.h"
#include "download/Progress.h"
#include "download/Segment.h"
#include "io/FileWriter.h"
#include "io/ResumeStore.h"
#include "net/HttpClient.h"
#include "net/HttpTypes.h"

namespace kdl {

struct DownloadConfig {
    std::string url;
    std::string outputPath;                 // empty -> derived from headers / URL
    // Ceiling for the number of parallel connections *per source*. With the
    // adaptive controller (on by default) a run starts with a handful of
    // connections and ramps up only while the extra connections actually buy
    // throughput, so this is a limit rather than a target.
    int threads = 128;
    int staggerMs = 0;                      // delay between opening connections
    bool adaptive = true;                   // size the connection count at runtime
    bool minSegmentExplicit = false;        // --min-segment was given by the user
    int maxRetries = 5;                     // per-segment retry budget
    // Chunk (work unit) size. Chunks are what the worker pool pulls from, so a
    // deep queue is what keeps every connection busy to the very end; the
    // planner will shrink this towards "four chunks per connection" unless the
    // user pinned it explicitly with --min-segment.
    u64 minSegmentSize = 1ull * 1024 * 1024; // target chunk size, floor of 1 MiB
    bool resume = true;                     // reuse .kdlpart state
    bool overwrite = false;                 // allow clobbering an existing file
    int connectTimeoutMs = 15000;
    int receiveTimeoutMs = 30000;
    std::string proxy;                      // empty -> system proxy
    std::string userAgent = "KevinDownloader/1.0";
    std::vector<HttpHeader> headers;         // extra request headers
    std::string expectedSha256;              // optional digest (lower-case hex)
    // Additional URLs serving the exact same file. Each one that probes as a
    // size-matching, range-capable source is used in parallel with the primary.
    std::vector<std::string> mirrors;
    bool showProgress = true;
};

struct DownloadResult {
    std::string outputPath;
    u64 totalBytes = 0;
    int segmentCount = 0;
    bool resumed = false;
    double elapsedSeconds = 0.0;
};

class Downloader {
public:
    explicit Downloader(DownloadConfig config);

    // Runs the download to completion. Throws kdl::Error on failure.
    DownloadResult run(const StopToken& stop);

    // Result of probing a single URL.
    struct ProbeInfo {
        std::string finalUrl;
        bool rangesSupported = false;
        bool sizeKnown = false;
        u64 total = kUnknownSize;
        std::string etag;
        std::string lastModified;
        std::string contentDisposition;
    };

    // Probes an arbitrary URL for size / range support / validators.
    ProbeInfo probeUrl(const std::string& url, const StopToken& stop);

    // Probes the primary resource and populates the metadata members.
    void probe(const StopToken& stop);

    // Try to restore a previous session; returns true when state was adopted.
    bool tryLoadResume();

    // Persist the current progress to the .kdlpart sidecar (best effort).
    void persistResume();

    // Worker body for one segment. `client` is owned by the calling worker so
    // the whole run reuses one WinHTTP session (and its keep-alive sockets) per
    // thread instead of rebuilding one per range request. `baseSource` is the
    // endpoint the scheduler picked for this chunk; retries rotate from there.
    void downloadSegment(Segment& segment, FileWriter& writer, ProgressState& progress,
                         HttpClient& client, std::size_t baseSource, const StopToken& stop);
    // Exponential backoff that honours cancellation.
    void backoff(int attempt, const StopToken& stop) const;

    DownloadConfig config_;

    // ---- per-run state ----
    std::string finalUrl_;
    std::string etag_;
    std::string lastModified_;
    std::string contentDisposition_;
    // Every usable endpoint for this file: the primary URL plus each accepted
    // mirror. Segments are spread round-robin across them so that per-host
    // connection / QPS limits are multiplied rather than shared.
    std::vector<std::string> sources_;
    u64 total_ = kUnknownSize;
    bool sizeKnown_ = false;
    bool rangesSupported_ = false;

    std::vector<std::unique_ptr<Segment>> segments_;
    std::unique_ptr<ResumeStore> resumeStore_;
    std::mutex resumeMutex_;
};

} // namespace kdl
