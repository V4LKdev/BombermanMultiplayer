#ifndef BOMBERMAN_SIM_SIMCONFIG_H
#define BOMBERMAN_SIM_SIMCONFIG_H

#include <cstdint>

namespace bomberman::sim
{
    /// Simulation tick rate in Hz.
    constexpr int16_t kTickRate = 60;

    /// Default number of simultaneously active bombs a player may own.
    constexpr uint8_t kDefaultPlayerMaxBombs = 1;

    /// Default explosion radius in tiles for newly placed bombs.
    constexpr uint8_t kDefaultPlayerBombRange = 1;

    /// Default authoritative bomb fuse in simulation ticks.
    constexpr uint32_t kDefaultBombFuseTicks = static_cast<uint32_t>(kTickRate) * 3u / 2u;

    /// Player movement speed in tiles per second.
    constexpr double kPlayerSpeedTilesPerSecond = 4.0;

    /// Player hitbox size as a fraction of the tile size (0.5 = half a tile wide).
    /// Used by both the Q8 sim and the SDL float collision system.
    constexpr float kPlayerHitboxScale = 0.5f;

    /// Maximum milliseconds the fixed-step accumulator will simulate in one frame.
    constexpr int32_t kMaxFrameClampMs = 250;

    /// Maximum simulation steps taken within a single frame even if the accumulator exceeds the limit.
    constexpr int32_t kMaxStepsPerFrame = 8;

    /// Default server-side input lead in ticks for authoritative input scheduling.
    constexpr int32_t kDefaultServerInputLeadTicks = 1;

    /// Default server snapshot interval in simulation ticks (1 = every tick / 60 Hz).
    constexpr int32_t kDefaultServerSnapshotIntervalTicks = 1;

} // namespace bomberman::sim

#endif // BOMBERMAN_SIM_SIMCONFIG_H
