#include "ServerSession.h"

#include <algorithm>

#include "Net/NetSend.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    void simulateServerTick(ServerState& state)
    {
        ++state.serverTick;

        if (state.inputs.empty())
            return;

        const auto msgState    = buildStateSnapshot(state);
        const auto packetBytes = makeStatePacket(msgState, state.nextStateSequence++, state.serverTick);

        broadcastUnreliable(state.host, packetBytes);
    }

    MsgState buildStateSnapshot(const ServerState& state)
    {
        MsgState msg{};

        // Collect client IDs into a fixed-size stack array and sort for stable ordering.
        // kMaxPlayers is 4, so no heap allocation needed.
        std::array<uint32_t, kMaxPlayers> ids{};
        uint8_t count = 0;
        for (const auto& [id, _] : state.inputs)
        {
            if (count >= kMaxPlayers)
            {
                LOG_SERVER_WARN("buildStateSnapshot: more clients than kMaxPlayers ({}), truncating", kMaxPlayers);
                break;
            }
            ids[count++] = id;
        }
        std::sort(ids.begin(), ids.begin() + count);

        // Compact fill: players[0..count-1] are always densely packed.
        for (uint8_t i = 0; i < count; ++i)
        {
            auto& slot    = msg.players[i];
            slot.clientId = static_cast<uint8_t>(ids[i]);
            slot.xQ       = 0; // TODO: replace with actual position from game state
            slot.yQ       = 0; // TODO: replace with actual position from game state
            slot.flags    = MsgState::PlayerState::EPlayerFlags::Alive; // TODO: replace with actual alive status
        }

        msg.playerCount = count;
        return msg;
    }

} // namespace bomberman::server
