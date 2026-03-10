#include "ServerSession.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <random>
#include <ranges>

#include "Net/NetSend.h"
#include "Sim/TileMapGen.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    void initServerState(ServerState& state, ENetHost* host, bool overrideMapSeed, uint32_t mapSeed)
    {
        state.host = host;
        state.serverTick = 0;
        state.nextStateSequence = 0;
        state.clients.clear();

        LOG_SERVER_INFO("ServerState initialized");

        // Generate the tile map with either the provided seed or a random seed.
        uint32_t seed = mapSeed;
        if (!overrideMapSeed)
        {
            seed = std::random_device{}();
        }
        state.mapSeed = seed;
        sim::generateTileMap(state.mapSeed, state.tiles);

        if (overrideMapSeed)
            LOG_SERVER_INFO("Map generated with provided seed={}", state.mapSeed);
        else
            LOG_SERVER_INFO("Map generated with random seed={}", state.mapSeed);
    }

    void simulateServerTick(ServerState& state)
    {
        ++state.serverTick;

        if (state.clients.empty())
            return;

        // Advance each player's authoritative position by one tick using the shared sim primitive.
        for (auto& client : state.clients | std::views::values)
        {
            client.pos = sim::stepMovementWithCollision(
                client.pos,
                client.input.moveX,
                client.input.moveY,
                state.tiles);
        }

        // TODO: Only movement is simulated on the server right now

        const auto msgState = buildStateSnapshot(state);
        const auto packetBytes = makeStatePacket(msgState, state.nextStateSequence++, state.serverTick);

        broadcastUnreliable(state.host, packetBytes);
    }

    MsgState buildStateSnapshot(const ServerState& state)
    {
        MsgState msg{};

        // Collect client IDs into a fixed-size stack array and sort for stable ordering.
        std::array<uint32_t, kMaxPlayers> ids{};
        uint8_t count = 0;

        for (const auto& id : state.clients | std::views::keys)
        {
            if (count >= kMaxPlayers)
            {
                LOG_SERVER_WARN("buildStateSnapshot: more clients than kMaxPlayers ({}), truncating", kMaxPlayers);
                // TODO: Should disconnect any additional clients in that case
                break;
            }
            ids[count++] = id;
        }

        // Sorting by client ID gives a stable player order across snapshots and makes it easier to correlate with logs/debug info.
        std::sort(ids.begin(), ids.begin() + count);

        for (uint8_t i = 0; i < count; ++i)
        {
            const uint32_t clientId = ids[i];
            const auto& client = state.clients.at(clientId);

            auto& slot    = msg.players[i];
            slot.clientId = static_cast<uint8_t>(clientId);
            slot.flags    = MsgState::PlayerState::EPlayerFlags::Alive; // TODO: replace with actual alive status

            // Clamp int32 tile-Q8 positions to int16 wire range, should be sufficient for the map sizes
            slot.xQ = static_cast<int16_t>(std::clamp(client.pos.xQ, INT16_MIN, INT16_MAX));
            slot.yQ = static_cast<int16_t>(std::clamp(client.pos.yQ, INT16_MIN, INT16_MAX));
        }

        msg.playerCount = count;
        return msg;
    }

} // namespace bomberman::server
