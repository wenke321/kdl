#pragma once

// ---------------------------------------------------------------------------
// Command line option parsing.
//
// parseOptions() is pure: it takes the already split argument vector (UTF-8)
// and returns a fully populated Options value, or sets `error` and returns
// false. Keeping it free of side effects makes it trivially testable.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "core/Logger.h"
#include "download/Downloader.h"

namespace kdl {

struct Options {
    DownloadConfig config;
    LogLevel logLevel = LogLevel::Info;
    bool showHelp = false;
    bool showVersion = false;
};

// Parses arguments (excluding argv[0]). Returns false and fills `error` on
// invalid input.
bool parseOptions(const std::vector<std::string>& args, Options& out, std::string& error);

// Usage / help text.
std::string usageText();
std::string versionText();

} // namespace kdl
