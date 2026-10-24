#include "download/Downloader.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>

#include "core/ByteUtil.h"
#include "core/Error.h"
#include "core/Hash.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "thread/ThreadPool.h"
#include "thread/ConcurrencyLimiter.h"
#include "thread/ThreadPool.h"
#include "url/Url.h"

namespace kdl {

namespace {


std::string httpStatusText(int code) {
    return "HTTP " + std::to_string(code);
}

// Extract a file name from a Content-Disposition header value.
std::string filenameFromContentDisposition(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    // Prefer RFC 5987 "filename*=UTF-8''name".
    const std::string lower = toLower(value);
    if (const auto star = lower.find("filename*="); star != std::string::npos) {
        std::string raw = value.substr(star + std::strlen("filename*="));
        if (const auto semi = raw.find(';'); semi != std::string::npos) {
            raw = raw.substr(0, semi);
        }
        raw = trim(raw);
        // Strip charset'language'
        const auto firstQuote = raw.find('\'');
        if (firstQuote != std::string::npos) {
            const auto secondQuote = raw.find('\'', firstQuote + 1);
            if (secondQuote != std::string::npos) {
                raw = raw.substr(secondQuote + 1);
            }
        }
        raw = urlDecode(raw);
        if (!raw.empty()) {
            return raw;
        }
    }

    // Fall back to plain filename="name".
    if (const auto plain = lower.find("filename="); plain != std::string::npos) {
        std::string raw = value.substr(plain + std::strlen("filename="));
        if (const auto semi = raw.find(';'); semi != std::string::npos) {
            raw = raw.substr(0, semi);
        }
        raw = trim(raw);
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }
        if (!raw.empty()) {
            return raw;
        }
    }

    return {};
}

// Remove characters that are illegal in Windows file names / paths.
std::string sanitizeFileName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        switch (c) {
            case '<': case '>': case ':': case '"':
            case '/': case '\\': case '|': case '?': case '*':
            case '\0':
                out.push_back('_');
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out.push_back('_');
                } else {
                    out.push_back(c);
                }
        }
    }
    out = trim(out);
    // Reserved DOS device names.
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2",
                                     "COM3", "COM4", "LPT1", "LPT2", "LPT3"};
    for (const char* r : reserved) {
        if (iEquals(out, r)) {
            out += "_";
            break;
        }
    }
    return out;
}

