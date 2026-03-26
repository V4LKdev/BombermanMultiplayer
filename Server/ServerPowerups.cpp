/**
 * @file ServerPowerups.cpp
 * @brief Authoritative round powerup placement and reveal logic.
 */

#include "ServerPowerups.h"

#include <algorithm>
#include <optional>
#include <random>
#include <vector>

#include "Util/Log.h"

namespace bomberman::server
{
    namespace
    {
        [[nodiscard]]
        uint32_t refreshedEffectUntilTick(const uint32_t currentUntilTick,
                                          const uint32_t serverTick,
                                          const uint32_t durationTicks)
        {
            return std::max(currentUntilTick, serverTick + durationTicks);
        }

        [[nodiscard]]
        std::optional<BombCell> playerCenterCell(const MatchPlayerState& matchPlayer)
        {
            const int32_t col = matchPlayer.pos.xQ / 256;
            const int32_t row = matchPlayer.pos.yQ / 256;
            if (col < 0 || row < 0 ||
                col >= static_cast<int32_t>(tileArrayWidth) ||
                row >= static_cast<int32_t>(tileArrayHeight))
            {
                return std::nullopt;
            }

            return BombCell{
                static_cast<uint8_t>(col),
                static_cast<uint8_t>(row)
            };
        }
    } // namespace

    bool hasInvincibility(const MatchPlayerState& matchPlayer, const uint32_t serverTick)
    {
        return matchPlayer.invincibleUntilTick > serverTick;
    }

    bool hasSpeedBoost(const MatchPlayerState& matchPlayer, const uint32_t serverTick)
    {
        return matchPlayer.speedBoostUntilTick > serverTick;
    }

    bool hasBombRangeBoost(const MatchPlayerState& matchPlayer, const uint32_t serverTick)
    {
        return matchPlayer.bombRangeBoostUntilTick > serverTick;
    }

    bool hasMaxBombsBoost(const MatchPlayerState& matchPlayer, const uint32_t serverTick)
    {
        return matchPlayer.maxBombsBoostUntilTick > serverTick;
    }

    void refreshMatchPlayerPowerupLoadout(const ServerState& state, MatchPlayerState& matchPlayer)
    {
        matchPlayer.maxBombs = hasMaxBombsBoost(matchPlayer, state.serverTick)
            ? sim::kBoostedMaxBombs
            : sim::kDefaultPlayerMaxBombs;

        matchPlayer.bombRange = hasBombRangeBoost(matchPlayer, state.serverTick)
            ? static_cast<uint8_t>(sim::kDefaultPlayerBombRange + sim::kBombRangeBoostAmount)
            : sim::kDefaultPlayerBombRange;
    }

    uint8_t buildReplicatedPlayerFlags(const MatchPlayerState& matchPlayer, const uint32_t serverTick)
    {
        using PlayerFlags = net::MsgSnapshot::PlayerEntry::EPlayerFlags;

        uint8_t flags = 0;
        if (matchPlayer.alive)
        {
            flags |= static_cast<uint8_t>(PlayerFlags::Alive);
        }
        if (matchPlayer.inputLocked)
        {
            flags |= static_cast<uint8_t>(PlayerFlags::InputLocked);
        }
        if (hasInvincibility(matchPlayer, serverTick))
        {
            flags |= static_cast<uint8_t>(PlayerFlags::Invulnerable);
        }
        if (hasSpeedBoost(matchPlayer, serverTick))
        {
            flags |= static_cast<uint8_t>(PlayerFlags::SpeedBoost);
        }
        if (hasBombRangeBoost(matchPlayer, serverTick))
        {
            flags |= static_cast<uint8_t>(PlayerFlags::BombRangeBoost);
        }
        if (hasMaxBombsBoost(matchPlayer, serverTick))
        {
            flags |= static_cast<uint8_t>(PlayerFlags::MaxBombsBoost);
        }

        return flags;
    }

    void clearRoundPowerups(ServerState& state)
    {
        for (auto& entry : state.powerups)
            entry.reset();
    }

    void placeRoundPowerups(ServerState& state)
    {
        clearRoundPowerups(state);
        if (!state.powersEnabled)
            return;

        std::vector<BombCell> candidateBrickCells;
        candidateBrickCells.reserve(tileArrayWidth * tileArrayHeight);

        for (uint8_t row = 0; row < tileArrayHeight; ++row)
        {
            for (uint8_t col = 0; col < tileArrayWidth; ++col)
            {
                if (state.tiles[row][col] == Tile::Brick)
                    candidateBrickCells.push_back(BombCell{col, row});
            }
        }

        if (candidateBrickCells.size() < sim::kPowerupsPerRound)
        {
            LOG_SERVER_WARN("Round powerup placement skipped matchId={} candidateBricks={} required={}",
                            state.currentMatchId,
                            candidateBrickCells.size(),
                            static_cast<unsigned int>(sim::kPowerupsPerRound));
            return;
        }

        const uint32_t placementSeed =
            state.mapSeed ^ (state.currentMatchId * sim::kPowerupPlacementSeedSalt);
        std::mt19937 rng(placementSeed);
        std::shuffle(candidateBrickCells.begin(), candidateBrickCells.end(), rng);

        for (std::size_t i = 0; i < state.powerups.size(); ++i)
        {
            auto& entry = state.powerups[i];
            entry.emplace();
            entry->type = sim::kRoundPowerupPlacements[i];
            entry->cell = candidateBrickCells[i];
            entry->revealed = false;
            entry->revealedTick = 0;
        }

        LOG_SERVER_INFO("Prepared hidden round powerups matchId={} count={}",
                        state.currentMatchId,
                        static_cast<unsigned int>(state.powerups.size()));
    }

