/**
 * @file ServerBombs.h
 * @ingroup authoritative_server
 * @brief Authoritative server-side bomb placement, explosion, and lifecycle helpers.
 */

#ifndef BOMBERMAN_SERVER_SERVERBOMBS_H
#define BOMBERMAN_SERVER_SERVERBOMBS_H

#include "ServerState.h"

namespace bomberman::server
{
    /**
     * @brief Attempts to place one authoritative bomb for a match player on the current server tick.
     */
    void tryPlaceBomb(ServerState& state, MatchPlayerState& matchPlayer);

    /**
     * @brief Resolves all bombs whose fuse expires on the current authoritative tick.
     */
    void resolveExplodingBombs(ServerState& state);

    /**
     * @brief Clears all active bombs and releases per-player active-bomb ownership counters.
     */
    void clearBombsAndReleaseOwnership(ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVER_SERVERBOMBS_H