bool isDirectory(const std::wstring& path) {
    const DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool fileExists(const std::wstring& path) {
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string deriveFileName(const std::string& contentDisposition, const Url& url) {
    const std::string fromHeader = filenameFromContentDisposition(contentDisposition);
    if (!fromHeader.empty()) {
        return sanitizeFileName(fromHeader);
    }
    const std::string fromPath = url.filenameFromPath();
    if (!fromPath.empty()) {
        return sanitizeFileName(fromPath);
    }
    return "download";
}

std::string resolveOutputPath(const std::string& configured, const std::string& derived) {
    if (configured.empty()) {
        return derived;
    }
    const std::wstring wide = toWide(configured);
    if (isDirectory(wide)) {
        std::string base = configured;
        if (base.back() != '\\' && base.back() != '/') {
            base += '\\';
        }
        return base + derived;
    }
    if (configured.back() == '\\' || configured.back() == '/') {
        return configured + derived;
    }
    return configured;
}

std::vector<std::unique_ptr<Segment>> buildSegments(u64 total, u64 count) {
    std::vector<std::unique_ptr<Segment>> segments;
    if (count == 0) {
        count = 1;
    }
    segments.reserve(static_cast<std::size_t>(count));

    const u64 base = total / count;
    const u64 remainder = total % count;
    u64 offset = 0;

    for (u64 i = 0; i < count; ++i) {
        const u64 length = base + (i < remainder ? 1u : 0u);
        auto segment = std::make_unique<Segment>();
        segment->index = static_cast<int>(i);
        segment->start = offset;
        segment->end = (length == 0) ? offset : offset + length - 1;
        segments.push_back(std::move(segment));
        offset += length;
    }
    return segments;
}

void renderProgress(u64 downloaded, u64 total, double rate) {
    constexpr int kWidth = 30;
    std::string line = "\r  ";

    if (total == kUnknownSize || total == 0) {
        line += "downloading ";
        line += formatBytes(downloaded);
    } else {
        const double fraction =
            total == 0 ? 1.0 : static_cast<double>(downloaded) / static_cast<double>(total);
        const int filled = static_cast<int>(fraction * kWidth);
        line += '[';
        for (int i = 0; i < kWidth; ++i) {
            line += (i < filled) ? '=' : ' ';
        }
        line += "] ";

        char percent[16];
        std::snprintf(percent, sizeof(percent), "%5.1f%%", fraction * 100.0);
        line += percent;
        line += "  ";
        line += formatBytes(downloaded) + " / " + formatBytes(total);
    }

    line += "  ";
    line += formatRate(rate);

    // Overwriting with a shorter frame leaves the tail of the previous one on
    // screen (e.g. a stray "/s" when the rate falls from "1.16 MiB/s" to
    // "0 B/s"), so pad up to the widest line drawn so far.
    static std::size_t previousWidth = 0;
    if (line.size() < previousWidth) {
        line.append(previousWidth - line.size(), ' ');
    }
    previousWidth = line.size();

    std::fputs(line.c_str(), stdout);
    std::fflush(stdout);
}

} // namespace

// ---------------------------------------------------------------------------

Downloader::Downloader(DownloadConfig config) : config_(std::move(config)) {
    if (config_.threads < 1) {
        config_.threads = 1;
    }
    if (config_.threads > 128) {
        config_.threads = 128;
    }
    if (config_.maxRetries < 1) {
        config_.maxRetries = 1;
    }
}

Downloader::ProbeInfo Downloader::probeUrl(const std::string& url, const StopToken& stop) {
    HttpClient client(config_.proxy, config_.userAgent);

    HttpRequest request;
    request.url = url;
    request.method = "GET";
    request.headers = config_.headers;
    request.rangeStart = 0;
    request.rangeEnd = 0;
    request.connectTimeoutMs = config_.connectTimeoutMs;
    request.receiveTimeoutMs = config_.receiveTimeoutMs;

    // We only need the headers: abort as soon as the first body byte arrives.
    const HttpResponse response =
        client.perform(request, [](const char*, std::size_t) { return false; });

    if (stop.stopRequested()) {
        throw Error(ErrorCode::Cancelled, "cancelled during probe");
    }

    ProbeInfo info;
    info.finalUrl = response.finalUrl.empty() ? url : response.finalUrl;
    info.etag = response.etag;
    info.lastModified = response.lastModified;
    info.contentDisposition = response.contentDisposition;

    if (response.statusCode == 206) {
        info.rangesSupported = true;
        if (response.rangeTotal.has_value()) {
            info.total = *response.rangeTotal;
            info.sizeKnown = true;
        }
    } else if (response.statusCode == 200) {
        if (response.contentLength.has_value()) {
            info.total = *response.contentLength;
            info.sizeKnown = true;
        }
    } else if (response.statusCode == 416) {
        // Range not satisfiable -> empty (or already complete) resource.
        info.rangesSupported = true;
        info.total = 0;
        info.sizeKnown = true;
    } else if (response.statusCode >= 400) {
        throw Error(ErrorCode::HttpProtocol,
                    "server returned " + httpStatusText(response.statusCode));
    } else if (response.contentLength.has_value()) {
        info.total = *response.contentLength;
        info.sizeKnown = true;
    }

    if (info.rangesSupported && !info.sizeKnown) {
        // A 206 without a usable Content-Range total cannot be segmented.
        info.rangesSupported = false;
    }

    return info;
}

void Downloader::probe(const StopToken& stop) {
    const ProbeInfo info = probeUrl(config_.url, stop);

    finalUrl_ = info.finalUrl;
    etag_ = info.etag;
    lastModified_ = info.lastModified;
    contentDisposition_ = info.contentDisposition;
    total_ = info.total;
    sizeKnown_ = info.sizeKnown;
    rangesSupported_ = info.rangesSupported;

    // Build the endpoint pool: the primary URL first, then every mirror that
    // provably serves the same object. Validating a mirror up-front is a lot
    // cheaper than discovering a mismatch halfway through a multi-gigabyte
    // transfer, and each accepted mirror multiplies the request budget the
    // server side is willing to grant a single client IP.
    sources_.clear();
    sources_.push_back(finalUrl_);

    for (const std::string& mirror : config_.mirrors) {
        if (mirror.empty() || mirror == finalUrl_) {
            continue;
        }

        ProbeInfo mirrorInfo;
        try {
            mirrorInfo = probeUrl(mirror, stop);
        } catch (const Error& e) {
            if (e.code() == ErrorCode::Cancelled) {
                throw;
            }
            KDL_LOG_WARN("mirror %s rejected: %s", mirror.c_str(), e.what());
            continue;
        }

        if (!mirrorInfo.rangesSupported) {
            KDL_LOG_WARN("mirror %s rejected: no byte-range support", mirror.c_str());
            continue;
        }
        if (sizeKnown_ && (!mirrorInfo.sizeKnown || mirrorInfo.total != total_)) {
            KDL_LOG_WARN("mirror %s rejected: size %llu != %llu", mirror.c_str(),
                         static_cast<unsigned long long>(mirrorInfo.total),
                         static_cast<unsigned long long>(total_));
            continue;
        }
        if (!etag_.empty() && !mirrorInfo.etag.empty() && mirrorInfo.etag != etag_) {
            KDL_LOG_WARN("mirror %s rejected: etag mismatch", mirror.c_str());
            continue;
        }

        KDL_LOG_INFO("mirror accepted: %s", mirrorInfo.finalUrl.c_str());
        sources_.push_back(mirrorInfo.finalUrl);
    }

    KDL_LOG_DEBUG("probe ok: final=%s size=%llu ranges=%d", finalUrl_.c_str(),
                  static_cast<unsigned long long>(total_), rangesSupported_ ? 1 : 0);
}

bool Downloader::tryLoadResume() {
    if (!config_.resume || !resumeStore_) {
        return false;
    }

    ResumeData data;
    if (!resumeStore_->load(data)) {
        return false;
    }

    const bool sameTarget = (data.url == finalUrl_);
    const bool sameSize = sizeKnown_ && (data.totalSize == total_);
    const bool sameValidators = (data.etag == etag_) && (data.lastModified == lastModified_);

    if (!sameTarget || !sameSize || !sameValidators) {
        KDL_LOG_DEBUG("resume state rejected (url/size/validator mismatch)");
        resumeStore_->remove();
        return false;
    }

    // Ranged resume is only possible when the server supports ranges.
    bool hasPartial = false;
    for (const ResumeSegment& rs : data.segments) {
        if (rs.downloaded > 0) {
            hasPartial = true;
            break;
        }
    }
    if (hasPartial && !rangesSupported_) {
        KDL_LOG_DEBUG("resume state rejected (server no longer supports ranges)");
        resumeStore_->remove();
        return false;
    }

    segments_.clear();
    segments_.reserve(data.segments.size());
    for (std::size_t i = 0; i < data.segments.size(); ++i) {
        const ResumeSegment& rs = data.segments[i];
        auto segment = std::make_unique<Segment>();
        segment->index = static_cast<int>(i);
        segment->start = rs.start;
        segment->end = rs.end;
        segment->downloaded.store(std::min(rs.downloaded, rs.end - rs.start + 1),
                                  std::memory_order_relaxed);
        segment->state = segment->isComplete() ? SegmentState::Completed
                                               : SegmentState::Pending;
        segments_.push_back(std::move(segment));
    }

    KDL_LOG_INFO("resuming previous session (%zu segments)", segments_.size());
    return true;
}

void Downloader::persistResume() {
    if (!config_.resume || !sizeKnown_ || !resumeStore_) {
        return;
    }

    std::lock_guard<std::mutex> lock(resumeMutex_);

    ResumeData data;
    data.url = finalUrl_;
    data.etag = etag_;
    data.lastModified = lastModified_;
    data.totalSize = total_;
    data.segments.reserve(segments_.size());
    for (const auto& segment : segments_) {
        ResumeSegment rs;
        rs.start = segment->start;
        rs.end = segment->end;
        rs.downloaded = segment->downloaded.load(std::memory_order_relaxed);
        data.segments.push_back(rs);
    }

    u64 checkpointed = 0;
    for (const auto& s : segments_) {
        checkpointed += s->downloaded.load(std::memory_order_relaxed);
    }
    KDL_LOG_DEBUG("resume checkpoint: %llu bytes committed",
                  static_cast<unsigned long long>(checkpointed));

    try {
        resumeStore_->save(data);
    } catch (const Error& e) {
        KDL_LOG_WARN("could not persist resume state: %s", e.what());
    }
}

void Downloader::backoff(int attempt, const StopToken& stop) const {
    // Exponential backoff capped at 30 s, with "full jitter": every worker
    // draws its own delay from [base/2, base). Without the jitter all sixteen
    // segments that hit a 429 at the same instant would also retry at the same
    // instant, which is exactly the burst the throttle is reacting to.
    thread_local std::mt19937 rng{std::random_device{}()};

    const int shift = std::min(attempt - 1, 6);
    int baseMs = 500 * (1 << shift);
    baseMs = std::min(baseMs, 30000);

    std::uniform_int_distribution<int> dist(baseMs / 2, baseMs);
    const int delayMs = dist(rng);

    constexpr int kStep = 100;
    for (int waited = 0; waited < delayMs && !stop.stopRequested(); waited += kStep) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kStep));
    }
}

