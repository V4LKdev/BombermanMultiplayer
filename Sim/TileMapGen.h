#ifndef BOMBERMAN_SIM_TILEMAPGEN_H
#define BOMBERMAN_SIM_TILEMAPGEN_H

#include <random>

#include "Const.h"
#include "Sim/SpawnSlots.h"

/**
 * @file Sim/TileMapGen.h
 * @brief Deterministic tile map generation shared by client and server.
 *
 * Usage:
 *   uint32_t seed = ...;
 *   Tile map[tileArrayHeight][tileArrayWidth];
 *   bomberman::sim::generateTileMap(seed, map);
 *
 * The seed is chosen by the server and transmitted to the client over MsgLevelInfo at the start of the game.
 */

namespace bomberman::sim
{
    /**
     * @brief Populates `outTiles` with a procedurally generated tile map.
     *
     * @param seed      32-bit seed for the internal std::mt19937 PRNG.
     * @param outTiles  Output array, must be [tileArrayHeight][tileArrayWidth].
     */
    inline void generateTileMap(uint32_t seed, Tile outTiles[tileArrayHeight][tileArrayWidth])
    {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> brickDist(0, brickSpawnRandomize);

        for (int row = 0; row < static_cast<int>(tileArrayHeight); ++row)
        {
            for (int col = 0; col < static_cast<int>(tileArrayWidth); ++col)
            {
                outTiles[row][col] = baseTiles[row][col];

                // Randomly promote Grass to Brick.
                if (outTiles[row][col] == Tile::Grass && brickDist(rng) == 0)
                {
                    outTiles[row][col] = Tile::Brick;
                }
            }
        }

        for (const auto& cell : kProtectedSpawnClearCells)
        {
            outTiles[cell.row][cell.col] = Tile::EmptyGrass;
        }
    }

} // namespace bomberman::sim

#endif // BOMBERMAN_SIM_TILEMAPGEN_H


