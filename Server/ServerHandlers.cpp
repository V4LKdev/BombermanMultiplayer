#include "ServerHandlers.h"

#include <string_view>

#include "Const.h"
#include "Net/NetSend.h"
#include "Net/PacketDispatch.h"
#include "ServerSession.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    // =================================================================================================================
    // ==== Internal Helpers ===========================================================================================
    // =================================================================================================================

    namespace
    {
        constexpr uint32_t kServerInputLogEveryN = static_cast<uint32_t>(sim::kTickRate) * 2u;

        /** @brief Sends a Reject packet and initiates a graceful disconnect. */
        void sendReject(ServerContext& ctx, MsgReject::EReason reason)
        {
            MsgReject reject{};
            reject.reason = reason;
            reject.expectedProtocolVersion = (reason == MsgReject::EReason::VersionMismatch) ? kProtocolVersion : 0;

            if (queueReliableControl(ctx.peer, makeRejectPacket(reject)))
            {
                flush(ctx.state.host);
                LOG_SERVER_INFO("Sent Reject (reason={}) to peer {}",
                                static_cast<int>(reason), ctx.peer->incomingPeerID);
            }

            enet_peer_disconnect_later(ctx.peer, 0);
        }

        bool hasClientState(const ENetPeer* peer)
        {
            return peer && peer->data != nullptr;
        }

        ClientState* getClientState(ENetPeer* peer)
        {
            return peer ? static_cast<ClientState*>(peer->data) : nullptr;
        }

    } // namespace

    // =================================================================================================================
    // ==== Message Handlers ===========================================================================================
    // =================================================================================================================

    void onHello(ServerContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t payloadSize)
    {
        // Ignore duplicate Hello from an already-handshaked peer.
        if (hasClientState(ctx.peer))
        {
            LOG_SERVER_WARN("Duplicate Hello from already-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
            return;
        }

        // Reject if the session is full (no player IDs available).
        if (ctx.state.playerIdPoolSize == 0)
        {
            LOG_SERVER_WARN("Server full ({}/{}) – rejecting peer {}",
                            static_cast<int>(kMaxPlayers), static_cast<int>(kMaxPlayers),
                            ctx.peer->incomingPeerID);
            sendReject(ctx, MsgReject::EReason::ServerFull);
            return;
        }

        MsgHello msgHello{};
        if (!deserializeMsgHello(payload, payloadSize, msgHello))
        {
            LOG_SERVER_WARN("Failed to parse Hello payload from peer {}", ctx.peer->incomingPeerID);
            return;
        }

        // Reject on protocol version mismatch.
        if (msgHello.protocolVersion != kProtocolVersion)
        {
            LOG_SERVER_ERROR("Protocol mismatch: peer {} sent version {}, expected {}",
                             ctx.peer->incomingPeerID, msgHello.protocolVersion, kProtocolVersion);
            sendReject(ctx, MsgReject::EReason::VersionMismatch);
            return;
        }

        const std::string_view playerName(msgHello.name, boundedStrLen(msgHello.name, kPlayerNameMax));
        LOG_SERVER_INFO("Hello from \"{}\" (peer {})", playerName, ctx.peer->incomingPeerID);

        // Allocate a player ID from the pool.
        const uint8_t playerId = ctx.state.playerIdPool[--ctx.state.playerIdPoolSize];

        // Build and send Welcome response.
        MsgWelcome welcome{};
        welcome.protocolVersion = kProtocolVersion;
        welcome.playerId        = playerId;
        welcome.serverTickRate  = sim::kTickRate;

        if (!queueReliableControl(ctx.peer, makeWelcomePacket(welcome)))
        {
            LOG_SERVER_ERROR("Failed to send Welcome to peer {} - rejecting", ctx.peer->incomingPeerID);
            // Return playerId to pool on failure.
            ctx.state.playerIdPool[ctx.state.playerIdPoolSize++] = playerId;
            sendReject(ctx, MsgReject::EReason::Other);
            return;
        }
        LOG_SERVER_INFO("Queued Welcome to playerId={}", playerId);

        // Temporarily, the level info packet is considered part of the handshake, will be separated later.
        MsgLevelInfo levelInfo{};
        levelInfo.mapSeed = ctx.state.mapSeed;
        if (!queueReliableControl(ctx.peer, makeLevelInfoPacket(levelInfo)))
        {
            LOG_SERVER_ERROR("Failed to send LevelInfo to peer {} - rejecting", ctx.peer->incomingPeerID);
            ctx.state.playerIdPool[ctx.state.playerIdPoolSize++] = playerId;
            sendReject(ctx, MsgReject::EReason::Other);
            return;
        }

        flush(ctx.state.host);
        LOG_SERVER_INFO("Sent handshake bundle (Welcome + LevelInfo seed={}) to playerId={}",
                        levelInfo.mapSeed, playerId);

        // Initialize per-client state in the stable-address array slot.
        auto& slot = ctx.state.clients[playerId];
        slot.emplace();
        slot->playerId = playerId;
        slot->peer = ctx.peer;
        // Spawn at the center of the start tile in Q8 (center convention: col*256+128, row*256+128).
        // TODO: individual spawn points per player.
        slot->pos = { playerStartX * 256 + 128, playerStartY * 256 + 128 };

        // Store stable pointer in peer->data for fast lookup in handlers and disconnect path.
        ctx.peer->data = &slot.value();

        // Start aggregated input diagnostics reporting after the first interval.
        slot->nextInputDiagTick = ctx.state.serverTick + kInputDiagReportTicks;
    }

    void onInput(ServerContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t size)
    {
        // Ignore input from peers that haven't completed the handshake yet.
        if (!hasClientState(ctx.peer))
        {
            LOG_SERVER_WARN("Input from non-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
            return;
        }

        MsgInput msgInput{};
        if (!deserializeMsgInput(payload, size, msgInput))
        {
            LOG_SERVER_WARN("Failed to parse Input payload from peer {}", ctx.peer->incomingPeerID);
            return;
        }

        const uint32_t highestSeq = msgInput.baseInputSeq + msgInput.count - 1;

        auto* client = getClientState(ctx.peer);
        if (client == nullptr)
        {
            LOG_SERVER_WARN("Input peer {} has no client state after guard - ignoring", ctx.peer->incomingPeerID);
            return;
        }

        uint8_t lateDropCount = 0;
        uint8_t aheadDropCount = 0;
        uint32_t firstAheadSeq = 0;
        uint32_t lastAheadSeq = 0;
        const uint32_t maxAcceptableSeq = client->lastConsumedInputSeq + kInputWindowAhead;

        // Store each entry from the batch into the ring buffer.
        for (uint8_t i = 0; i < msgInput.count; ++i)
        {
            const uint32_t seq = msgInput.baseInputSeq + i;

            // Drop late arrivals: anything already consumed is discarded.
            if (seq <= client->lastConsumedInputSeq)
            {
                ++lateDropCount;
                continue;
            }

            // Drop packets too far ahead of the consume window to prevent ring stomping.
            if (seq > client->lastConsumedInputSeq + kInputWindowAhead)
            {
                if (aheadDropCount == 0)
                    firstAheadSeq = seq;
                lastAheadSeq = seq;
                ++aheadDropCount;
                continue;
            }

            auto& slot = client->inputRing[seq % kServerInputBufferSize];
            slot.seq     = seq;
            slot.buttons = msgInput.inputs[i];
            slot.valid   = true;

            if (seq > client->lastReceivedInputSeq)
                client->lastReceivedInputSeq = seq;
        }

        client->lateDrops += lateDropCount;
        client->aheadDrops += aheadDropCount;

        if (aheadDropCount > 0)
        {
            ++client->consecutiveAheadDropBatches;
            const uint16_t streak = client->consecutiveAheadDropBatches;
            if (streak >= kRepeatedInputWarnThreshold
                && ctx.state.serverTick >= client->nextAheadWarnTick)
            {
                LOG_SERVER_WARN(
                    "Repeated ahead drops playerId={} streak={} latestAheadSeqs=[{}..{}] count={} batch=[{}..{}] maxAcceptable={} lastRecv={} lastConsumed={}",
                    client->playerId, streak,
                    firstAheadSeq, lastAheadSeq, aheadDropCount,
                    msgInput.baseInputSeq, highestSeq,
                    maxAcceptableSeq, client->lastReceivedInputSeq, client->lastConsumedInputSeq);

                client->nextAheadWarnTick = ctx.state.serverTick + kRepeatedInputWarnCooldownTicks;
            }
        }
        else
        {
            client->consecutiveAheadDropBatches = 0;
        }

        // Periodic logging.
        if (highestSeq % kServerInputLogEveryN == 0)
        {
            LOG_SERVER_DEBUG("Input playerId={} batch=[{}..{}] lastRecv={} lastConsumed={}",
                             client->playerId, msgInput.baseInputSeq, highestSeq,
                             client->lastReceivedInputSeq, client->lastConsumedInputSeq);
        }
    }

    // =================================================================================================================
    // ==== Packet Dispatcher ==========================================================================================
    // =================================================================================================================

    namespace
    {
        PacketDispatcher<ServerContext> makeServerDispatcher()
        {
            PacketDispatcher<ServerContext> d{};
            d.bind(EMsgType::Hello, &onHello);
            d.bind(EMsgType::Input, &onInput);
            return d;
        }

        const PacketDispatcher<ServerContext> gDispatcher = makeServerDispatcher();
    } // namespace

    void handleEventReceive(const ENetEvent& event, ServerState& state)
    {
        LOG_SERVER_TRACE("Received {} bytes on channel {}", event.packet->dataLength, channelName(event.channelID));

        ServerContext ctx{state, event.peer};
        dispatchPacket(gDispatcher, ctx, event.packet->data, event.packet->dataLength);
    }

} // namespace bomberman::server
