/**
 * @file SimConfig.h
 * @brief Shared simulation tuning constants used by client and server gameplay code.
 */

#ifndef BOMBERMAN_SIM_SIMCONFIG_H
#define BOMBERMAN_SIM_SIMCONFIG_H

#include <cstdint>

namespace bomberman::sim
{
    /** @brief Simulation tick rate in Hz. */
    constexpr int16_t kTickRate = 60;

    /** @brief Default number of simultaneously active bombs a player may own. */
    constexpr uint8_t kDefaultPlayerMaxBombs = 1;

    /** @brief Default explosion radius in tiles for newly placed bombs. */
    constexpr uint8_t kDefaultPlayerBombRange = 1;

    /** @brief Default authoritative bomb fuse in simulation ticks. */
    constexpr uint32_t kDefaultBombFuseTicks = static_cast<uint32_t>(kTickRate) * 3u / 2u;

    /** @brief Player movement speed in tiles per second. */
    constexpr double kPlayerSpeedTilesPerSecond = 4.0;

    /**
     * @brief Player hitbox size as a fraction of the tile size.
     *
     * Used by both the Q8 simulation and the SDL float collision system.
     */
    constexpr float kPlayerHitboxScale = 0.5f;

    /** @brief Maximum milliseconds the fixed-step accumulator will simulate in one frame. */
    constexpr int32_t kMaxFrameClampMs = 250;

    /** @brief Maximum simulation steps taken within a single frame even if the accumulator exceeds the limit. */
    constexpr int32_t kMaxStepsPerFrame = 8;

    /** @brief Default server-side input lead in ticks for authoritative input scheduling. */
    constexpr int32_t kDefaultServerInputLeadTicks = 1;

    /** @brief Default server snapshot interval in simulation ticks (1 = every tick / 60 Hz). */
    constexpr int32_t kDefaultServerSnapshotIntervalTicks = 1;

} // namespace bomberman::sim

#endif // BOMBERMAN_SIM_SIMCONFIG_H
