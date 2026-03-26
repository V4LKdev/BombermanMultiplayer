#ifndef BOMBERMAN_UTIL_LOG_H
#define BOMBERMAN_UTIL_LOG_H

#include <string>
#include <unordered_map>
#include <spdlog/spdlog.h>

#ifndef BOMBERMAN_DEFAULT_LOG_LEVEL
#define BOMBERMAN_DEFAULT_LOG_LEVEL SPDLOG_LEVEL_INFO
#endif

/**
 * @brief Centralized logging interface for the project.
 */

namespace bomberman::log
{
    struct LogConfig
    {
        spdlog::level::level_enum baseLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        std::string logFilePath;
        std::unordered_map<std::string, spdlog::level::level_enum> channelLevels;
    };

    /** @brief Builds the hardcoded default logging configuration. */
    LogConfig makeDefaultConfig();

    /** @brief Returns the default logging config path. */
    std::string defaultConfigFilePath();

    /**
     * @brief Resolves the final logging config from defaults, default file input, and CLI-style overrides.
     *
     * Precedence is:
     * 1. hardcoded defaults,
     * 2. default config file if present,
     * 3. explicit base-level override,
     * 4. explicit log-file override.
     */
    bool resolveConfig(LogConfig& outConfig,
                       bool hasBaseLevelOverride,
                       spdlog::level::level_enum baseLevelOverride,
                       bool hasLogFileOverride,
                       const std::string& logFileOverride,
                       std::string& outError);

    /**
     * @brief Initializes all named loggers.
     *
     * @param config Final resolved logging configuration.
     */
    void init(const LogConfig& config);
    /** @brief Convenience overload that initializes from a base level plus optional log file path. */
    void init(spdlog::level::level_enum baseLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL), const std::string& logFile = "");


    spdlog::logger* client();       ///< Client-process bootstrap and top-level configuration.
    spdlog::logger* server();       ///< Dedicated-server process lifecycle and authoritative round-flow milestones.
    spdlog::logger* game();         ///< SDL/game runtime lifecycle outside network semantics.
    spdlog::logger* netConn();      ///< Session lifecycle and user-visible connect/lobby/match admission transitions.
    spdlog::logger* netPacket();    ///< Raw transport queueing/dispatch detail and channel-level anomalies.
    spdlog::logger* netProto();     ///< Malformed payloads, wrong-state protocol use, and version/shape mismatches.
    spdlog::logger* netInput();     ///< Authoritative input acceptance, prediction/replay, and input-stream anomalies.
    spdlog::logger* netSnapshot();  ///< Snapshot/correction and reliable gameplay-event transport/merge detail.
    spdlog::logger* netDiag();      ///< Explicit diagnostics-layer summaries or recorder lifecycle, not normal flow logs.
} // namespace bomberman::log


// =====================================================================================================================
// Convenience macros
//
// Wrap SPDLOG_LOGGER_* with subsystem-specific logger accessors.
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
#define LOG_NET_CONN_TRACE(...)      SPDLOG_LOGGER_TRACE(bomberman::log::netConn(), __VA_ARGS__)
#define LOG_NET_CONN_DEBUG(...)      SPDLOG_LOGGER_DEBUG(bomberman::log::netConn(), __VA_ARGS__)
#define LOG_NET_CONN_INFO(...)       SPDLOG_LOGGER_INFO(bomberman::log::netConn(), __VA_ARGS__)
#define LOG_NET_CONN_WARN(...)       SPDLOG_LOGGER_WARN(bomberman::log::netConn(), __VA_ARGS__)
#define LOG_NET_CONN_ERROR(...)      SPDLOG_LOGGER_ERROR(bomberman::log::netConn(), __VA_ARGS__)

