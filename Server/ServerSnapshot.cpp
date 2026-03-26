/**
 * @file ServerSnapshot.cpp
 * @brief Authoritative snapshot cadence and connected-client snapshot construction.
 */

#include "ServerSnapshot.h"

#include <algorithm>
#include <array>

#include "ServerPowerups.h"
#include "ServerState.h"
#include "Util/Log.h"

namespace bomberman::server
{
    // =================================================================================================================
    // ===== Snapshot Broadcast ========================================================================================
    // =================================================================================================================

    bool shouldBroadcastSnapshot(const ServerState& state)
    {
        return state.snapshotIntervalTicks > 0 &&
               state.serverTick % state.snapshotIntervalTicks == 0;
    }

    net::MsgSnapshot buildSnapshot(const ServerState& state)
    {
        net::MsgSnapshot snapshot{};
        snapshot.matchId = state.currentMatchId;
        snapshot.serverTick = state.serverTick;

        uint8_t playerCount = 0;
        for (uint8_t i = 0; i < net::kMaxPlayers && playerCount < net::kMaxPlayers; ++i)
        {
            const auto& slot = state.matchPlayers[i];
            if (!slot.has_value())
                continue;

            // Pack active match players in stable player-id slot order.
            const auto& matchPlayer = *slot;
            auto& entry = snapshot.players[playerCount++];
            entry.playerId = matchPlayer.playerId;
            entry.xQ = static_cast<int16_t>(std::clamp(matchPlayer.pos.xQ, INT16_MIN, INT16_MAX));
            entry.yQ = static_cast<int16_t>(std::clamp(matchPlayer.pos.yQ, INT16_MIN, INT16_MAX));

            entry.flags = static_cast<net::MsgSnapshot::PlayerEntry::EPlayerFlags>(
                buildReplicatedPlayerFlags(matchPlayer, state.serverTick));
        }

        snapshot.playerCount = playerCount;

        std::array<const BombState*, kServerBombCapacity> activeBombs{};
        std::size_t activeBombCount = 0;
        for (const auto& bombEntry : state.bombs)
        {
            if (!bombEntry.has_value())
                continue;

            activeBombs[activeBombCount++] = &bombEntry.value();
        }

        std::sort(activeBombs.begin(),
                  activeBombs.begin() + static_cast<std::ptrdiff_t>(activeBombCount),
                  [](const BombState* lhs, const BombState* rhs)
                  {
                      if (lhs->cell.row != rhs->cell.row)
                          return lhs->cell.row < rhs->cell.row;

                      return lhs->cell.col < rhs->cell.col;
                  });

        if (activeBombCount > net::kMaxSnapshotBombs &&
            state.serverTick % net::kSnapshotLogEveryN == 0)
        {
            // This should never happen in normal gameplay. If it does, the protocol and snapshot size should be adjusted
            LOG_NET_SNAPSHOT_ERROR("Snapshot bomb overflow tick={} activeBombCount={} snapshotCapacity={} - truncating",
                                  state.serverTick,
                                  activeBombCount,
                                  static_cast<int>(net::kMaxSnapshotBombs));
        }

        const std::size_t packedBombCount = std::min<std::size_t>(activeBombCount, net::kMaxSnapshotBombs);
        for (std::size_t i = 0; i < packedBombCount; ++i)
        {
            const BombState& bomb = *activeBombs[i];
            auto& entry = snapshot.bombs[i];
            entry.ownerId = bomb.ownerId;
            entry.col = bomb.cell.col;
            entry.row = bomb.cell.row;
            entry.radius = bomb.radius;
        }

        snapshot.bombCount = static_cast<uint8_t>(packedBombCount);

        std::array<const PowerupState*, kServerPowerupCapacity> revealedPowerups{};
        std::size_t revealedPowerupCount = 0;
        for (const auto& powerupEntry : state.powerups)
        {
            if (!powerupEntry.has_value() || !powerupEntry->revealed)
                continue;

            revealedPowerups[revealedPowerupCount++] = &powerupEntry.value();
        }

        std::sort(revealedPowerups.begin(),
                  revealedPowerups.begin() + static_cast<std::ptrdiff_t>(revealedPowerupCount),
                  [](const PowerupState* lhs, const PowerupState* rhs)
                  {
                      if (lhs->cell.row != rhs->cell.row)
                          return lhs->cell.row < rhs->cell.row;

                      return lhs->cell.col < rhs->cell.col;
                  });

        for (std::size_t i = 0; i < revealedPowerupCount; ++i)
        {
            const PowerupState& powerup = *revealedPowerups[i];
            auto& entry = snapshot.powerups[i];
            entry.type = powerup.type;
            entry.col = powerup.cell.col;
            entry.row = powerup.cell.row;
        }

        snapshot.powerupCount = static_cast<uint8_t>(revealedPowerupCount);
        return snapshot;
    }
} // namespace bomberman::server
