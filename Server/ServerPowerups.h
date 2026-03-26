/**
 * @file ServerPowerups.h
 * @brief Authoritative hidden/revealed round powerup helpers.
 */

#ifndef BOMBERMAN_SERVER_SERVERPOWERUPS_H
#define BOMBERMAN_SERVER_SERVERPOWERUPS_H

#include <span>

#include "ServerState.h"

namespace bomberman::server
{
    /** @brief Returns true while one player's invincibility timer is active for the current tick. */
    [[nodiscard]]
    bool hasInvincibility(const MatchPlayerState& matchPlayer, uint32_t serverTick);

    /** @brief Returns true while one player's speed-boost timer is active for the current tick. */
    [[nodiscard]]
    bool hasSpeedBoost(const MatchPlayerState& matchPlayer, uint32_t serverTick);

    /** @brief Returns true while one player's bomb-range boost timer is active for the current tick. */
    [[nodiscard]]
    bool hasBombRangeBoost(const MatchPlayerState& matchPlayer, uint32_t serverTick);

    /** @brief Returns true while one player's max-bombs boost timer is active for the current tick. */
    [[nodiscard]]
    bool hasMaxBombsBoost(const MatchPlayerState& matchPlayer, uint32_t serverTick);

    /** @brief Refreshes one match player's derived bomb loadout from active timed powerup effects. */
    void refreshMatchPlayerPowerupLoadout(const ServerState& state, MatchPlayerState& matchPlayer);

    /** @brief Builds the replicated player-flag bitmask used by snapshots and owner corrections. */
    [[nodiscard]]
    uint8_t buildReplicatedPlayerFlags(const MatchPlayerState& matchPlayer, uint32_t serverTick);

    /** @brief Clears all hidden and revealed round powerup state. */
    void clearRoundPowerups(ServerState& state);

    /** @brief Deterministically seeds the current round's hidden powerups under random bricks. */
    void placeRoundPowerups(ServerState& state);

    /** @brief Reveals any hidden powerups whose brick cells were just destroyed. */
    void revealPowerupsUnderDestroyedBricks(ServerState& state, std::span<const BombCell> destroyedBricks);

    /** @brief Removes any revealed powerups collected by active match players on the current tick. */
    void collectRevealedPowerups(ServerState& state);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVER_SERVERPOWERUPS_H
