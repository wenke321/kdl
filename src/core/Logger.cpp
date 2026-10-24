#include "core/Logger.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace kdl {

namespace {

const char* levelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off:   return "OFF  ";
    }
    return "?????";
}

} // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!enabled(level)) {
        return;
    }

    SYSTEMTIME st{};
    ::GetLocalTime(&st);

    char timestamp[32];
    std::snprintf(timestamp, sizeof(timestamp), "%02u:%02u:%02u.%03u",
                  static_cast<unsigned>(st.wHour),
                  static_cast<unsigned>(st.wMinute),
                  static_cast<unsigned>(st.wSecond),
                  static_cast<unsigned>(st.wMilliseconds));

    std::lock_guard<std::mutex> lock(mutex_);
    std::fprintf(stderr, "[%s] %s %s\n", timestamp, levelName(level), message.c_str());
    std::fflush(stderr);
}

void Logger::logf(LogLevel level, const char* fmt, ...) {
    if (!enabled(level)) {
        return;
    }

    char stackBuffer[1024];
    va_list args;
    va_start(args, fmt);
    const int needed = std::vsnprintf(stackBuffer, sizeof(stackBuffer), fmt, args);
    va_end(args);

    if (needed < 0) {
        return;
    }

    if (static_cast<std::size_t>(needed) < sizeof(stackBuffer)) {
        log(level, std::string(stackBuffer, static_cast<std::size_t>(needed)));
        return;
    }

    std::vector<char> heapBuffer(static_cast<std::size_t>(needed) + 1);
    va_start(args, fmt);
    std::vsnprintf(heapBuffer.data(), heapBuffer.size(), fmt, args);
    va_end(args);

    log(level, std::string(heapBuffer.data(), static_cast<std::size_t>(needed)));
}

} // namespace kdl
