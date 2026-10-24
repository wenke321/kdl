#include <utility>
#include "cli/Options.h"

#include <string>

#include "core/StringUtil.h"
#include "core/Hash.h"

namespace kdl {

namespace {

bool needsValue(const std::string& flag, std::size_t index, std::size_t count,
                std::string& error) {
    if (index + 1 >= count) {
        error = "option '" + flag + "' requires a value";
        return false;
    }
    return true;
}

bool parseHeaderArgument(const std::string& value, HttpHeader& out, std::string& error) {
    const auto colon = value.find(':');
    if (colon == std::string::npos) {
        error = "invalid header (expected \"Name: Value\"): " + value;
        return false;
    }
    out.name = trim(value.substr(0, colon));
    out.value = trim(value.substr(colon + 1));
    if (out.name.empty()) {
        error = "invalid header (empty name): " + value;
        return false;
    }
    return true;
}

bool parsePositiveInt(const std::string& value, int minValue, int maxValue, int& out,
                      std::string& error, const char* what) {
    long long parsed = 0;
    if (!parseInt(value, parsed) || parsed < minValue || parsed > maxValue) {
        error = std::string(what) + " out of range [" + std::to_string(minValue) + ", " +
                std::to_string(maxValue) + "]: " + value;
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

} // namespace

bool parseOptions(const std::vector<std::string>& args, Options& out, std::string& error) {
    out = Options{};

    std::vector<std::string> positionals;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "-h" || arg == "--help") {
            out.showHelp = true;
            return true;
        }
        if (arg == "-V" || arg == "--version") {
            out.showVersion = true;
            return true;
        }
        if (arg == "-o" || arg == "--output") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            out.config.outputPath = args[++i];
        } else if (arg == "-t" || arg == "--threads") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            if (!parsePositiveInt(args[++i], 1, 512, out.config.threads, error, "thread count"))
                return false;
        } else if (arg == "-r" || arg == "--retries") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            if (!parsePositiveInt(args[++i], 1, 100, out.config.maxRetries, error, "retry count"))
                return false;
        } else if (arg == "--min-segment") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            u64 bytes = 0;
            if (!parseU64(args[++i], bytes) || bytes == 0) {
                error = "invalid --min-segment value: " + args[i];
                return false;
            }
            out.config.minSegmentSize = bytes;
            out.config.minSegmentExplicit = true;
        } else if (arg == "--timeout") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            int seconds = 0;
            if (!parsePositiveInt(args[++i], 1, 86400, seconds, error, "timeout (seconds)"))
                return false;
            out.config.connectTimeoutMs = seconds * 1000;
            out.config.receiveTimeoutMs = seconds * 1000;
        } else if (arg == "--connect-timeout") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            int seconds = 0;
            if (!parsePositiveInt(args[++i], 1, 86400, seconds, error, "connect timeout"))
                return false;
            out.config.connectTimeoutMs = seconds * 1000;
        } else if (arg == "--no-resume") {
            out.config.resume = false;
        } else if (arg == "--overwrite" || arg == "-f") {
            out.config.overwrite = true;
        } else if (arg == "--no-progress") {
            out.config.showProgress = false;
        } else if (arg == "--adaptive") {
            out.config.adaptive = true;
        } else if (arg == "--no-adaptive") {
            out.config.adaptive = false;
        } else if (arg == "--proxy") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            out.config.proxy = args[++i];
        } else if (arg == "--user-agent" || arg == "-A") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            out.config.userAgent = args[++i];
        } else if (arg == "-H" || arg == "--header") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            HttpHeader header;
            if (!parseHeaderArgument(args[++i], header, error)) return false;
            out.config.headers.push_back(std::move(header));
        } else if (arg == "--sha256") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            const std::string digest = normalizeHex(args[++i]);
            if (!isValidSha256Hex(digest)) {
                error = "invalid --sha256 digest (expected 64 hex characters): " + args[i];
                return false;
            }
            out.config.expectedSha256 = digest;
        } else if (arg == "-m" || arg == "--mirror") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            out.config.mirrors.push_back(args[++i]);
        } else if (arg == "--stagger") {
            if (!needsValue(arg, i, args.size(), error)) return false;
            if (!parsePositiveInt(args[++i], 0, 60000, out.config.staggerMs, error,
                                  "stagger (ms)"))
                return false;
        } else if (arg == "-q" || arg == "--quiet") {
            out.logLevel = LogLevel::Warn;
        } else if (arg == "-v" || arg == "--verbose") {
            out.logLevel = LogLevel::Debug;
        } else if (arg == "-vv") {
            out.logLevel = LogLevel::Trace;
        } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
            error = "unknown option: " + arg;
            return false;
        } else {
            positionals.push_back(arg);
        }
    }

    if (positionals.empty()) {
        error = "no URL specified";
        return false;
    }
    if (positionals.size() > 1) {
        error = "too many positional arguments (only one URL is supported)";
        return false;
    }

    out.config.url = positionals.front();
    return true;
}

