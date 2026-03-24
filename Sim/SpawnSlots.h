/**
 * @file SpawnSlots.h
 * @brief Shared default multiplayer spawn slots and safe-zone helpers.
 */

#ifndef BOMBERMAN_SIM_SPAWNSLOTS_H
#define BOMBERMAN_SIM_SPAWNSLOTS_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "Sim/Movement.h"

namespace bomberman::sim
{
    /** @brief One multiplayer spawn slot expressed in tile-cell coordinates. */
    struct SpawnSlotCell
    {
        uint8_t col = 0;
        uint8_t row = 0;
    };

    /**
     * @brief Default player-id keyed spawn slots for the current arena layout.
     *
     * These slots assume the existing rectangular border and stone-grid layout.
     * If `net::kMaxPlayers` grows beyond 4, extend this table and its
     * protected clear cells alongside the new protocol capacity.
     */
    constexpr std::array<SpawnSlotCell, 4> kDefaultSpawnSlots{{
        {1, 1},
        {static_cast<uint8_t>(tileArrayWidth - 2), 1},
        {1, static_cast<uint8_t>(tileArrayHeight - 2)},
        {static_cast<uint8_t>(tileArrayWidth - 2), static_cast<uint8_t>(tileArrayHeight - 2)},
    }};

    /**
     * @brief Cells forced clear so each default spawn has a minimal walk-out area.
     *
     * This keeps the current prototype rules simple: each slot guarantees its
     * own tile plus the two immediate orthogonal escape cells.
     */
    constexpr std::array<SpawnSlotCell, 12> kProtectedSpawnClearCells{{
        {1, 1}, {2, 1}, {1, 2},
        {static_cast<uint8_t>(tileArrayWidth - 2), 1},
        {static_cast<uint8_t>(tileArrayWidth - 3), 1},
        {static_cast<uint8_t>(tileArrayWidth - 2), 2},
        {1, static_cast<uint8_t>(tileArrayHeight - 2)},
        {2, static_cast<uint8_t>(tileArrayHeight - 2)},
        {1, static_cast<uint8_t>(tileArrayHeight - 3)},
        {static_cast<uint8_t>(tileArrayWidth - 2), static_cast<uint8_t>(tileArrayHeight - 2)},
        {static_cast<uint8_t>(tileArrayWidth - 3), static_cast<uint8_t>(tileArrayHeight - 2)},
        {static_cast<uint8_t>(tileArrayWidth - 2), static_cast<uint8_t>(tileArrayHeight - 3)},
    }};

    /** @brief Returns the default spawn slot assigned to the given player id. */
    [[nodiscard]]
    constexpr SpawnSlotCell spawnSlotCellForPlayerId(const uint8_t playerId)
    {
        return playerId < kDefaultSpawnSlots.size()
            ? kDefaultSpawnSlots[playerId]
            : kDefaultSpawnSlots.front();
    }

    /** @brief Converts one tile-cell spawn slot to a center-position tile-Q8 coordinate. */
    [[nodiscard]]
    constexpr TilePos spawnSlotTilePos(const SpawnSlotCell slot)
    {
        return {
            static_cast<int32_t>(slot.col) * 256 + 128,
            static_cast<int32_t>(slot.row) * 256 + 128
        };
    }

    /** @brief Returns the default center-position tile-Q8 spawn for the given player id. */
    [[nodiscard]]
    constexpr TilePos spawnTilePosForPlayerId(const uint8_t playerId)
    {
        return spawnSlotTilePos(spawnSlotCellForPlayerId(playerId));
    }

} // namespace bomberman::sim

#endif // BOMBERMAN_SIM_SPAWNSLOTS_H
