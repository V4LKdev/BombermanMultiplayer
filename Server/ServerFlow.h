/**
 * @file ServerFlow.h
 * @ingroup authoritative_server
 * @brief Authoritative lobby-to-match flow helpers.
 */

#ifndef BOMBERMAN_SERVERFLOW_H
#define BOMBERMAN_SERVERFLOW_H

#include <cstdint>
#include <optional>

namespace bomberman::server
{
    struct ServerState;

    /** @brief Resets round-scoped state and returns to the lobby. */
    void resetRoundRuntimeToLobby(ServerState& state);

    /** @brief Reacts to one lobby ready-state change. */
    void handleLobbyReadyStateChanged(ServerState& state);

    /** @brief Refreshes coarse server-flow diagnostics. */
    void refreshServerFlowDiagnostics(ServerState& state);

    /** @brief Applies the lobby participant-change policy after one accepted player joins. */
    void handleAcceptedPlayerJoined(ServerState& state);

    /** @brief Starts match bootstrap from a validated lobby countdown commit. */
    bool beginMatchBootstrap(ServerState& state);

    /** @brief Advances non-gameplay flow state. */
    void advanceServerFlow(ServerState& state);

    /** @brief Records one match-loaded acknowledgement. */
    void markPlayerLoadedForCurrentMatch(ServerState& state, uint8_t playerId);

    /** @brief Updates flow state after one accepted player disconnects. */
    void handleAcceptedPlayerReleased(ServerState& state, uint8_t playerId);

    /** @brief Commits end-of-match state for the current round. */
    void beginEndOfMatch(ServerState& state,
                         std::optional<uint8_t> winnerPlayerId,
                         bool draw,
                         uint8_t activePlayerCount,
                         uint8_t alivePlayerCount);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVERFLOW_H
