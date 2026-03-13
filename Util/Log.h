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

    spdlog::logger* client();       ///< NetClient, main.cpp
    spdlog::logger* server();       ///< server_main.cpp, handlers
    spdlog::logger* game();         ///< Game.cpp, SDL subsystems
    spdlog::logger* netConn();      ///< Connect/disconnect/handshake lifecycle
    spdlog::logger* netPacket();    ///< Transport queueing, packet dispatch, raw receive paths
    spdlog::logger* netProto();     ///< Serialization/deserialization, protocol versioning
    spdlog::logger* netInput();     ///< Input batching, buffering, gap/drop diagnostics
    spdlog::logger* netSnapshot();  ///< Snapshot/correction send + receive paths
    spdlog::logger* netDiag();      ///< Reserved for future diagnostics/telemetry plumbing
    spdlog::logger* perf();         ///< Performance instrumentation
    spdlog::logger* test();         ///< Test-only helpers/harnesses
    spdlog::logger* protocol();     ///< Backwards-compat alias for netProto()
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

#define LOG_PERF_TRACE(...)          SPDLOG_LOGGER_TRACE(bomberman::log::perf(), __VA_ARGS__)
#define LOG_PERF_DEBUG(...)          SPDLOG_LOGGER_DEBUG(bomberman::log::perf(), __VA_ARGS__)
#define LOG_PERF_INFO(...)           SPDLOG_LOGGER_INFO(bomberman::log::perf(), __VA_ARGS__)
#define LOG_PERF_WARN(...)           SPDLOG_LOGGER_WARN(bomberman::log::perf(), __VA_ARGS__)
#define LOG_PERF_ERROR(...)          SPDLOG_LOGGER_ERROR(bomberman::log::perf(), __VA_ARGS__)

#define LOG_TEST_TRACE(...)          SPDLOG_LOGGER_TRACE(bomberman::log::test(), __VA_ARGS__)
#define LOG_TEST_DEBUG(...)          SPDLOG_LOGGER_DEBUG(bomberman::log::test(), __VA_ARGS__)
#define LOG_TEST_INFO(...)           SPDLOG_LOGGER_INFO(bomberman::log::test(), __VA_ARGS__)
#define LOG_TEST_WARN(...)           SPDLOG_LOGGER_WARN(bomberman::log::test(), __VA_ARGS__)
#define LOG_TEST_ERROR(...)          SPDLOG_LOGGER_ERROR(bomberman::log::test(), __VA_ARGS__)

// ---- Protocol (backwards-compat alias) ----
#define LOG_PROTO_TRACE(...)         LOG_NET_PROTO_TRACE(__VA_ARGS__)
#define LOG_PROTO_DEBUG(...)         LOG_NET_PROTO_DEBUG(__VA_ARGS__)
#define LOG_PROTO_INFO(...)          LOG_NET_PROTO_INFO(__VA_ARGS__)
#define LOG_PROTO_WARN(...)          LOG_NET_PROTO_WARN(__VA_ARGS__)
#define LOG_PROTO_ERROR(...)         LOG_NET_PROTO_ERROR(__VA_ARGS__)

#endif // BOMBERMAN_UTIL_LOG_H