std::string usageText() {
    return
        "KevinDownloader " + versionText() + "\n"
        "A production-grade multi-threaded HTTP/HTTPS downloader for Windows.\n"
        "\n"
        "Usage:\n"
        "  kdl [options] <url>\n"
        "\n"
        "Options:\n"
        "  -o, --output <path>       Output file or directory (default: derived from URL)\n"
        "  -t, --threads <n>         Max concurrent connections (1-512, default 128)\n"
        "      --no-adaptive         Use a fixed -t connection count instead of adjusting\n"
        "                            it at runtime\n"
        "      --stagger <ms>        Delay each successive connection by N ms (default 0)\n"
        "  -r, --retries <n>         Per-segment retry budget (default 5)\n"
        "      --min-segment <bytes> Pin the chunk size (default: auto, ~4 chunks per\n"
        "                            connection)\n"
        "      --timeout <seconds>   Connect + receive timeout (default 30)\n"
        "      --connect-timeout <s> Connect timeout only\n"
        "      --no-resume           Ignore and delete any previous partial state\n"
        "  -f, --overwrite           Overwrite an existing output file\n"
        "      --no-progress         Disable the live progress bar\n"
        "      --proxy <url>         Use an explicit proxy (default: system proxy)\n"
        "  -A, --user-agent <ua>     Override the User-Agent header\n"
        "      --sha256 <hex>        Verify the file against this SHA-256 digest\n"
        "  -H, --header \"N: V\"       Add a custom request header (repeatable)\n"
        "  -m, --mirror <url>        Extra source for the same file (repeatable)\n"
        "  -q, --quiet               Only show warnings and errors\n"
        "  -v, --verbose             Verbose (debug) logging\n"
        "  -h, --help                Show this help and exit\n"
        "  -V, --version             Show version and exit\n"
        "\n"
        "Examples:\n"
        "  kdl https://example.com/file.iso\n"
        "  kdl -t 32 -o D:\\Downloads\\file.iso https://example.com/file.iso\n"
        "  kdl --header \"Authorization: Bearer TOKEN\" https://example.com/private.zip\n"
        "  kdl -m https://mirrorB/file -m https://mirrorC/file https://mirrorA/file\n"
        "\n"
        "Performance:\n"
        "  Origins usually cap a single connection far below the speed of the link, so\n"
        "  throughput scales with the number of connections until the server or your own\n"
        "  line saturates - and which of the two it is cannot be seen from the command\n"
        "  line. kdl therefore starts with a few connections and doubles the count while\n"
        "  the transfer keeps getting faster. When it stops improving the count is held,\n"
        "  and it is only lowered when connections start failing. A slot that is not\n"
        "  used never opens a connection, so a server that gains nothing from more\n"
        "  parallelism simply ends up with a lower settled count.\n"
        "  Use --no-adaptive -t <n> to pin an exact count, e.g. on a metered link or\n"
        "  when the server reacts badly to a ramp up.\n"
        "  Several endpoints can be combined with -m; each one is measured while the\n"
        "  transfer runs, endpoints that start failing are dropped, and the fastest\n"
        "  ones receive most of the work. A host that caps requests per second is a\n"
        "  good reason to pass more mirrors (or --stagger 50 to open connections\n"
        "  gradually). Throttled requests (HTTP 429/503) are retried with jittered\n"
        "  exponential backoff, so a transient cap slows a transfer but does not kill\n"
        "  it. Note that a saturated link cannot be improved by any of this: if the\n"
        "  total stops rising as connections go up, the line is the limit.\n";
}

std::string versionText() {
    return "1.0.0";
}

} // namespace kdl
