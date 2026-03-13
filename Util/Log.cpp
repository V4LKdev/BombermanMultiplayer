#include "Util/Log.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#include "Util/CliCommon.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace bomberman::log
{
    namespace
    {
        std::once_flag gInitFlag;

        constexpr const char* kClientName      = "client";
        constexpr const char* kServerName      = "server";
        constexpr const char* kGameName        = "game";
        constexpr const char* kNetConnName     = "net.conn";
        constexpr const char* kNetPacketName   = "net.packet";
        constexpr const char* kNetProtoName    = "net.proto";
        constexpr const char* kNetInputName    = "net.input";
        constexpr const char* kNetSnapshotName = "net.snapshot";
        constexpr const char* kNetDiagName     = "net.diag";
        constexpr const char* kPerfName        = "perf";
        constexpr const char* kTestName        = "test";
        constexpr const char* kDefaultConfigFilePath = "Configs/DefaultLogging.ini";

        constexpr spdlog::level::level_enum kCompileTimeActiveLevel =
            static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL);

        struct ChannelDefaultLevel
        {
            const char* name;
            spdlog::level::level_enum level;
        };

        constexpr std::array<ChannelDefaultLevel, 11> kChannelDefaultLevels{{
            {kClientName,      static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL)},
            {kServerName,      static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL)},
            {kGameName,        static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL)},
            {kNetConnName,     spdlog::level::info},
            {kNetPacketName,   spdlog::level::warn},
            {kNetProtoName,    spdlog::level::info},
            {kNetInputName,    spdlog::level::debug},
            {kNetSnapshotName, spdlog::level::debug},
            {kNetDiagName,     spdlog::level::info},
            {kPerfName,        spdlog::level::info},
            {kTestName,        spdlog::level::debug},
        }};

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

        bool isKnownChannel(std::string_view name)
        {
            return std::ranges::any_of(kChannelDefaultLevels, [name](const ChannelDefaultLevel& entry)
            {
                return name == entry.name;
            });
        }

        spdlog::level::level_enum defaultChannelLevel(std::string_view name)
        {
            for (const auto& entry : kChannelDefaultLevels)
            {
                if (name == entry.name)
                    return entry.level;
            }

            return static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        }

        spdlog::level::level_enum configuredChannelLevel(const LogConfig& config, std::string_view name)
        {
            const auto it = config.channelLevels.find(std::string(name));
            if (it != config.channelLevels.end())
                return it->second;

            return defaultChannelLevel(name);
        }

        spdlog::level::level_enum effectiveLoggerLevel(spdlog::level::level_enum channelLevel, spdlog::level::level_enum baseLevel)
        {
            auto effectiveLevel = baseLevel;
            if (channelLevel > effectiveLevel)
                effectiveLevel = channelLevel;
            if (kCompileTimeActiveLevel > effectiveLevel)
                effectiveLevel = kCompileTimeActiveLevel;
            return effectiveLevel;
        }

        /**
         * @brief Creates and registers a named logger if missing.
         *
         * Logger level defaults to the stricter of:
         * 1. the requested base runtime level,
         * 2. the channel-specific default level,
         * 3. the compile-time active level.
         *
         * This logger-level filter applies before any sink-level filtering,
         * including the optional rotating file sink.
         */
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

        /**
         * @brief Returns a logger by name, guaranteeing a non-null result.
         *
         * If init() has not been called yet, asserts in debug builds and then
         * falls back to default initialization so release builds recover
         * defensively instead of dereferencing a nullptr.
         */
        spdlog::logger* getLogger(const char* name)
        {
            auto* logger = spdlog::get(name).get();

            if (!logger)
            {
                assert(false && "Logger accessed before bomberman::log::init()"); ///< Surface missing init() calls during development, but recover gracefully in release.
                init();
                logger = spdlog::get(name).get();
            }

            return logger;
        }
    } // namespace

    LogConfig makeDefaultConfig()
    {
        LogConfig config{};
        config.baseLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);

        for (const auto& entry : kChannelDefaultLevels)
            config.channelLevels.emplace(entry.name, entry.level);

        return config;
    }

    std::string defaultConfigFilePath()
    {
        return kDefaultConfigFilePath;
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

        ESection section = ESection::None;
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

            const std::string key = trim(std::string_view(stripped).substr(0, equalsPos));
            const std::string value = trim(std::string_view(stripped).substr(equalsPos + 1));

            if (section == ESection::Log)
            {
                if (key == "level")
                {
                    spdlog::level::level_enum parsedLevel{};
                    if (!bomberman::cli::parseLogLevel(value, parsedLevel))
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

    /** @brief Initializes all named loggers. Thread-safe via std::call_once. */
    void init(const LogConfig& config)
    {
        std::call_once(gInitFlag, [&]
        {
            std::vector<spdlog::sink_ptr> sinks;

            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_level(config.baseLevel);
            consoleSink->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
            sinks.push_back(consoleSink);

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

            for (const auto& entry : kChannelDefaultLevels)
                makeLogger(entry.name, configuredChannelLevel(config, entry.name), sinks, config.baseLevel);
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
    spdlog::logger* perf()        { return getLogger(kPerfName); }
    spdlog::logger* test()        { return getLogger(kTestName); }

} // namespace bomberman::log
