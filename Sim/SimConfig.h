#ifndef BOMBERMAN_SIM_SIMCONFIG_H
#define BOMBERMAN_SIM_SIMCONFIG_H

namespace bomberman::sim
{
    /// Simulation tick rate in Hz.
    constexpr int16_t kTickRate = 60;

    /// Player movement speed in tiles per second.
    constexpr double kPlayerSpeedTilesPerSecond = 4.0;

    /// Player hitbox size as a fraction of the tile size (0.5 = half a tile wide).
    /// Used by both the Q8 sim and the SDL float collision system.
    constexpr float kPlayerHitboxScale = 0.5f;

    /// Maximum milliseconds the fixed-step accumulator will simulate in one frame.
    constexpr int32_t kMaxFrameClampMs = 250;

    /// Maximum simulation steps taken within a single frame even if the accumulator exceeds the limit.
    constexpr int32_t kMaxStepsPerFrame = 8;

    /// Fixed server-side input delay in ticks.
    constexpr int32_t kServerInputBufferTicks = 0;

} // namespace bomberman::sim

#endif // BOMBERMAN_SIM_SIMCONFIG_H