void Downloader::downloadSegment(Segment& segment, FileWriter& writer,
                                 ProgressState& progress, HttpClient& client,
                                 std::size_t baseSource, const StopToken& stop) {
    const bool multiSegment = segments_.size() > 1;
    const bool expectsRange = multiSegment || segment.start > 0;

    for (int attempt = 1; attempt <= config_.maxRetries; ++attempt) {
        if (stop.stopRequested()) {
            segment.state = SegmentState::Failed;
            segment.error = "cancelled";
            return;
        }

        if (segment.isComplete()) {
            segment.state = SegmentState::Completed;
            return;
        }

        segment.state = SegmentState::Downloading;

        // Rotate through the source list on every retry, starting at the
        // endpoint the scheduler handed us. Rotating is what keeps a burst of
        // Range requests clear of the per-IP QPS throttling a single mirror
        // applies: a request that comes back throttled is simply retried on a
        // different host.
        const std::size_t sourceIndex =
            sources_.empty()
                ? 0
                : (baseSource + static_cast<std::size_t>(attempt - 1)) % sources_.size();
        if (sources_.size() > 1) {
            KDL_LOG_DEBUG("segment %d attempt %d/%d -> source %zu of %zu", segment.index,
                          attempt, config_.maxRetries, sourceIndex + 1, sources_.size());
        }
        try {
            HttpRequest request;
            request.url = sources_.empty() ? finalUrl_ : sources_[sourceIndex];
            request.method = "GET";
            request.headers = config_.headers;
            request.connectTimeoutMs = config_.connectTimeoutMs;
            request.receiveTimeoutMs = config_.receiveTimeoutMs;

            const bool useRange = expectsRange || segment.downloaded > 0;
            if (segment.end != kUnknownSize && useRange) {
                request.rangeStart = segment.start + segment.downloaded;
                request.rangeEnd = segment.end;
            } else if (segment.end == kUnknownSize && segment.downloaded > 0) {
                request.rangeStart = segment.downloaded;
            }

            bool aborted = false;

            const HttpResponse response = client.perform(
                request, [&](const char* data, std::size_t size) -> bool {
                    if (stop.stopRequested()) {
                        aborted = true;
                        return false;
                    }

                    std::size_t writable = size;
                    if (segment.end != kUnknownSize) {
                        const u64 expected = segment.expectedLength();
                        const u64 have = segment.downloaded.load(std::memory_order_relaxed);
                        const u64 remaining = have >= expected ? 0 : expected - have;
                        if (remaining == 0) {
                            aborted = true;
                            return false;
                        }
                        if (writable > remaining) {
                            writable = static_cast<std::size_t>(remaining);
                        }
                    }

                    writer.writeAt(segment.start + segment.downloaded.load(std::memory_order_relaxed),
                                   data, writable);
                    segment.downloaded.fetch_add(writable, std::memory_order_relaxed);
                    progress.downloaded.fetch_add(writable, std::memory_order_relaxed);

                    if (writable < size) {
                        aborted = true;
                        return false;
                    }
                    return true;
                });


            if (request.rangeStart.has_value()) {
                if (!response.isPartialContent()) {
                    throw Error(ErrorCode::HttpProtocol,
                                "expected 206 but got " + httpStatusText(response.statusCode));
                }
                if (response.rangeTotal.has_value() && sizeKnown_ &&
                    *response.rangeTotal != total_) {
                    throw Error(ErrorCode::HttpProtocol,
                                "resource changed on the server (size mismatch)");
                }
            } else if (!response.isSuccess()) {
                throw Error(ErrorCode::HttpProtocol,
                            "server returned " + httpStatusText(response.statusCode));
            }

            if (aborted) {
                if (stop.stopRequested()) {
                    segment.state = SegmentState::Failed;
                    segment.error = "cancelled";
                    return;
                }
                throw Error(ErrorCode::Network, "transfer interrupted");
            }

            if (segment.end == kUnknownSize) {
                const u64 have = segment.downloaded.load(std::memory_order_relaxed);
                segment.end = have > 0 ? have - 1 : 0;
                segment.state = SegmentState::Completed;
                return;
            }

            if (segment.isComplete()) {
                segment.state = SegmentState::Completed;
                return;
            }

            throw Error(ErrorCode::Network, "incomplete transfer");
        } catch (const Error& e) {
            if (e.code() == ErrorCode::Cancelled) {
                segment.state = SegmentState::Failed;
                segment.error = "cancelled";
                return;
            }
            segment.error = e.what();
            KDL_LOG_WARN("segment %d attempt %d/%d failed: %s", segment.index, attempt,
                         config_.maxRetries, e.what());
        } catch (const std::exception& e) {
            segment.error = e.what();
            KDL_LOG_WARN("segment %d attempt %d/%d failed: %s", segment.index, attempt,
                         config_.maxRetries, e.what());
        }

        if (attempt < config_.maxRetries) {
            backoff(attempt, stop);
        }
    }

    segment.state = SegmentState::Failed;
    if (segment.error.empty()) {
        segment.error = "exceeded retry limit";
    }
}

