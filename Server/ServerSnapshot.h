#ifndef BOMBERMAN_SERVERSNAPSHOT_H
#define BOMBERMAN_SERVERSNAPSHOT_H

#include "Net/NetCommon.h"

/**
 * @file ServerSnapshot.h
 * @brief Authoritative snapshot cadence and connected-client snapshot construction helpers.
 */

namespace bomberman::server
{
    // ----- Snapshot Broadcast -----

    struct ServerState;

    /**
     * @brief Returns `true` when the current server tick should broadcast a snapshot.
     *
     * @note A snapshot interval of 0 disables snapshot broadcast.
     */
    [[nodiscard]]
    bool shouldBroadcastSnapshot(const ServerState& state);

    /**
     * @brief Builds a `MsgSnapshot` from the current authoritative round state.
     *
     * Packs active `matchPlayers` plus active bombs into the payload.
     *
     * @note This snapshot is intentionally incomplete for mid-match bootstrap:
     * it does not encode tile destruction, round-result state, or enough data
     * to reconstruct the full authoritative world on its own.
     */
    [[nodiscard]]
    net::MsgSnapshot buildSnapshot(const ServerState& state);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSNAPSHOT_H