#define LOG_NET_PACKET_TRACE(...)    SPDLOG_LOGGER_TRACE(bomberman::log::netPacket(), __VA_ARGS__)
#define LOG_NET_PACKET_DEBUG(...)    SPDLOG_LOGGER_DEBUG(bomberman::log::netPacket(), __VA_ARGS__)
#define LOG_NET_PACKET_INFO(...)     SPDLOG_LOGGER_INFO(bomberman::log::netPacket(), __VA_ARGS__)
#define LOG_NET_PACKET_WARN(...)     SPDLOG_LOGGER_WARN(bomberman::log::netPacket(), __VA_ARGS__)
#define LOG_NET_PACKET_ERROR(...)    SPDLOG_LOGGER_ERROR(bomberman::log::netPacket(), __VA_ARGS__)

#define LOG_NET_PROTO_TRACE(...)     SPDLOG_LOGGER_TRACE(bomberman::log::netProto(), __VA_ARGS__)
#define LOG_NET_PROTO_DEBUG(...)     SPDLOG_LOGGER_DEBUG(bomberman::log::netProto(), __VA_ARGS__)
#define LOG_NET_PROTO_INFO(...)      SPDLOG_LOGGER_INFO(bomberman::log::netProto(), __VA_ARGS__)
#define LOG_NET_PROTO_WARN(...)      SPDLOG_LOGGER_WARN(bomberman::log::netProto(), __VA_ARGS__)
#define LOG_NET_PROTO_ERROR(...)     SPDLOG_LOGGER_ERROR(bomberman::log::netProto(), __VA_ARGS__)

#define LOG_NET_INPUT_TRACE(...)     SPDLOG_LOGGER_TRACE(bomberman::log::netInput(), __VA_ARGS__)
#define LOG_NET_INPUT_DEBUG(...)     SPDLOG_LOGGER_DEBUG(bomberman::log::netInput(), __VA_ARGS__)
#define LOG_NET_INPUT_INFO(...)      SPDLOG_LOGGER_INFO(bomberman::log::netInput(), __VA_ARGS__)
#define LOG_NET_INPUT_WARN(...)      SPDLOG_LOGGER_WARN(bomberman::log::netInput(), __VA_ARGS__)
#define LOG_NET_INPUT_ERROR(...)     SPDLOG_LOGGER_ERROR(bomberman::log::netInput(), __VA_ARGS__)

#define LOG_NET_SNAPSHOT_TRACE(...)  SPDLOG_LOGGER_TRACE(bomberman::log::netSnapshot(), __VA_ARGS__)
#define LOG_NET_SNAPSHOT_DEBUG(...)  SPDLOG_LOGGER_DEBUG(bomberman::log::netSnapshot(), __VA_ARGS__)
#define LOG_NET_SNAPSHOT_INFO(...)   SPDLOG_LOGGER_INFO(bomberman::log::netSnapshot(), __VA_ARGS__)
#define LOG_NET_SNAPSHOT_WARN(...)   SPDLOG_LOGGER_WARN(bomberman::log::netSnapshot(), __VA_ARGS__)
#define LOG_NET_SNAPSHOT_ERROR(...)  SPDLOG_LOGGER_ERROR(bomberman::log::netSnapshot(), __VA_ARGS__)

#define LOG_NET_DIAG_TRACE(...)      SPDLOG_LOGGER_TRACE(bomberman::log::netDiag(), __VA_ARGS__)
#define LOG_NET_DIAG_DEBUG(...)      SPDLOG_LOGGER_DEBUG(bomberman::log::netDiag(), __VA_ARGS__)
#define LOG_NET_DIAG_INFO(...)       SPDLOG_LOGGER_INFO(bomberman::log::netDiag(), __VA_ARGS__)
#define LOG_NET_DIAG_WARN(...)       SPDLOG_LOGGER_WARN(bomberman::log::netDiag(), __VA_ARGS__)
#define LOG_NET_DIAG_ERROR(...)      SPDLOG_LOGGER_ERROR(bomberman::log::netDiag(), __VA_ARGS__)

#endif // BOMBERMAN_UTIL_LOG_H
