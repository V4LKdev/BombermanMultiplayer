/**
 * @file SpawnSlots.h
 * @brief Shared default multiplayer spawn slots.
 */

#ifndef BOMBERMAN_SIM_SPAWNSLOTS_H
#define BOMBERMAN_SIM_SPAWNSLOTS_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "Sim/Movement.h"

namespace bomberman::sim
{
    constexpr uint8_t kLeftSpawnCol = 1;
    constexpr uint8_t kRightSpawnCol = static_cast<uint8_t>(tileArrayWidth - 2);
    constexpr uint8_t kTopSpawnRow = 1;
    constexpr uint8_t kBottomSpawnRow = 9;

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
     * Player ids beyond the four default seats currently wrap this corner table
     * until a dedicated larger-match layout exists.
     */
    constexpr std::array<SpawnSlotCell, 4> kDefaultSpawnSlots{{
        {kLeftSpawnCol, kTopSpawnRow},
        {kLeftSpawnCol, kBottomSpawnRow},
        {kRightSpawnCol, kTopSpawnRow},
        {kRightSpawnCol, kBottomSpawnRow},
    }};

    static_assert(baseTiles[kTopSpawnRow][kLeftSpawnCol] == Tile::EmptyGrass, "Top-left spawn must stay clear");
    static_assert(baseTiles[kBottomSpawnRow][kLeftSpawnCol] == Tile::EmptyGrass, "Bottom-left spawn must stay clear");
    static_assert(baseTiles[kTopSpawnRow][kRightSpawnCol] == Tile::EmptyGrass, "Top-right spawn must stay clear");
    static_assert(baseTiles[kBottomSpawnRow][kRightSpawnCol] == Tile::EmptyGrass, "Bottom-right spawn must stay clear");

    /** @brief Returns the default spawn slot assigned to the given player id. */
    [[nodiscard]]
    constexpr SpawnSlotCell spawnSlotCellForPlayerId(const uint8_t playerId)
    {
        return kDefaultSpawnSlots[static_cast<std::size_t>(playerId) % kDefaultSpawnSlots.size()];
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
