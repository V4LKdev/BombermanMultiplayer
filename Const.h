#ifndef BOMBERMAN_CONST_H
#define BOMBERMAN_CONST_H

#include <cstdint>

/**
 * @file Const.h
 * @brief Game-world configuration constants.
 */

/// @brief Tile type identifiers used in the tile map.
enum class Tile : uint8_t
{
    Stone,
    Grass,
    Brick,
    EmptyGrass,
    Bomb,
    Bang
};

namespace bomberman
{
    constexpr int playerStartX = 1; ///< Player spawn tile column.
    constexpr int playerStartY = 1; ///< Player spawn tile row.

    constexpr unsigned int brickSpawnRandomize = 10; ///< Brick density — lower value means more bricks.
    constexpr unsigned int doorSpawnRandomize  = 10; ///< Door placement — lower value means door spawns further in.
    constexpr unsigned int bangSpawnCells      = 5;  ///< Number of cells in the bang spread pattern.
    constexpr unsigned int minEnemiesOnLevel   = 2;
    constexpr unsigned int maxEnemiesOnLevel   = 10;

    constexpr int bangSpawnPositions[bangSpawnCells][2] = {
        { 0,  0},
        { 0,  1},
        { 1,  0},
        { 0, -1},
        {-1,  0}
    };
    static_assert(bangSpawnCells == 5, "bangSpawnPositions row count must match bangSpawnCells");

    constexpr unsigned int tileArrayWidth  = 31; ///< Tile map width in tiles.
    constexpr unsigned int tileArrayHeight = 13; ///< Tile map height in tiles.
    constexpr unsigned int tileSize        = 16; ///< Source sprite size in pixels (not scaled render size).

    constexpr Tile baseTiles[tileArrayHeight][tileArrayWidth] = {
        {Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone,
            Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone,
            Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone,
            Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone},
        {Tile::Stone, Tile::EmptyGrass, Tile::EmptyGrass, Tile::EmptyGrass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass,      Tile::Grass,      Tile::Grass,      Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass,      Tile::Grass,      Tile::Grass,      Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass,      Tile::Grass,      Tile::Grass,      Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass,      Tile::Stone},
        {Tile::Stone, Tile::EmptyGrass, Tile::Stone, Tile::EmptyGrass, Tile::Stone, Tile::Grass, Tile::Stone,
            Tile::Grass, Tile::Stone,      Tile::Grass, Tile::Stone,      Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass,      Tile::Stone, Tile::Grass,      Tile::Stone, Tile::Grass, Tile::Stone,
            Tile::Grass, Tile::Stone,      Tile::Grass, Tile::Stone,      Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass,      Tile::Stone},
        {Tile::Stone, Tile::EmptyGrass, Tile::EmptyGrass, Tile::EmptyGrass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass,      Tile::Grass,      Tile::Grass,      Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass,      Tile::Grass,      Tile::Grass,      Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass,      Tile::Grass,      Tile::Grass,      Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass,      Tile::Stone},
        {Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone},
        {Tile::Stone, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Brick, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Stone},
        {Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone},
        {Tile::Stone, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Stone},
        {Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass,
            Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone, Tile::Grass, Tile::Stone},
        {Tile::Stone, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass,
            Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Grass, Tile::Stone},
        {Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone,
            Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone,
            Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone,
            Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone, Tile::Stone}
    };
} // namespace bomberman

#endif // BOMBERMAN_CONST_H
