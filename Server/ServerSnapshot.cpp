/**
 * @file ServerSnapshot.cpp
 * @brief Authoritative snapshot cadence and snapshot message construction.
 */

#include "ServerSnapshot.h"

#include "ServerState.h"

namespace bomberman::server
{
    bool shouldBroadcastSnapshot(const ServerState& state)
    {
        return state.snapshotIntervalTicks > 0 &&
               (state.serverTick % state.snapshotIntervalTicks) == 0;
    }

    net::MsgSnapshot buildSnapshot(const ServerState& state)
    {
        net::MsgSnapshot msg{};
        msg.serverTick = state.serverTick;

        uint8_t count = 0;
        for (uint8_t i = 0; i < net::kMaxPlayers && count < net::kMaxPlayers; ++i)
        {
            if (!state.matchPlayers[i].has_value())
                continue;

            const auto& matchPlayer = state.matchPlayers[i].value();
            auto& outPlayer = msg.players[count++];
            outPlayer.playerId = matchPlayer.playerId;
            outPlayer.xQ = static_cast<int16_t>(matchPlayer.pos.xQ);
            outPlayer.yQ = static_cast<int16_t>(matchPlayer.pos.yQ);
            outPlayer.flags = net::MsgSnapshot::PlayerEntry::EPlayerFlags::Alive;
        }

        msg.playerCount = count;
        return msg;
    }
} // namespace bomberman::server
