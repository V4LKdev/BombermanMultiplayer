/**
 * @file ServerFlow.h
 * @brief Authoritative lobby-to-match flow helpers.
 */

#ifndef BOMBERMAN_SERVERFLOW_H
#define BOMBERMAN_SERVERFLOW_H

#include <cstdint>
#include <optional>

namespace bomberman::server
{
    struct ServerState;

    /** @brief Resets round-scoped runtime state and returns the dedicated server to the lobby phase. */
    void resetRoundRuntimeToLobby(ServerState& state);

    /** @brief Reacts to one authoritative lobby ready-state change and updates the lobby countdown/start flow. */
    void handleLobbyReadyStateChanged(ServerState& state);

    /** @brief Applies the lobby participant-change policy after one accepted player joins during lobby flow. */
    void handleAcceptedPlayerJoined(ServerState& state);

    /** @brief Starts the next match bootstrap from an already-authorized lobby countdown commit. */
    bool beginMatchBootstrap(ServerState& state);

    /** @brief Advances non-gameplay server flow state such as starting-match timeouts. */
    void advanceServerFlow(ServerState& state);

    /** @brief Records that one match participant has loaded the current bootstrap and may start the round if complete. */
    void markPlayerLoadedForCurrentMatch(ServerState& state, uint8_t playerId);

    /** @brief Updates phase-owned flow state after one accepted player disconnects. */
    void handleAcceptedPlayerReleased(ServerState& state, uint8_t playerId);

    /** @brief Commits authoritative end-of-match state, result messaging, and return timing for the current round. */
    void beginEndOfMatch(ServerState& state,
                         std::optional<uint8_t> winnerPlayerId,
                         bool draw,
                         uint8_t activePlayerCount,
                         uint8_t alivePlayerCount);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVERFLOW_H
