#include "core/ByteUtil.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace kdl {

std::string formatBytes(u64 bytes) {
    static const std::array<const char*, 6> units = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};

    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }

    char buffer[64];
    if (unit == 0) {
        std::snprintf(buffer, sizeof(buffer), "%llu %s",
                      static_cast<unsigned long long>(bytes), units[unit]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, units[unit]);
    }
    return std::string(buffer);
}

std::string formatDuration(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }

    char buffer[64];
    if (seconds < 60.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1fs", seconds);
        return std::string(buffer);
    }

    const auto total = static_cast<long long>(seconds + 0.5);
    const long long hours   = total / 3600;
    const long long minutes = (total % 3600) / 60;
    const long long secs    = total % 60;

    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lldh %02lldm %02llds", hours, minutes, secs);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lldm %02llds", minutes, secs);
    }
    return std::string(buffer);
}

std::string formatRate(double bytesPerSecond) {
    if (bytesPerSecond < 0.0 || std::isnan(bytesPerSecond)) {
        bytesPerSecond = 0.0;
    }
    return formatBytes(static_cast<u64>(bytesPerSecond)) + "/s";
}

} // namespace kdl
