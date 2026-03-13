#ifndef BOMBERMAN_UTIL_CLICOMMON_H
#define BOMBERMAN_UTIL_CLICOMMON_H

#include <charconv>
#include <limits>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#ifndef BOMBERMAN_DEFAULT_LOG_LEVEL
#define BOMBERMAN_DEFAULT_LOG_LEVEL SPDLOG_LEVEL_INFO
#endif

namespace bomberman::cli
{
    struct LoggingCliOptions
    {
        spdlog::level::level_enum logLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        std::string logFile;
        bool hasLogLevelOverride = false;
        bool hasLogFileOverride = false;
    };

    inline constexpr std::string_view kLoggingUsageArgs =
        "[--log-level <trace|debug|info|warn|error|critical>] [--log-file <path>]";

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

    /**
     * @brief Tries to parse one shared logging-related CLI option.
     *
     * @param argc Full argc from main().
     * @param argv Full argv from main().
     * @param ioIndex Current argument index; advanced when an option consumes a value.
     * @param outOptions Shared logging CLI state.
     * @param outError Filled on failure.
     * @return true if this argument was recognized as a shared logging option.
     */
    inline bool tryParseLoggingOption(int argc, char** argv, int& ioIndex, LoggingCliOptions& outOptions, std::string& outError)
    {
        const std::string_view arg = argv[ioIndex];

        if (arg == "--log-level")
        {
            if (ioIndex + 1 >= argc)
            {
                outError = "Missing value for --log-level";
                return true;
            }

            const std::string_view value = argv[++ioIndex];
            if (!parseLogLevel(value, outOptions.logLevel))
            {
                outError = "Invalid log level: " + std::string(value);
                return true;
            }

            outOptions.hasLogLevelOverride = true;
            outError.clear();
            return true;
        }

        if (arg == "--log-file")
        {
            if (ioIndex + 1 >= argc)
            {
                outError = "Missing value for --log-file";
                return true;
            }

            outOptions.logFile = argv[++ioIndex];
            outOptions.hasLogFileOverride = true;
            outError.clear();
            return true;
        }

        return false;
    }
} // namespace bomberman::cli

#endif // BOMBERMAN_UTIL_CLICOMMON_H
