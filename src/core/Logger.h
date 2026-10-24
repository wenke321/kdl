#pragma once

// ---------------------------------------------------------------------------
// Process-wide, thread-safe logger.
//
// Deliberately tiny: it writes timestamped, level-tagged lines to stderr.
// All output is serialized through an internal mutex so log lines from many
// worker threads never interleave.
// ---------------------------------------------------------------------------

#include <atomic>
#include <mutex>
#include <string>

namespace kdl {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Off   = 5,
};

class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level) noexcept { level_.store(level); }
    LogLevel level() const noexcept { return level_.load(); }

    bool enabled(LogLevel level) const noexcept {
        return static_cast<int>(level) >= static_cast<int>(level_.load());
    }

    void log(LogLevel level, const std::string& message);
    void logf(LogLevel level, const char* fmt, ...);

private:
    Logger() = default;

    std::atomic<LogLevel> level_{LogLevel::Info};
    std::mutex mutex_;
};

// Convenience free functions.
inline void logSetLevel(LogLevel level) { Logger::instance().setLevel(level); }
inline LogLevel logGetLevel() { return Logger::instance().level(); }

#define KDL_LOG_INFO(...)  ::kdl::Logger::instance().logf(::kdl::LogLevel::Info, __VA_ARGS__)
#define KDL_LOG_WARN(...)  ::kdl::Logger::instance().logf(::kdl::LogLevel::Warn, __VA_ARGS__)
#define KDL_LOG_ERROR(...) ::kdl::Logger::instance().logf(::kdl::LogLevel::Error, __VA_ARGS__)
#define KDL_LOG_DEBUG(...) ::kdl::Logger::instance().logf(::kdl::LogLevel::Debug, __VA_ARGS__)
#define KDL_LOG_TRACE(...) ::kdl::Logger::instance().logf(::kdl::LogLevel::Trace, __VA_ARGS__)

} // namespace kdl