    void revealPowerupsUnderDestroyedBricks(ServerState& state, const std::span<const BombCell> destroyedBricks)
    {
        if (!state.powersEnabled || destroyedBricks.empty())
            return;

        for (auto& powerupEntry : state.powerups)
        {
            if (!powerupEntry.has_value() || powerupEntry->revealed)
                continue;

            for (const BombCell cell : destroyedBricks)
            {
                if (powerupEntry->cell.col != cell.col || powerupEntry->cell.row != cell.row)
                    continue;

                powerupEntry->revealed = true;
                powerupEntry->revealedTick = state.serverTick;
                LOG_SERVER_INFO("Powerup revealed type={} tick={} cell=({}, {})",
                                sim::powerupTypeName(powerupEntry->type),
                                state.serverTick,
                                static_cast<int>(powerupEntry->cell.col),
                                static_cast<int>(powerupEntry->cell.row));
                break;
            }
        }
    }

    void collectRevealedPowerups(ServerState& state)
    {
        if (!state.powersEnabled || state.phase != ServerPhase::InMatch)
            return;

        for (auto& powerupEntry : state.powerups)
        {
            if (!powerupEntry.has_value() ||
                !powerupEntry->revealed ||
                powerupEntry->revealedTick >= state.serverTick)
            {
                continue;
            }

            std::optional<uint8_t> collectorId{};
            for (uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
            {
                const auto& matchEntry = state.matchPlayers[playerId];
                if (!matchEntry.has_value())
                    continue;

                const MatchPlayerState& matchPlayer = matchEntry.value();
                if (!matchPlayer.alive || matchPlayer.inputLocked)
                    continue;

                const auto centerCell = playerCenterCell(matchPlayer);
                if (!centerCell.has_value())
                    continue;

                if (centerCell->col != powerupEntry->cell.col || centerCell->row != powerupEntry->cell.row)
                    continue;

                collectorId = playerId;
                break;
            }

            if (!collectorId.has_value())
                continue;

            auto& collector = state.matchPlayers[collectorId.value()].value();
            uint32_t effectUntilTick = 0;
            switch (powerupEntry->type)
            {
                case sim::PowerupType::Invincibility:
                    collector.invincibleUntilTick =
                        refreshedEffectUntilTick(collector.invincibleUntilTick,
                                                 state.serverTick,
                                                 sim::powerupEffectDurationTicks(powerupEntry->type));
                    effectUntilTick = collector.invincibleUntilTick;
                    break;
                case sim::PowerupType::SpeedBoost:
                    collector.speedBoostUntilTick =
                        refreshedEffectUntilTick(collector.speedBoostUntilTick,
                                                 state.serverTick,
                                                 sim::powerupEffectDurationTicks(powerupEntry->type));
                    effectUntilTick = collector.speedBoostUntilTick;
                    break;
                case sim::PowerupType::BombRangeBoost:
                    collector.bombRangeBoostUntilTick =
                        refreshedEffectUntilTick(collector.bombRangeBoostUntilTick,
                                                 state.serverTick,
                                                 sim::powerupEffectDurationTicks(powerupEntry->type));
                    effectUntilTick = collector.bombRangeBoostUntilTick;
                    break;
                case sim::PowerupType::MaxBombsBoost:
                    collector.maxBombsBoostUntilTick =
                        refreshedEffectUntilTick(collector.maxBombsBoostUntilTick,
                                                 state.serverTick,
                                                 sim::powerupEffectDurationTicks(powerupEntry->type));
                    effectUntilTick = collector.maxBombsBoostUntilTick;
                    break;
            }

            refreshMatchPlayerPowerupLoadout(state, collector);

            if (effectUntilTick != 0)
            {
                LOG_SERVER_INFO("Powerup collected playerId={} type={} tick={} cell=({}, {}) untilTick={}",
                                static_cast<int>(collectorId.value()),
                                sim::powerupTypeName(powerupEntry->type),
                                state.serverTick,
                                static_cast<int>(powerupEntry->cell.col),
                                static_cast<int>(powerupEntry->cell.row),
                                effectUntilTick);
            }
            else
            {
                LOG_SERVER_INFO("Powerup collected playerId={} type={} tick={} cell=({}, {})",
                                static_cast<int>(collectorId.value()),
                                sim::powerupTypeName(powerupEntry->type),
                                state.serverTick,
                                static_cast<int>(powerupEntry->cell.col),
                                static_cast<int>(powerupEntry->cell.row));
            }

            powerupEntry.reset();
        }
    }
} // namespace bomberman::server
