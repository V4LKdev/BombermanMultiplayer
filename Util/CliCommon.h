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
#if defined(BOMBERMAN_ENABLE_NET_DIAG) && BOMBERMAN_ENABLE_NET_DIAG
    inline constexpr bool kNetDiagAvailable = true;
#else
    inline constexpr bool kNetDiagAvailable = false;
#endif

    struct LoggingCliOptions
    {
        spdlog::level::level_enum logLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        std::string logFile;
        bool hasLogLevelOverride = false;
        bool hasLogFileOverride = false;
    };

    struct DiagnosticsCliOptions
    {
        bool netDiagEnabled = false;
    };

    inline constexpr std::string_view kLoggingUsageArgs =
        "[--log-level <trace|debug|info|warn|error>] [--log-file <path>]";

    inline constexpr std::string_view kDiagnosticsUsageArgs =
        "[--net-diag]";

    /** @brief Parses a textual log level into a spdlog level enum. */
    inline bool parseLogLevel(std::string_view text, spdlog::level::level_enum& outLevel)
    {
        if (text == "trace")    { outLevel = spdlog::level::trace; return true; }
        if (text == "debug")    { outLevel = spdlog::level::debug; return true; }
        if (text == "info")     { outLevel = spdlog::level::info; return true; }
        if (text == "warn")     { outLevel = spdlog::level::warn; return true; }
        if (text == "error")    { outLevel = spdlog::level::err; return true; }
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

    /** @brief Tries to parse one shared diagnostics-related CLI option. */
    inline bool tryParseDiagnosticsOption(int argc, char** argv, int& ioIndex, DiagnosticsCliOptions& outOptions, std::string& outError)
    {
        static_cast<void>(argc);
        static_cast<void>(argv);
        static_cast<void>(ioIndex);

        const std::string_view arg = argv[ioIndex];

        if (arg == "--net-diag")
        {
            if constexpr (kNetDiagAvailable)
            {
                outOptions.netDiagEnabled = true;
                outError.clear();
            }
            else
            {
                outError = "--net-diag is not available in this build";
            }

            return true;
        }

        return false;
    }
} // namespace bomberman::cli

#endif // BOMBERMAN_UTIL_CLICOMMON_H
