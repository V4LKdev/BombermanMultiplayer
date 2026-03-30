/**
 * @file PowerupConfig.h
 * @brief Shared multiplayer powerup types and round-setup tuning.
 */

#ifndef BOMBERMAN_SIM_POWERUPCONFIG_H
#define BOMBERMAN_SIM_POWERUPCONFIG_H

#include <array>
#include <cstdint>
#include <string_view>

#include "Sim/SimConfig.h"

namespace bomberman::sim
{
    /** @brief Number of defined multiplayer powerup types. */
    constexpr std::size_t kPowerupTypeCount = 4;

    /** @brief Supported temporary multiplayer powerup effects. */
    enum class PowerupType : uint8_t
    {
        SpeedBoost = 0,
        Invincibility = 1,
        BombRangeBoost = 2,
        MaxBombsBoost = 3
    };

    /**
     * @brief Stable per-round hidden placements seeded under random bricks.
     *
     * The array length defines how many powerups are hidden in each round.
     * Repeating a type here increases its frequency without needing a separate
     * weighted-RNG system.
     */
    constexpr std::array<PowerupType, 4> kRoundPowerupPlacements{{
        PowerupType::SpeedBoost,
        PowerupType::Invincibility,
        PowerupType::BombRangeBoost,
        PowerupType::MaxBombsBoost
    }};

    /** @brief Number of hidden powerups seeded into each round when enabled. */
    constexpr uint8_t kPowerupsPerRound = static_cast<uint8_t>(kRoundPowerupPlacements.size());

    /** @brief Default temporary powerup lifetime in simulation ticks. */
    constexpr uint32_t kDefaultPowerupDurationTicks = static_cast<uint32_t>(kTickRate) * 10u;

    /** @brief Invincibility lifetime in simulation ticks. */
    constexpr uint32_t kInvincibilityDurationTicks = kDefaultPowerupDurationTicks;

    /** @brief Speed boost lifetime in simulation ticks. */
    constexpr uint32_t kSpeedBoostDurationTicks = kDefaultPowerupDurationTicks;

    /** @brief Bomb-range boost lifetime in simulation ticks. */
    constexpr uint32_t kBombRangeBoostDurationTicks = kDefaultPowerupDurationTicks;

    /** @brief Max-bombs boost lifetime in simulation ticks. */
    constexpr uint32_t kMaxBombsBoostDurationTicks = kDefaultPowerupDurationTicks;

    /** @brief Future movement-speed target while the speed boost is active. */
    constexpr double kSpeedBoostTilesPerSecond = 5.5;

    /** @brief Movement speed in tile-Q8 units per simulation tick while the speed boost is active. */
    constexpr int32_t kSpeedBoostPlayerSpeedQ =
        static_cast<int32_t>(kSpeedBoostTilesPerSecond * 256.0 / kTickRate + 0.5);

    /** @brief Additional blast radius applied to bombs placed during the range boost. */
    constexpr uint8_t kBombRangeBoostAmount = 1;

    /** @brief Bomb-cap target while the max-bombs boost is active. */
    constexpr uint8_t kBoostedMaxBombs = 2;

    /** @brief Deterministic salt mixed into round placement RNG so repeated map seeds can still vary by round. */
    constexpr uint32_t kPowerupPlacementSeedSalt = 0x9E3779B9u;

    static_assert(static_cast<std::size_t>(PowerupType::MaxBombsBoost) + 1u == kPowerupTypeCount,
                  "kPowerupTypeCount must match the defined powerup enum range");

    /** @brief Returns true when a raw byte encodes one of the defined powerup types. */
    constexpr bool isValidPowerupType(const uint8_t rawType)
    {
        return rawType == static_cast<uint8_t>(PowerupType::SpeedBoost) ||
               rawType == static_cast<uint8_t>(PowerupType::Invincibility) ||
               rawType == static_cast<uint8_t>(PowerupType::BombRangeBoost) ||
               rawType == static_cast<uint8_t>(PowerupType::MaxBombsBoost);
    }

    /** @brief Returns a human-readable name for one powerup type. */
    constexpr std::string_view powerupTypeName(const PowerupType type)
    {
        switch (type)
        {
            case PowerupType::SpeedBoost:
                return "SpeedBoost";
            case PowerupType::Invincibility:
                return "Invincibility";
            case PowerupType::BombRangeBoost:
                return "BombRangeBoost";
            case PowerupType::MaxBombsBoost:
                return "MaxBombsBoost";
        }

        return "Unknown";
    }

    /** @brief Returns the stable array index used for per-type powerup stats. */
    constexpr std::size_t powerupTypeIndex(const PowerupType type)
    {
        return static_cast<std::size_t>(type);
    }

    /** @brief Returns the configured lifetime for one powerup effect. */
    constexpr uint32_t powerupEffectDurationTicks(const PowerupType type)
    {
        switch (type)
        {
            case PowerupType::SpeedBoost:
                return kSpeedBoostDurationTicks;
            case PowerupType::Invincibility:
                return kInvincibilityDurationTicks;
            case PowerupType::BombRangeBoost:
                return kBombRangeBoostDurationTicks;
            case PowerupType::MaxBombsBoost:
                return kMaxBombsBoostDurationTicks;
        }

        return 0;
    }
} // namespace bomberman::sim

#endif // BOMBERMAN_SIM_POWERUPCONFIG_H
