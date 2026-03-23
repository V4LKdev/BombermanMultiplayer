/**
 * @file ServerSnapshot.cpp
 * @brief Authoritative snapshot cadence and snapshot message construction.
 */

#include "ServerSnapshot.h"

#include <algorithm>

#include "ServerState.h"

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
            // Death/respawn and temporary invulnerability are not replicated yet.
            entry.flags = net::MsgSnapshot::PlayerEntry::EPlayerFlags::Alive;
        }

        snapshot.playerCount = playerCount;
        return snapshot;
    }
} // namespace bomberman::server
