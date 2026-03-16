#include "ServerSession.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <random>

#include "Net/NetSend.h"
#include "ServerSnapshot.h"
#include "Sim/TileMapGen.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    namespace
    {
        [[nodiscard]]
        MsgCorrection buildCorrection(const ServerState& state, const ClientState& client)
        {
            MsgCorrection corr{};
            corr.serverTick = state.serverTick;
            corr.lastProcessedInputSeq = client.lastProcessedInputSeq;
            corr.xQ = static_cast<int16_t>(std::clamp(client.pos.xQ, INT16_MIN, INT16_MAX));
            corr.yQ = static_cast<int16_t>(std::clamp(client.pos.yQ, INT16_MIN, INT16_MAX));
            return corr;
        }

        /** @brief Resolves which input bitmask the server should simulate for this client on the current tick. */
        void resolveClientInputForTick(ServerState& state, ClientState& client)
        {
            client.currentButtons = 0;

            if (client.lastReceivedInputSeq == 0)
            {
                // No input ever received from this client - idle.
                return;
            }

            if (!client.inputTimelineStarted)
            {
                client.inputTimelineStarted = true;
                client.nextConsumeServerTick = state.serverTick + state.inputLeadTicks;
            }

            if (state.serverTick < client.nextConsumeServerTick)
                return;

            const uint32_t nextSeq = client.lastProcessedInputSeq + 1u;
            const std::size_t seqSlot = nextSeq % kServerInputBufferSize;
            auto& entry = client.inputRing[seqSlot];
            if (entry.valid && entry.seq == nextSeq)
            {
                // Exact match at the scheduled deadline - consume this command.
                client.currentButtons  = entry.buttons;
                client.previousButtons = client.currentButtons;
                client.lastProcessedInputSeq = nextSeq;
                client.consecutiveInputGaps = 0;

                if (entry.seenBuffered && !entry.seenDirect)
                    state.diag.recordBufferedInputRecovery(client.playerId, nextSeq, state.serverTick);

                entry.valid = false;
                entry.seenDirect = false;
                entry.seenBuffered = false;
                ++client.nextConsumeServerTick;
                return;
            }

            // The consume deadline was reached and the exact sequence is still missing.
            // Fall back to the previous buttons and advance permanently.
            client.currentButtons = client.previousButtons;
            client.lastProcessedInputSeq = nextSeq;
            ++client.consecutiveInputGaps;
            ++client.nextConsumeServerTick;

            state.diag.recordSimulationGap(client.playerId,
                                           nextSeq,
                                           client.previousButtons,
                                           state.serverTick);

            if (client.consecutiveInputGaps >= kRepeatedInputWarnThreshold
                && state.serverTick >= client.nextGapWarnTick)
            {
                LOG_NET_INPUT_WARN(
                    "Repeated input gaps playerId={} streak={} expectedSeq={} slotValid={} slotSeq={} using previousButtons=0x{:02x} lastRecv={} lastProcessed={}",
                    client.playerId, client.consecutiveInputGaps,
                    nextSeq,
                    entry.valid ? 1 : 0, entry.seq,
                    client.previousButtons,
                    client.lastReceivedInputSeq, client.lastProcessedInputSeq);

                client.nextGapWarnTick = state.serverTick + kRepeatedInputWarnCooldownTicks;
            }
        }
    } // namespace

    void initServerState(ServerState& state,
                         ENetHost* host,
                         const bool diagEnabled,
                         const bool overrideMapSeed,
                         uint32_t mapSeed,
                         const uint32_t inputLeadTicks,
                         const uint32_t snapshotIntervalTicks)
    {
        state.host = host;
        state.serverTick = 0;
        state.inputLeadTicks = inputLeadTicks;
        state.snapshotIntervalTicks = snapshotIntervalTicks;

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

    std::optional<uint8_t> acquirePlayerId(ServerState& state)
    {
        if(state.playerIdPoolSize == 0)
            return std::nullopt;

        const uint8_t playerId = state.playerIdPool[0];

        for(uint8_t i = 1; i < state.playerIdPoolSize; ++i)
            state.playerIdPool[i - 1] = state.playerIdPool[i];

        --state.playerIdPoolSize;
        return playerId;
    }

    void releasePlayerId(ServerState& state, const uint8_t playerId)
    {
        if(state.playerIdPoolSize >= kMaxPlayers)
            return;

        // Keep free IDs sorted so acquirePlayerId() always returns the lowest available id.
        uint8_t insertIndex = 0;
        while(insertIndex < state.playerIdPoolSize && state.playerIdPool[insertIndex] < playerId)
            ++insertIndex;

        if(insertIndex < state.playerIdPoolSize && state.playerIdPool[insertIndex] == playerId)
            return;

        for(uint8_t i = state.playerIdPoolSize; i > insertIndex; --i)
            state.playerIdPool[i] = state.playerIdPool[i - 1];

        state.playerIdPool[insertIndex] = playerId;
        ++state.playerIdPoolSize;
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
            resolveClientInputForTick(state, client);
            state.diag.samplePeerInputContinuity(client.playerId,
                                                 client.lastReceivedInputSeq,
                                                 client.lastProcessedInputSeq);

            // Derive movement from the button bitmask.
            const int8_t moveX = buttonsToMoveX(client.currentButtons);
            const int8_t moveY = buttonsToMoveY(client.currentButtons);

            client.pos = sim::stepMovementWithCollision(
                client.pos, moveX, moveY, state.tiles);

            bool correctionQueued = false;
            if (client.peer != nullptr)
            {
                const auto correction = buildCorrection(state, client);
                const auto correctionBytes = makeCorrectionPacket(correction);
                correctionQueued = queueUnreliableCorrection(client.peer, correctionBytes);
            }
            state.diag.recordPacketSent(EMsgType::Correction,
                                        client.playerId,
                                        static_cast<uint8_t>(EChannel::CorrectionUnreliable),
                                        kPacketHeaderSize + kMsgCorrectionSize,
                                        correctionQueued ? NetPacketResult::Ok : NetPacketResult::Dropped);

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

        if (!shouldBroadcastSnapshot(state))
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

            const bool queued = queueUnreliableSnapshot(slot->peer, snapshotBytes);
            state.diag.recordPacketSent(EMsgType::Snapshot,
                                        slot->playerId,
                                        static_cast<uint8_t>(EChannel::SnapshotUnreliable),
                                        kPacketHeaderSize + kMsgSnapshotSize,
                                        queued ? NetPacketResult::Ok : NetPacketResult::Dropped);
        }

        flush(state.host);
    }

} // namespace bomberman::server
