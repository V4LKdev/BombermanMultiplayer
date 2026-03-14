#include "Util/Log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "Util/CliCommon.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace bomberman::log
{
    namespace
    {
        // =============================================================================================================
        // ===== Constants and lookup tables ===========================================================================
        // =============================================================================================================

        std::once_flag gInitFlag; ///< Guards one-time logger init.

        constexpr const char* kClientName      = "client";
        constexpr const char* kServerName      = "server";
        constexpr const char* kGameName        = "game";
        constexpr const char* kNetConnName     = "net.conn";
        constexpr const char* kNetPacketName   = "net.packet";
        constexpr const char* kNetProtoName    = "net.proto";
        constexpr const char* kNetInputName    = "net.input";
        constexpr const char* kNetSnapshotName = "net.snapshot";
        constexpr const char* kNetDiagName     = "net.diag";

        constexpr const char* kDefaultConfigFilePath = "Configs/DefaultLogging.ini";

        // Compile-time active level from SPDLOG_ACTIVE_LEVEL, used to filter out log messages entirely at compile time.
        constexpr spdlog::level::level_enum kCompileTimeActiveLevel =
            static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL);

        // Known logging channels array.
        constexpr std::array<const char*, 9> kChannelNames{{
            kClientName,
            kServerName,
            kGameName,
            kNetConnName,
            kNetPacketName,
            kNetProtoName,
            kNetInputName,
            kNetSnapshotName,
            kNetDiagName
        }};

        // =============================================================================================================
        // ===== String normalisation helpers ==========================================================================
        // =============================================================================================================

        std::string trim(std::string_view text)
        {
            std::size_t begin = 0;
            while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
                ++begin;

            std::size_t end = text.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
                --end;

            return std::string(text.substr(begin, end - begin));
        }

        std::string toLower(std::string_view text)
        {
            std::string lowered;
            lowered.reserve(text.size());
            for (const char ch : text)
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            return lowered;
        }

        // =============================================================================================================
        // ===== Channel level lookup ==================================================================================
        // =============================================================================================================

        bool isKnownChannel(std::string_view name)
        {
            return std::ranges::any_of(kChannelNames, [name](const char* channelName)
            {
                return name == channelName;
            });
        }

        spdlog::level::level_enum configuredChannelLevel(const LogConfig& config, std::string_view name)
        {
            const auto it = config.channelLevels.find(std::string(name));
            if (it != config.channelLevels.end())
                return it->second;

            return config.baseLevel;
        }

        /** @brief Selects the strictest log level among the requested base level, channel-specific override, and compile-time active level. */
        spdlog::level::level_enum effectiveLoggerLevel(spdlog::level::level_enum channelLevel, spdlog::level::level_enum baseLevel)
        {
            auto effectiveLevel = baseLevel;
            if (channelLevel > effectiveLevel)
                effectiveLevel = channelLevel;
            if (kCompileTimeActiveLevel > effectiveLevel)
                effectiveLevel = kCompileTimeActiveLevel;
            return effectiveLevel;
        }

        // =============================================================================================================
        // ===== Logger management =====================================================================================
        // =============================================================================================================

        /** @brief Creates and registers a named logger if missing. */
        void makeLogger(const char* name, spdlog::level::level_enum channelLevel,
                        const std::vector<spdlog::sink_ptr>& sinks, spdlog::level::level_enum baseLevel)
        {
            if (spdlog::get(name))
                return;

            const auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
            logger->set_level(effectiveLoggerLevel(channelLevel, baseLevel));
            logger->flush_on(spdlog::level::warn);
            spdlog::register_logger(logger);
        }

        /** @brief Returns a logger by name, guaranteeing a non-null result. */
        spdlog::logger* getLogger(const char* name)
        {
            auto* logger = spdlog::get(name).get();

            if (!logger)
            {
                throw std::logic_error("Logger accessed before log::init()");
            }

            return logger;
        }
    } // namespace

    // =================================================================================================================
    // ===== Public API implementation =================================================================================
    // =================================================================================================================

    LogConfig makeDefaultConfig()
    {
        LogConfig config{};
        config.baseLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        return config;
    }

    std::string defaultConfigFilePath()
    {
#ifdef BOMBERMAN_SOURCE_DIR
        return (std::filesystem::path(BOMBERMAN_SOURCE_DIR) / kDefaultConfigFilePath).lexically_normal().string();
#else
        return kDefaultConfigFilePath;
#endif
    }


    static bool loadConfigFile(const std::string& path, LogConfig& inOutConfig, std::string& outError)
    {
        std::ifstream input(path);
        if (!input.is_open())
        {
            outError = "Failed to open log config file: " + path;
            return false;
        }

        enum class ESection
        {
            None,
            Log,
            Channels,
        };

        auto section = ESection::None;
        std::string line;
        int lineNumber = 0;
        while (std::getline(input, line))
        {
            ++lineNumber;

            const std::string stripped = trim(line);
            if (stripped.empty() || stripped[0] == '#' || stripped[0] == ';')
                continue;

            if (stripped.front() == '[')
            {
                if (stripped.back() != ']')
                {
                    outError = "Malformed section header in log config at line " + std::to_string(lineNumber);
                    return false;
                }

                // Case-insensitive section name matching.
                const std::string sectionName = toLower(trim(std::string_view(stripped).substr(1, stripped.size() - 2)));
                if (sectionName == "log")
                    section = ESection::Log;
                else if (sectionName == "channels")
                    section = ESection::Channels;
                else
                    section = ESection::None;
                continue;
            }

            const std::size_t equalsPos = stripped.find('=');
            if (equalsPos == std::string::npos)
            {
                outError = "Expected key=value entry in log config at line " + std::to_string(lineNumber);
                return false;
            }

            const std::string key = toLower(trim(std::string_view(stripped).substr(0, equalsPos)));
            const std::string value = toLower(trim(std::string_view(stripped).substr(equalsPos + 1)));

            if (section == ESection::Log)
            {
                if (key == "level")
                {
                    spdlog::level::level_enum parsedLevel{};
                    if (!cli::parseLogLevel(value, parsedLevel))
                    {
                        outError = "Invalid [log] level at line " + std::to_string(lineNumber) + ": " + value;
                        return false;
                    }

                    inOutConfig.baseLevel = parsedLevel;
                }
                else
                {
                    outError = "Unknown [log] key at line " + std::to_string(lineNumber) + ": " + key;
                    return false;
                }
            }
            else if (section == ESection::Channels)
            {
                if (!isKnownChannel(key))
                {
                    outError = "Unknown [channels] logger at line " + std::to_string(lineNumber) + ": " + key;
                    return false;
                }

                spdlog::level::level_enum parsedLevel{};
                if (!bomberman::cli::parseLogLevel(value, parsedLevel))
                {
                    outError = "Invalid [channels] level at line " + std::to_string(lineNumber) + ": " + value;
                    return false;
                }

                inOutConfig.channelLevels[key] = parsedLevel;
            }
            else
            {
                outError = "Key/value entry outside a supported log config section at line " + std::to_string(lineNumber);
                return false;
            }
        }

        outError.clear();
        return true;
    }

    static bool loadDefaultConfigFileIfPresent(LogConfig& inOutConfig, std::string& outError)
    {
        const std::string path = defaultConfigFilePath();

        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (ec)
        {
            outError = "Failed to query default log config file: " + path;
            return false;
        }

        if (!exists)
        {
            outError.clear();
            return true;
        }

        return loadConfigFile(path, inOutConfig, outError);
    }

    bool resolveConfig(LogConfig& outConfig,
                       bool hasBaseLevelOverride,
                       spdlog::level::level_enum baseLevelOverride,
                       bool hasLogFileOverride,
                       const std::string& logFileOverride,
                       std::string& outError)
    {
        outConfig = makeDefaultConfig();

        if (!loadDefaultConfigFileIfPresent(outConfig, outError))
        {
            return false;
        }

        if (hasBaseLevelOverride)
            outConfig.baseLevel = baseLevelOverride;

        if (hasLogFileOverride)
        {
            outConfig.logFilePath = logFileOverride;
        }

        outError.clear();
        return true;
    }

    void init(const LogConfig& config)
    {
        std::call_once(gInitFlag, [&]
        {
            try
            {
                std::vector<spdlog::sink_ptr> sinks;

                // Console sink with color and millisecond timestamps, respecting the base log level.
                auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                consoleSink->set_level(config.baseLevel);
                consoleSink->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
                sinks.push_back(consoleSink);

                // Optional rotating file sink with millisecond timestamps, always capturing all log levels if not compiletime gated.
                if (!config.logFilePath.empty())
                {
                    constexpr std::size_t kMaxFileSize = 5 * 1024 * 1024; // 5 MB
                    constexpr std::size_t kMaxFiles    = 3;

                    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        config.logFilePath,
                        kMaxFileSize,
                        kMaxFiles);

                    fileSink->set_level(spdlog::level::trace);
                    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
                    sinks.push_back(fileSink);
                }

                for (const char* channelName : kChannelNames)
                    makeLogger(channelName, configuredChannelLevel(config, channelName), sinks, config.baseLevel);
            }
            catch (const spdlog::spdlog_ex& e)
            {
                throw std::runtime_error(std::string("Failed to initialize logging: ") + e.what());
            }
        });
    }

    void init(spdlog::level::level_enum baseLevel, const std::string& logFile)
    {
        LogConfig config = makeDefaultConfig();
        config.baseLevel = baseLevel;
        config.logFilePath = logFile;
        init(config);
    }

    spdlog::logger* client()      { return getLogger(kClientName); }
    spdlog::logger* server()      { return getLogger(kServerName); }
    spdlog::logger* game()        { return getLogger(kGameName); }
    spdlog::logger* netConn()     { return getLogger(kNetConnName); }
    spdlog::logger* netPacket()   { return getLogger(kNetPacketName); }
    spdlog::logger* netProto()    { return getLogger(kNetProtoName); }
    spdlog::logger* netInput()    { return getLogger(kNetInputName); }
    spdlog::logger* netSnapshot() { return getLogger(kNetSnapshotName); }
    spdlog::logger* netDiag()     { return getLogger(kNetDiagName); }

} // namespace bomberman::log
