#ifndef BOMBERMAN_SIM_MOVEMENT_H
#define BOMBERMAN_SIM_MOVEMENT_H

#include "Const.h"
#include "Sim/SimConfig.h"

/**
 * @file Sim/Movement.h
 * @brief Shared authoritative movement simulation primitives.
 *
 * All positions are in tile-space Q8 fixed-point and represent the center of the entity:
 *   xQ = tileColumn * 256 + 128,  yQ = tileRow * 256 + 128
 *
 * One Q8 unit = 1/256 of a tile.
 */

/**
 * @brief Shared simulation primitives and constants used by both the client and the authoritative server.
 */
namespace bomberman::sim
{
    /// Player movement speed in tile-Q8 units per simulation tick.
    constexpr int32_t kPlayerSpeedQ = static_cast<int32_t>(kPlayerSpeedTilesPerSecond * 256.0 / kTickRate + 0.5);

    /// Player hitbox half-extent in tile-Q8 units (half a tile at kPlayerHitboxScale=0.5).
    constexpr int32_t kHitboxHalfQ = static_cast<int32_t>(kPlayerHitboxScale * 256.0f / 2.0f);

    /** @brief Tile-space Q8 position representing the CENTER of the entity. */
    struct TilePos
    {
        int32_t xQ = 0;
        int32_t yQ = 0;
    };

    /** @brief Read-only view of a runtime tile map (same layout as LevelScene::tiles). */
    using TileMap = Tile[tileArrayHeight][tileArrayWidth];

    /**
     * @brief Returns true if the given tile coordinate is passable.
     *
     * Stone and Brick are solid. Out-of-bounds is treated as solid (world boundary).
     * Bomb is intentionally excluded here - bomb collision is a gameplay layer above sim.
     */
    [[nodiscard]]
    inline bool isWalkable(const TileMap& map, int col, int row)
    {
        // Out-of-bounds check.
        if (col < 0 || row < 0 ||
            col >= static_cast<int>(tileArrayWidth) ||
            row >= static_cast<int>(tileArrayHeight))
        {
            return false;
        }

        const Tile t = map[row][col];
        return t != Tile::Stone && t != Tile::Brick;
    }

    /**
     * @brief Returns true if the AABB centered at (xQ, yQ) with half-extent kHitboxHalfQ overlaps any solid tile.
     */
    [[nodiscard]]
    inline bool overlapsWall(const TileMap& map, int32_t xQ, int32_t yQ)
    {
        // Convert the four corners of the hitbox to tile indices and check each.
        // Integer divide by 256 gives the tile column/row.
        const int left   = (xQ - kHitboxHalfQ) / 256;
        const int right  = (xQ + kHitboxHalfQ - 1) / 256; // -1 so touching edge is not solid
        const int top    = (yQ - kHitboxHalfQ) / 256;
        const int bottom = (yQ + kHitboxHalfQ - 1) / 256;

        return !isWalkable(map, left,  top)    ||
               !isWalkable(map, right, top)    ||
               !isWalkable(map, left,  bottom) ||
               !isWalkable(map, right, bottom);
    }

    /**
     * @brief Advances a position by one simulation tick given a directional input.
     */
    [[nodiscard]]
    inline TilePos stepMovement(TilePos pos, int8_t moveX, int8_t moveY, int32_t speedQ = kPlayerSpeedQ)
    {
        pos.xQ += moveX * speedQ;
        pos.yQ += moveY * speedQ;
        return pos;
    }

    /**
     * @brief Advances a position by one tick with tile-map collision.
     *
     * Resolves X and Y axes independently so the player slides along walls instead of stopping dead when hitting a corner.
     *
     * @param pos   Current position in tile-Q8.
     * @param moveX Horizontal input in {-1, 0, 1}.
     * @param moveY Vertical input in {-1, 0, 1}.
     * @param map   Read-only tile map to check collision against.
     * @return New position, guaranteed not to overlap any solid tile.
     */
    [[nodiscard]]
    inline TilePos stepMovementWithCollision(TilePos pos,
                                             int8_t moveX,
                                             int8_t moveY,
                                             const TileMap& map,
                                             int32_t speedQ = kPlayerSpeedQ)
    {
        // Try X axis.
        if (moveX != 0)
        {
            const int32_t newXQ = pos.xQ + moveX * speedQ;
            if (!overlapsWall(map, newXQ, pos.yQ))
                pos.xQ = newXQ;
        }

        // Try Y axis.
        if (moveY != 0)
        {
            const int32_t newYQ = pos.yQ + moveY * speedQ;
            if (!overlapsWall(map, pos.xQ, newYQ))
                pos.yQ = newYQ;
        }

        return pos;
    }

    /**
     * @brief Converts a center tile-Q8 coordinate to the screen pixel at that center.
     *
     * Use for debug overlays, hit indicators, etc.
     *
     * @param tileQ      Center position in tile-Q8  (entity center, not top-left).
     * @param fieldPx    Field origin in screen pixels.
     * @param scaledTile Rendered tile size in pixels.
     * @param cameraPx   Camera offset in the same axis.
     * @return Screen pixel coordinate of the center.
     */
    [[nodiscard]]
    inline int tileQToScreen(int32_t tileQ, int fieldPx, int scaledTile, int cameraPx)
    {
        return fieldPx + (tileQ * scaledTile / 256) - cameraPx;
    }

    /**
     * @brief Converts a center tile-Q8 coordinate to the screen top-left pixel for sprite rendering.
     *
     * Sprites are positioned by their top-left corner. Given a center Q8 position, subtract
     * half a tile (128 Q8 units = 0.5 tiles) before converting to get the top-left.
     *
     * @param tileQ      Center position in tile-Q8.
     * @param fieldPx    Field origin in screen pixels.
     * @param scaledTile Rendered tile size in pixels.
     * @param cameraPx   Camera offset in the same axis.
     * @return Screen pixel coordinate of the sprite's top-left corner.
     */
    [[nodiscard]]
    inline int tileQToScreenTopLeft(int32_t tileQ, int fieldPx, int scaledTile, int cameraPx)
    {
        return fieldPx + ((tileQ - 128) * scaledTile / 256) - cameraPx;
    }

} // namespace bomberman::sim

#endif // BOMBERMAN_SIM_MOVEMENT_H
