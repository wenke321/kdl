// ---------------------------------------------------------------------------
// KevinDownloader - command line entry point.
//
// Responsibilities kept here are deliberately thin:
//   * obtain Unicode command line arguments,
//   * install a Ctrl+C handler wired to the cancellation token,
//   * run the Downloader and map errors to process exit codes.
// ---------------------------------------------------------------------------

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "cli/Options.h"
#include "core/ByteUtil.h"
#include "core/Error.h"
#include "core/Logger.h"
#include "core/StringUtil.h"
#include "download/Downloader.h"

#pragma comment(lib, "shell32.lib")

namespace {

// The token shared between the console handler and the downloader.
kdl::StopToken g_stopToken;

BOOL WINAPI consoleHandler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_stopToken.requestStop();
            return TRUE;
        default:
            return FALSE;
    }
}

// CommandLineToArgvW + UTF-8 conversion: gives us correct Unicode arguments
// without depending on the active ANSI code page.
std::vector<std::string> getArguments() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::string> args;
    if (!argv) {
        return args;
    }
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.push_back(kdl::toUtf8(argv[i]));
    }
    ::LocalFree(argv);
    return args;
}

} // namespace

int main() {
    ::SetConsoleOutputCP(CP_UTF8);

    const std::vector<std::string> args = getArguments();

    kdl::Options options;
    std::string error;
    if (!kdl::parseOptions(args, options, error)) {
        std::fprintf(stderr, "error: %s\n\n%s", error.c_str(), kdl::usageText().c_str());
        return 2;
    }

    if (options.showHelp) {
        std::fputs(kdl::usageText().c_str(), stdout);
        return 0;
    }
    if (options.showVersion) {
        std::printf("%s\n", kdl::versionText().c_str());
        return 0;
    }

    kdl::Logger::instance().setLevel(options.logLevel);
    ::SetConsoleCtrlHandler(consoleHandler, TRUE);

    try {
        kdl::Downloader downloader(options.config);
        const kdl::DownloadResult result = downloader.run(g_stopToken);

        std::printf("done: %s\n", result.outputPath.c_str());
        std::printf("  size     : %s\n", kdl::formatBytes(result.totalBytes).c_str());
        std::printf("  segments : %d%s\n", result.segmentCount, result.resumed ? " (resumed)" : "");
        std::printf("  elapsed  : %s\n", kdl::formatDuration(result.elapsedSeconds).c_str());
        if (result.elapsedSeconds > 0.0) {
            const double rate =
                static_cast<double>(result.totalBytes) / result.elapsedSeconds;
            std::printf("  average  : %s\n", kdl::formatRate(rate).c_str());
        }
        return 0;
    } catch (const kdl::Error& e) {
        std::fputc('\n', stdout);
        if (e.code() == kdl::ErrorCode::Cancelled) {
            std::fprintf(stderr, "interrupted: %s\n", e.what());
            return 130;
        }
        std::fprintf(stderr, "error [%s]: %s\n", kdl::errorCodeName(e.code()), e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fputc('\n', stdout);
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
