#include "ServerSession.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <random>

#include "Net/NetSend.h"
#include "Sim/TileMapGen.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    void initServerState(ServerState& state, ENetHost* host, bool diagEnabled, bool overrideMapSeed, uint32_t mapSeed)
    {
        state.host = host;
        state.serverTick = 0;

        // Reset all client slots.
        for (auto& slot : state.clients)
            slot.reset();

        // Initialize player ID pool.
        for (uint8_t i = 0; i < kMaxPlayers; ++i)
            state.playerIdPool[i] = i;

        state.playerIdPoolSize = kMaxPlayers;

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

        state.diag.beginSession("server", diagEnabled);
    }

    void simulateServerTick(ServerState& state)
    {
        ++state.serverTick;
        state.diag.advanceTick();

        bool anyClient = false;

        for (auto& slot : state.clients)
        {
            if (!slot.has_value())
                continue;

            anyClient = true;
            auto& client = slot.value();

            // ---- Consume next input from ring buffer ----
            if (client.lastReceivedInputSeq > 0)
            {
                // Client has sent at least one input - advance consumed seq unconditionally.
                const uint32_t nextSeq = client.lastConsumedInputSeq + 1;
                const std::size_t seqSlot = nextSeq % kServerInputBufferSize;

                auto& entry = client.inputRing[seqSlot];
                if (entry.valid && entry.seq == nextSeq)
                {
                    // Exact match - consume this command.
                    client.currentButtons  = entry.buttons;
                    client.previousButtons = client.currentButtons;
                    entry.valid = false; // mark consumed
                    client.consecutiveInputGaps = 0;
                }
                else
                {
                    // Gap or stale slot: hold previous buttons.
                    client.currentButtons = client.previousButtons;
                    ++client.consecutiveInputGaps;

                    // Detection lives here; telemetry goes to diag.
                    state.diag.recordInputAnomaly(net::NetInputAnomalyType::Gap,
                                                  client.playerId, nextSeq, client.previousButtons, "hold");

                    if (client.consecutiveInputGaps >= kRepeatedInputWarnThreshold
                        && state.serverTick >= client.nextGapWarnTick)
                    {
                        LOG_NET_INPUT_WARN(
                            "Repeated input gaps playerId={} streak={} expectedSeq={} slotValid={} slotSeq={} using previousButtons=0x{:02x} lastRecv={} lastConsumed={}",
                            client.playerId, client.consecutiveInputGaps,
                            nextSeq,
                            entry.valid ? 1 : 0, entry.seq,
                            client.previousButtons,
                            client.lastReceivedInputSeq, client.lastConsumedInputSeq);

                        client.nextGapWarnTick = state.serverTick + kRepeatedInputWarnCooldownTicks;
                    }
                }

                client.lastConsumedInputSeq = nextSeq;
            }
            else
            {
                // No input ever received from this client - idle.
                client.currentButtons = 0;
            }

            // Derive movement from the button bitmask.
            const int8_t moveX = buttonsToMoveX(client.currentButtons);
            const int8_t moveY = buttonsToMoveY(client.currentButtons);

            client.pos = sim::stepMovementWithCollision(
                client.pos, moveX, moveY, state.tiles);

            if ((state.serverTick % kPeerSampleTicks) == 0 && client.peer != nullptr)
            {
                const uint32_t queuedReliable = client.peer->reliableDataInTransit;
                const uint32_t totalWaiting = static_cast<uint32_t>(client.peer->totalWaitingData);
                const uint32_t queuedUnreliable = (totalWaiting > queuedReliable)
                    ? (totalWaiting - queuedReliable)
                    : 0;
                const uint32_t packetLossPermille = static_cast<uint32_t>(
                    (static_cast<uint64_t>(client.peer->packetLoss) * 1000u) / ENET_PEER_PACKET_LOSS_SCALE);

                state.diag.samplePeer(client.playerId,
                                      client.peer->roundTripTime,
                                      client.peer->roundTripTimeVariance,
                                      packetLossPermille,
                                      queuedReliable,
                                      queuedUnreliable);
            }
        }

        // TODO: Only movement is simulated on the server right now

        // ---- Construct and Send snapshot ----
        if (!anyClient)
            return;

        const auto snapshot = buildSnapshot(state);
        const auto snapshotBytes = makeSnapshotPacket(snapshot);
        if ((state.serverTick % kServerSnapshotLogIntervalTicks) == 0)
        {
            LOG_NET_SNAPSHOT_DEBUG("Snapshot tick={} playerCount={}", snapshot.serverTick, snapshot.playerCount);
        }

        for (const auto& slot : state.clients)
        {
            if (!slot.has_value() || slot->peer == nullptr)
                continue;

            const bool queued = queueUnreliableGame(slot->peer, snapshotBytes);
            state.diag.recordPacketSent(EMsgType::Snapshot,
                                        slot->playerId,
                                        static_cast<uint8_t>(EChannel::GameUnreliable),
                                        kPacketHeaderSize + kMsgSnapshotSize,
                                        queued ? NetPacketResult::Ok : NetPacketResult::Dropped);
        }

        flush(state.host);
    }

    MsgSnapshot buildSnapshot(const ServerState& state)
    {
        MsgSnapshot msg{};
        msg.serverTick = state.serverTick;

        uint8_t count = 0;
        for (uint8_t i = 0; i < kMaxPlayers && count < kMaxPlayers; ++i)
        {
            if (!state.clients[i].has_value())
                continue;

            const auto& client = state.clients[i].value();
            auto& slot = msg.players[count];

            slot.playerId = client.playerId;
            slot.flags = MsgSnapshot::PlayerEntry::EPlayerFlags::Alive; // TODO: replace with actual alive status
            // Clamp int32 tile-Q8 positions to int16 wire range.
            slot.xQ = static_cast<int16_t>(std::clamp(client.pos.xQ, INT16_MIN, INT16_MAX));
            slot.yQ = static_cast<int16_t>(std::clamp(client.pos.yQ, INT16_MIN, INT16_MAX));

            ++count;
        }

        msg.playerCount = count;
        return msg;
    }

} // namespace bomberman::server
