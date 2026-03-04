#ifndef BOMBERMAN_UTIL_LOG_H
#define BOMBERMAN_UTIL_LOG_H

#include <string>

#include <spdlog/spdlog.h>

/**
 * @brief Centralized logging interface for the project.
 *
 * Exposes named loggers per subsystem and convenience macros for call sites.
 */

namespace bomberman::log
{
    /**
    * @brief Initializes all named loggers.
    *
    * @param consoleLevel Runtime level for the console sink.
    * @param logFile Optional rotating file sink path (5 MB x 3 files).
    *
    * Loggers flush automatically on warn-or-above to minimize crash-time tail loss.
    */
#ifndef BOMBERMAN_DEFAULT_LOG_LEVEL
#define BOMBERMAN_DEFAULT_LOG_LEVEL SPDLOG_LEVEL_INFO
#endif

    void init(spdlog::level::level_enum consoleLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL), const std::string& logFile = "");

    spdlog::logger* client();    ///< NetClient, main.cpp
    spdlog::logger* server();    ///< server_main.cpp, handlers
    spdlog::logger* game();      ///< Game.cpp, SDL subsystems
    spdlog::logger* protocol();  ///< PacketDispatch, serialization
} // namespace bomberman::log




// =====================================================================================================================
// Convenience macros
//
// Wrap `SPDLOG_LOGGER_*` with subsystem-specific logger accessors.
// =====================================================================================================================

// ---- Client ----
#define LOG_CLIENT_TRACE(...)  SPDLOG_LOGGER_TRACE(bomberman::log::client(), __VA_ARGS__)
#define LOG_CLIENT_DEBUG(...)  SPDLOG_LOGGER_DEBUG(bomberman::log::client(), __VA_ARGS__)
#define LOG_CLIENT_INFO(...)   SPDLOG_LOGGER_INFO(bomberman::log::client(), __VA_ARGS__)
#define LOG_CLIENT_WARN(...)   SPDLOG_LOGGER_WARN(bomberman::log::client(), __VA_ARGS__)
#define LOG_CLIENT_ERROR(...)  SPDLOG_LOGGER_ERROR(bomberman::log::client(), __VA_ARGS__)

// ---- Server ----
#define LOG_SERVER_TRACE(...)  SPDLOG_LOGGER_TRACE(bomberman::log::server(), __VA_ARGS__)
#define LOG_SERVER_DEBUG(...)  SPDLOG_LOGGER_DEBUG(bomberman::log::server(), __VA_ARGS__)
#define LOG_SERVER_INFO(...)   SPDLOG_LOGGER_INFO(bomberman::log::server(), __VA_ARGS__)
#define LOG_SERVER_WARN(...)   SPDLOG_LOGGER_WARN(bomberman::log::server(), __VA_ARGS__)
#define LOG_SERVER_ERROR(...)  SPDLOG_LOGGER_ERROR(bomberman::log::server(), __VA_ARGS__)

// ---- Game ----
#define LOG_GAME_TRACE(...)    SPDLOG_LOGGER_TRACE(bomberman::log::game(), __VA_ARGS__)
#define LOG_GAME_DEBUG(...)    SPDLOG_LOGGER_DEBUG(bomberman::log::game(), __VA_ARGS__)
#define LOG_GAME_INFO(...)     SPDLOG_LOGGER_INFO(bomberman::log::game(), __VA_ARGS__)
#define LOG_GAME_WARN(...)     SPDLOG_LOGGER_WARN(bomberman::log::game(), __VA_ARGS__)
#define LOG_GAME_ERROR(...)    SPDLOG_LOGGER_ERROR(bomberman::log::game(), __VA_ARGS__)

// ---- Protocol ----
#define LOG_PROTO_TRACE(...)   SPDLOG_LOGGER_TRACE(bomberman::log::protocol(), __VA_ARGS__)
#define LOG_PROTO_DEBUG(...)   SPDLOG_LOGGER_DEBUG(bomberman::log::protocol(), __VA_ARGS__)
#define LOG_PROTO_INFO(...)    SPDLOG_LOGGER_INFO(bomberman::log::protocol(), __VA_ARGS__)
#define LOG_PROTO_WARN(...)    SPDLOG_LOGGER_WARN(bomberman::log::protocol(), __VA_ARGS__)
#define LOG_PROTO_ERROR(...)   SPDLOG_LOGGER_ERROR(bomberman::log::protocol(), __VA_ARGS__)

#endif // BOMBERMAN_UTIL_LOG_H
