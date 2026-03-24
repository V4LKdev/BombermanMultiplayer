/**
 * @file ServerSnapshot.cpp
 * @brief Authoritative snapshot cadence and snapshot message construction.
 */

#include "ServerSnapshot.h"

#include <algorithm>
#include <array>

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

            uint8_t flags = 0;
            if (matchPlayer.alive)
            {
                flags |= static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::Alive);
            }

            // Temporary invulnerability is not replicated yet.
            entry.flags = static_cast<net::MsgSnapshot::PlayerEntry::EPlayerFlags>(flags);
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
            LOG_NET_SNAPSHOT_WARN("Snapshot bomb overflow tick={} activeBombCount={} snapshotCapacity={} - truncating",
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
        return snapshot;
    }
} // namespace bomberman::server
