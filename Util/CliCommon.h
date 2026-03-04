#ifndef BOMBERMAN_UTIL_CLICOMMON_H
#define BOMBERMAN_UTIL_CLICOMMON_H

#include <charconv>
#include <limits>
#include <string_view>

#include <spdlog/spdlog.h>

namespace bomberman::cli
{
    /** @brief Parses a textual log level into a spdlog level enum. */
    inline bool parseLogLevel(std::string_view text, spdlog::level::level_enum& outLevel)
    {
        if (text == "trace")    { outLevel = spdlog::level::trace; return true; }
        if (text == "debug")    { outLevel = spdlog::level::debug; return true; }
        if (text == "info")     { outLevel = spdlog::level::info; return true; }
        if (text == "warn")     { outLevel = spdlog::level::warn; return true; }
        if (text == "error")    { outLevel = spdlog::level::err; return true; }
        if (text == "critical") { outLevel = spdlog::level::critical; return true; }
        return false;
    }

    /** @brief Parses and validates a network port in range [1, 65535]. */
    inline bool parsePort(std::string_view text, uint16_t& outPort)
    {
        unsigned int value = 0;
        const char* begin = text.data();
        const char* end = begin + text.size();
        const auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec != std::errc{} || ptr != end || value == 0 ||
            value > std::numeric_limits<uint16_t>::max())
        {
            return false;
        }

        outPort = static_cast<uint16_t>(value);
        return true;
    }
} // namespace bomberman::cli

#endif // BOMBERMAN_UTIL_CLICOMMON_H
