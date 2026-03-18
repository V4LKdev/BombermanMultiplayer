/**
 * @file ServerSnapshot.h
 * @brief Authoritative snapshot cadence and snapshot message construction helpers.
 */

#ifndef BOMBERMAN_SERVERSNAPSHOT_H
#define BOMBERMAN_SERVERSNAPSHOT_H

#include "Net/NetCommon.h"

namespace bomberman::server
{
    struct ServerState;

    /** @brief Returns true when the current server tick should broadcast a snapshot. */
    [[nodiscard]]
    bool shouldBroadcastSnapshot(const ServerState& state);

    /** @brief Builds a `MsgSnapshot` from the current authoritative in-match server state. */
    [[nodiscard]]
    net::MsgSnapshot buildSnapshot(const ServerState& state);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSNAPSHOT_H
