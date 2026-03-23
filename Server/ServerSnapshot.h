#ifndef BOMBERMAN_SERVERSNAPSHOT_H
#define BOMBERMAN_SERVERSNAPSHOT_H

#include "Net/NetCommon.h"

/**
 * @file ServerSnapshot.h
 * @brief Authoritative snapshot cadence and snapshot message construction helpers.
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
     * @brief Builds a `MsgSnapshot` from the current authoritative in-match server state.
     *
     * Packs only active `matchPlayers` into the snapshot payload.
     */
    [[nodiscard]]
    net::MsgSnapshot buildSnapshot(const ServerState& state);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSNAPSHOT_H
