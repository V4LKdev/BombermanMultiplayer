#ifndef BOMBERMAN_SERVERSNAPSHOT_H
#define BOMBERMAN_SERVERSNAPSHOT_H

#include "Net/NetCommon.h"

/**
 * @file ServerSnapshot.h
 * @ingroup authoritative_server
 * @brief Authoritative snapshot cadence and connected-client snapshot construction helpers.
 */

namespace bomberman::server
{
    // ----- Snapshot Broadcast -----

    struct ServerState;

    /** @brief Returns `true` when the current tick should broadcast a snapshot. */
    [[nodiscard]]
    bool shouldBroadcastSnapshot(const ServerState& state);

    /**
     * @brief Builds a `MsgSnapshot` from the current round state.
     *
     * Packs active `matchPlayers`, bombs, and revealed powerups into the payload.
     */
    [[nodiscard]]
    net::MsgSnapshot buildSnapshot(const ServerState& state);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSNAPSHOT_H
