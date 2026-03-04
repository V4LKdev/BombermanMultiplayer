#include "Util/Log.h"

#include <cassert>
#include <mutex>
#include <vector>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace bomberman::log
{
    namespace
    {
        std::once_flag gInitFlag;

        constexpr const char* kClientName   = "client";
        constexpr const char* kServerName   = "server";
        constexpr const char* kGameName     = "game";
        constexpr const char* kProtocolName = "net.proto";

        /**
         * @brief Creates and registers a named logger if missing.
         *
         * Logger level is set to trace so that each sink's own level
         * controls filtering independently.
         */
        void makeLogger(const char* name, const std::vector<spdlog::sink_ptr>& sinks)
        {
            if (spdlog::get(name))
                return;

            const auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
            logger->set_level(spdlog::level::trace);
            logger->flush_on(spdlog::level::warn);
            spdlog::register_logger(logger);
        }

        /**
         * @brief Returns a logger by name, guaranteeing a non-null result.
         *
         * If init() has not been called yet, triggers a default initialization
         * so that early log calls never dereference a nullptr. Asserts in debug
         * builds to surface the misuse during development.
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

    /** @brief Initializes all named loggers. Thread-safe via std::call_once. */
    void init(spdlog::level::level_enum consoleLevel, const std::string& logFile)
    {
        std::call_once(gInitFlag, [&]
        {
            std::vector<spdlog::sink_ptr> sinks;

            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_level(consoleLevel);
            consoleSink->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
            sinks.push_back(consoleSink);

            if (!logFile.empty())
            {
                constexpr std::size_t kMaxFileSize = 5 * 1024 * 1024; // 5 MB
                constexpr std::size_t kMaxFiles    = 3;

                auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    logFile,
                    kMaxFileSize,
                    kMaxFiles);

                fileSink->set_level(spdlog::level::trace);
                fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
                sinks.push_back(fileSink);
            }

            makeLogger(kClientName,   sinks);
            makeLogger(kServerName,   sinks);
            makeLogger(kGameName,     sinks);
            makeLogger(kProtocolName, sinks);
        });
    }

    spdlog::logger* client()   { return getLogger(kClientName); }
    spdlog::logger* server()   { return getLogger(kServerName); }
    spdlog::logger* game()     { return getLogger(kGameName); }
    spdlog::logger* protocol() { return getLogger(kProtocolName); }

} // namespace bomberman::log