DownloadResult Downloader::run(const StopToken& stop) {
    if (config_.url.empty()) {
        throw Error(ErrorCode::InvalidUrl, "no URL specified");
    }

    const auto startTime = std::chrono::steady_clock::now();

    // 1. Probe the resource.
    probe(stop);
    KDL_LOG_INFO("target: %s", finalUrl_.c_str());
    if (sizeKnown_) {
        KDL_LOG_INFO("size: %s%s", formatBytes(total_).c_str(),
                     rangesSupported_ ? " (range requests supported)" : "");
    } else {
        KDL_LOG_INFO("size: unknown (streaming download)");
    }
    // -t is the total connection budget for the whole transfer, not a
    // per-source one. Two reasons: the scarce resource is usually the client's
    // own link, and a budget that grows with the number of mirrors is exactly
    // the kind of thing that turns "add a mirror" into "flood three servers and
    // go slower" (measured, not theorised: 384 connections over three hosts
    // moved less than 128 over one). The scheduler below decides how that
    // budget is spread over the sources.
    const u64 endpoints = std::max<u64>(static_cast<u64>(sources_.size()), 1u);
    const u64 connectionBudget = static_cast<u64>(config_.threads);

    if (sources_.size() > 1) {
        KDL_LOG_INFO("sources: %zu endpoints (%zu mirror(s)), up to %llu connection(s) in total",
                     sources_.size(), sources_.size() - 1,
                     static_cast<unsigned long long>(connectionBudget));
    }

    // 2. Resolve the output path.
    const Url url = Url::parse(finalUrl_);
    const std::string fileName = deriveFileName(contentDisposition_, url);

    const std::string outputPath = resolveOutputPath(config_.outputPath, fileName);
    const std::wstring wideOutput = toWide(outputPath);
    resumeStore_ = std::make_unique<ResumeStore>(wideOutput + L".kdlpart");

    // 3. Load resume state (or reject it).
    bool resumed = false;
    if (sizeKnown_ && total_ > 0) {
        resumed = tryLoadResume();
    }

    if (!resumed) {
        if (fileExists(wideOutput) && !config_.overwrite) {
            throw Error(ErrorCode::Io,
                        "output file already exists: " + outputPath + " (use --overwrite)");
        }
        if (resumeStore_) {
            resumeStore_->remove();
        }
    }

    // 4. Build the segment plan.
    //
    // A chunk is a unit of *scheduling*, not a thread: workers pull them from a
    // shared queue, and a deep queue is what lets a connection that happens to
    // be fast absorb the work a slow one has not finished yet. Too shallow a
    // queue is a real cost - with one chunk per connection the transfer can
    // only end when the slowest single connection ends, which on a throttling
    // origin is where a surprising share of the wall clock goes.
    //
    // So the default is roughly four chunks per connection. The floor keeps
    // the request rate sane (a 64 KiB request stream is already a lot of
    // round trips for no gain), and --min-segment overrides the whole thing
    // when the user has a reason to care about the exact chunk size.
    if (!resumed) {
        u64 count = 1;
        if (rangesSupported_ && sizeKnown_ && total_ > 0 && config_.minSegmentSize > 0) {
            constexpr u64 kMinChunk = 64ull * 1024;
            u64 chunk = config_.minSegmentSize;
            if (!config_.minSegmentExplicit) {
                const u64 targetChunks = std::max<u64>(connectionBudget * 4, 1);
                const u64 byCount = (total_ + targetChunks - 1) / targetChunks;
                chunk = std::min<u64>(chunk, std::max<u64>(byCount, kMinChunk));
            }
            count = (total_ + chunk - 1) / chunk;
            count = std::max<u64>(count, 1);
        }
        segments_ = buildSegments(sizeKnown_ ? total_ : kUnknownSize, count);
    }

    if (segments_.empty()) {
        throw Error(ErrorCode::Internal, "no segments planned");
    }

    // 5. Open the target file.
    FileWriter writer;
    writer.open(wideOutput, sizeKnown_ ? total_ : kUnknownSize, resumed);

    // 6. Prepare progress accounting.
    ProgressState progress;
    progress.reset(sizeKnown_ ? total_ : kUnknownSize);
    for (const auto& segment : segments_) {
        progress.downloaded.fetch_add(segment->downloaded.load(std::memory_order_relaxed),
                                      std::memory_order_relaxed);
    }

    KDL_LOG_INFO("downloading to: %s (%zu segments)", outputPath.c_str(), segments_.size());
    persistResume();

    // 7. Adaptive concurrency, progress monitor and checkpointing.
    //
    // The pool below is created with `ceiling` threads, but only
    // `limiter.limit()` of them may be inside a transfer at any moment - the
    // rest wait at the gate. Sizing that limit is the whole game: too few
    // connections and an origin that throttles each of them individually
    // leaves most of the bandwidth unused; too many and the link (or the
    // server's per-client accounting) gets slower than the sum of its parts.
    // The right number is not a property of this program, it is a property of
    // the path to *this* file right now, so it is measured instead of guessed.
    const std::size_t ceiling = static_cast<std::size_t>(std::min<u64>(connectionBudget, 512));
    const std::size_t floorLimit = static_cast<std::size_t>(
        std::min<u64>(ceiling, std::max<u64>(1, static_cast<u64>(endpoints) * 4)));
    const std::size_t startLimit =
        config_.adaptive ? std::min<std::size_t>(ceiling, floorLimit * 2) : ceiling;
    ConcurrencyLimiter limiter(startLimit);
    std::atomic<int> transientFailures{0};
    std::atomic<unsigned> rampSteps{0};
    std::atomic<unsigned> backOffs{0};
    std::atomic<std::size_t> settledLimit{startLimit};
    KDL_LOG_DEBUG("adaptive concurrency: %s (start %zu, floor %zu, ceiling %zu)",
                  config_.adaptive ? "on" : "off (fixed)", startLimit, floorLimit, ceiling);

    std::atomic<bool> monitorStop{false};
    std::thread monitor([&] {
        auto lastTick = std::chrono::steady_clock::now();
        auto lastCheckpoint = lastTick;
        u64 lastBytes = progress.downloaded.load(std::memory_order_relaxed);
        double smoothedRate = 0.0;
        bool haveRate = false;

        // Controller state: `bestRate` is the best throughput seen at a given
        // connection count and `cooldown` suppresses growth for a couple of
        // windows after a back-off, so the count cannot ping-pong.
        double bestRate = 0.0;
        int cooldown = 0;

        while (!monitorStop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (monitorStop.load(std::memory_order_relaxed)) {
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            const u64 current = progress.downloaded.load(std::memory_order_relaxed);

            const double dt = std::chrono::duration<double>(now - lastTick).count();
            if (dt > 0.0) {
                const double instant = static_cast<double>(current - lastBytes) / dt;
                // Exponentially weighted moving average: a raw per-tick delta is
                // far too noisy to read (it dips to zero whenever a single
                // sampling window happens to contain no completed read), and the
                // controller below needs a signal that is not pure noise either.
                smoothedRate = haveRate ? smoothedRate * 0.7 + instant * 0.3 : instant;
                haveRate = true;

                if (config_.adaptive) {
                    // Two signals drive this loop:
                    //   * Connections that fail (timeouts, resets, 429/503) are
                    //     the server - or the link - saying "too much". That is
                    //     the only trustworthy evidence that the count is too
                    //     high, so it is what triggers a back-off.
                    //   * A throughput plateau means the count is already high
                    //     enough, so growth simply stops (hold).
                    // Deliberately *not* a signal: a throughput dip on its own.
                    // Where an origin throttles each connection individually,
                    // throughput is proportional to the number of connections,
                    // so shrinking because the rate dipped is what *makes* the
                    // rate dip - the controller talks itself down to the floor
                    // and blames the network. Failures do not have that failure
                    // mode: closed connections cannot be caused by backing off.
                    const int failures = transientFailures.exchange(0, std::memory_order_relaxed);
                    const std::size_t limit = limiter.limit();
                    if (failures >= 3) {
                        const std::size_t shrunk = std::max<std::size_t>(
                            floorLimit, limit - std::max<std::size_t>(2, limit / 4));
                        if (shrunk < limit) {
                            limiter.setLimit(shrunk);
                            backOffs.fetch_add(1, std::memory_order_relaxed);
                            KDL_LOG_DEBUG("adaptive: %d failure(s) -> back off to %zu connection(s)",
                                          failures, shrunk);
                        }
                        // Re-baseline a little above what we just measured: after
                        // a back-off, "recovering" has to mean beating this
                        // number, not merely matching it.
                        bestRate = smoothedRate * 1.10;
                        cooldown = 2;
                    } else if (smoothedRate > bestRate * 1.05) {
                        bestRate = smoothedRate;
                        if (cooldown == 0 && limit < ceiling) {
                            const std::size_t grown = std::min<std::size_t>(
                                ceiling, limit + std::max<std::size_t>(2, limit));
                            limiter.setLimit(grown);
                            rampSteps.fetch_add(1, std::memory_order_relaxed);
                            KDL_LOG_DEBUG("adaptive: %s -> %zu connection(s)",
                                          formatRate(bestRate).c_str(), grown);
                        }
                    } else if (cooldown > 0) {
                        --cooldown;
                    }
                    settledLimit.store(limiter.limit(), std::memory_order_relaxed);
                }
            }
            lastTick = now;
            lastBytes = current;

            if (config_.showProgress) {
                renderProgress(current, progress.total.load(std::memory_order_relaxed),
                               smoothedRate);
            }

            if (now - lastCheckpoint >= std::chrono::seconds(2)) {
                persistResume();
                lastCheckpoint = now;
            }
        }
    });

    // 8. Run the workers.
    //
    // Work is claimed from a shared queue rather than partitioned up-front.
    // Two sources - or two connections to the same source - never run at the
    // same speed, and any static split therefore ends the transfer when the
    // *slowest* of N equally sized segments finishes, leaving the faster
    // connections idle. A queue keeps every worker busy until the last byte.
    {
        // The pool is sized for the *ceiling*, not for the current limit: the
        // limiter below decides how many of these threads may actually be
        // inside a transfer, so resizing the count at runtime never has to
        // create or destroy threads mid-transfer.
        const std::size_t poolSize = static_cast<std::size_t>(
            std::min<u64>(static_cast<u64>(segments_.size()), static_cast<u64>(ceiling)));
        KDL_LOG_DEBUG("worker pool: %zu thread(s) over %llu chunk(s)", poolSize,
                      static_cast<unsigned long long>(segments_.size()));
        ThreadPool pool(poolSize);
        std::atomic<std::size_t> next{0};

        // Per-endpoint accounting used by the scheduler below. Mirrors never
        // run at the same speed, and a mirror that has been throttled can be an
        // order of magnitude slower than the primary, so handing every endpoint
        // an equal share of the queue is a good way to waste half the available
        // bandwidth. Instead each free worker asks for the endpoint whose
        // estimated completion time - bytes already assigned divided by the
        // bytes per second that endpoint has actually delivered - is smallest.
        // A slow host therefore naturally receives fewer chunks, and a host
        // that starts failing is avoided outright rather than being retried in
        // a loop: one dead mirror must never hold the transfer hostage.
        struct EndpointStats {
            std::atomic<u64> bytes{0};
            std::atomic<u64> inFlight{0};
            std::atomic<long long> firstClaimUs{0};
            std::atomic<int> failures{0};
        };
        std::vector<EndpointStats> stats(std::max<std::size_t>(sources_.size(), 1));

        const u64 approxChunk = segments_.empty() ? 0 : segments_.front()->expectedLength();

        // Per-endpoint connection cap. Connections are what a mirror sees, so
        // this is where the adaptive ramp and the per-source budget meet: with
        // several mirrors the run may hold `ceiling` connections in total, but
        // no single host is asked for more than the -t ceiling.
        const u64 perEndpointCap = std::max<u64>(static_cast<u64>(config_.threads), 1);

        auto pickEndpoint = [&stats, approxChunk, perEndpointCap]() -> std::size_t {
            if (stats.size() == 1) {
                return 0;
            }

            const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();

            // Reference throughput for endpoints with no data yet, so that the
            // very first chunks are spread over every host (which is also how
            // the scheduler learns which one is fast).
            double sum = 0.0;
            int measured = 0;
            int dead = 0;
            for (const EndpointStats& s : stats) {
                const long long start = s.firstClaimUs.load(std::memory_order_relaxed);
                const u64 bytes = s.bytes.load(std::memory_order_relaxed);
                if (s.failures.load(std::memory_order_relaxed) >= 2) {
                    ++dead;
                }
                if (start > 0 && bytes > 0 && nowUs > start) {
                    sum += static_cast<double>(bytes) / static_cast<double>(nowUs - start) * 1e6;
                    ++measured;
                }
            }
            const double prior = measured > 0 ? sum / measured : 1.0;
            // Only honour the circuit breaker while something else is healthy,
            // otherwise a run where every mirror is angry would stall.
            const bool skipFailing = dead < static_cast<int>(stats.size());

            std::size_t best = 0;
            double bestScore = -1.0;
            std::size_t leastLoaded = 0;
            u64 minInFlight = stats[0].inFlight.load(std::memory_order_relaxed);
            for (std::size_t i = 0; i < stats.size(); ++i) {
                const EndpointStats& s = stats[i];
                const u64 inFlight = s.inFlight.load(std::memory_order_relaxed);
                const int failures = s.failures.load(std::memory_order_relaxed);

                if (inFlight < minInFlight) {
                    minInFlight = inFlight;
                    leastLoaded = i;
                }
                if (skipFailing && failures >= 2) {
                    continue;
                }
                // Do not open more than the per-source budget on one host.
                if (inFlight >= perEndpointCap) {
                    continue;
                }

                const long long start = s.firstClaimUs.load(std::memory_order_relaxed);
                const u64 bytes = s.bytes.load(std::memory_order_relaxed);
                const u64 assigned = bytes + inFlight * approxChunk;

                double speed = prior;
                if (start > 0 && bytes > 0 && nowUs > start) {
                    speed = static_cast<double>(bytes) / static_cast<double>(nowUs - start) * 1e6;
                }
                // A host that has just failed gets pushed to the back of the
                // queue rather than removed from it; it may still be the only
                // one that can serve a retry.
                const double penalty = 1.0 + 3.0 * static_cast<double>(failures);
                const double score =
                    static_cast<double>(assigned) / std::max(speed, 1.0) * penalty;
                if (bestScore < 0.0 || score < bestScore) {
                    bestScore = score;
                    best = i;
                }
            }

            if (bestScore < 0.0) {
                // Everything is failing or saturated: fall back to the endpoint
                // with the fewest transfers in flight.
                best = leastLoaded;
            }
            return best;
        };

        auto claimEndpoint = [&stats](std::size_t index) {
            EndpointStats& s = stats[index];
            long long expected = 0;
            const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();
            s.firstClaimUs.compare_exchange_strong(expected, nowUs, std::memory_order_relaxed);
            s.inFlight.fetch_add(1, std::memory_order_relaxed);
        };

        for (std::size_t worker = 0; worker < poolSize; ++worker) {
            pool.submit([this, worker, &writer, &progress, &stop, &next, &claimEndpoint, &stats,
                         &pickEndpoint, &limiter, &transientFailures] {
                // Optional staggered start: some mirrors rate-limit the rate at
                // which a client may open *new* connections (QPS), and firing
                // every connection in one burst can trip that throttle for the
                // lifetime of those connections.
                if (config_.staggerMs > 0 && worker > 0) {
                    const auto total = std::chrono::milliseconds(
                        static_cast<long long>(config_.staggerMs) *
                        static_cast<long long>(worker));
                    const auto step = std::chrono::milliseconds(25);
                    for (auto waited = std::chrono::milliseconds(0);
                         waited < total && !stop.stopRequested(); waited += step) {
                        std::this_thread::sleep_for(step);
                    }
                }

                HttpClient client(config_.proxy, config_.userAgent);

                while (!stop.stopRequested()) {
                    // Wait at the gate: this is the one place where the
                    // adaptive controller's decision turns into a real
                    // connection count.
                    limiter.acquire();
                    struct SlotGuard {
                        ConcurrencyLimiter& limiter;
                        ~SlotGuard() { limiter.release(); }
                    } guard{limiter};

                    const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= segments_.size()) {
                        return;
                    }

                    Segment& segment = *segments_[index];
                    const std::size_t source = pickEndpoint();
                    segment.source = static_cast<int>(source);
                    claimEndpoint(source);
                    downloadSegment(segment, writer, progress, client, source, stop);

                    EndpointStats& s = stats[source];
                    s.inFlight.fetch_sub(1, std::memory_order_relaxed);
                    if (segment.state == SegmentState::Completed) {
                        // A completed chunk is proof the endpoint works, so the
                        // circuit breaker resets rather than accumulating
                        // ancient history.
                        s.failures.store(0, std::memory_order_relaxed);
                        s.bytes.fetch_add(segment.downloaded.load(std::memory_order_relaxed),
                                          std::memory_order_relaxed);
                    } else {
                        s.failures.fetch_add(1, std::memory_order_relaxed);
                        transientFailures.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }
        pool.wait();

        if (sources_.size() > 1) {
            std::string summary;
            for (std::size_t i = 0; i < sources_.size(); ++i) {
                const int failed = stats[i].failures.load(std::memory_order_relaxed);
                if (!summary.empty()) {
                    summary += ", ";
                }
                summary += formatBytes(stats[i].bytes.load(std::memory_order_relaxed));
                if (failed > 0) {
                    summary += " (failing)";
                }
            }
            KDL_LOG_INFO("sources delivered: %s", summary.c_str());
        }
    }

    // 9. Stop the background monitor.
    monitorStop.store(true, std::memory_order_relaxed);
    if (monitor.joinable()) {
        monitor.join();
    }

    if (config_.adaptive) {
        KDL_LOG_INFO("adaptive: settled at %zu of %zu connection(s) (%u ramp step(s), %u back-off(s))",
                     settledLimit.load(std::memory_order_relaxed), ceiling,
                     rampSteps.load(std::memory_order_relaxed),
                     backOffs.load(std::memory_order_relaxed));
    }

    // 10. Evaluate the outcome.
    std::vector<std::string> failures;
    int completedCount = 0;
    for (const auto& segment : segments_) {
        if (segment->state == SegmentState::Completed) {
            ++completedCount;
        } else {
            failures.push_back("segment " + std::to_string(segment->index) + ": " +
                               (segment->error.empty() ? "unknown error" : segment->error));
        }
    }

    if (!failures.empty()) {
        persistResume(); // keep the partial progress for a later run

        if (stop.stopRequested()) {
            throw Error(ErrorCode::Cancelled, "download cancelled by user");
        }

        std::string message = "download failed (";
        message += failures.front();
        if (failures.size() > 1) {
            message += ", and " + std::to_string(failures.size() - 1) + " more segment(s)";
        }
        message += ")";
        throw Error(ErrorCode::Network, message);
    }

    // 11. Finalize.
    writer.flush();

    const u64 finalBytes = progress.downloaded.load(std::memory_order_relaxed);
    if (sizeKnown_ && finalBytes != total_) {
        throw Error(ErrorCode::Network,
                    "size mismatch after download: got " + std::to_string(finalBytes) +
                        ", expected " + std::to_string(total_));
    }

    // Optional integrity verification against a caller-provided SHA-256.
    if (!config_.expectedSha256.empty()) {
        KDL_LOG_INFO("verifying sha256 ...");
        const std::string actual = sha256File(wideOutput);
        if (!iEquals(actual, config_.expectedSha256)) {
            // The bytes on disk are wrong; drop the resume state so a later run
            // re-downloads rather than "resuming" a corrupt file.
            if (resumeStore_) {
                resumeStore_->remove();
            }
            throw Error(ErrorCode::Verify, "sha256 mismatch: expected " +
                                               config_.expectedSha256 + ", got " + actual);
        }
        KDL_LOG_INFO("sha256 verified: %s", actual.c_str());
    }

    if (resumeStore_) {
        resumeStore_->remove();
    }

    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();

    if (config_.showProgress) {
        // Paint a final, complete frame before the summary is printed.
        const double average =
            elapsedSeconds > 0.0 ? static_cast<double>(finalBytes) / elapsedSeconds : 0.0;
        renderProgress(finalBytes, progress.total.load(std::memory_order_relaxed), average);
        std::fputs("\n", stdout);
    }

    DownloadResult result;
    result.outputPath = outputPath;
    result.totalBytes = finalBytes;
    result.segmentCount = static_cast<int>(segments_.size());
    result.resumed = resumed;
    result.elapsedSeconds = elapsedSeconds;
    return result;
}

} // namespace kdl
