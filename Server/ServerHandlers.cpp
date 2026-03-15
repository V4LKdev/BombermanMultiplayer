#include "ServerHandlers.h"

#include <optional>
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
        constexpr NetPacketResult toNetPacketResult(const ReceiveDispatchResult result)
        {
            switch (result)
            {
                case ReceiveDispatchResult::Ok:        return NetPacketResult::Ok;
                case ReceiveDispatchResult::Rejected:  return NetPacketResult::Rejected;
                case ReceiveDispatchResult::Malformed: return NetPacketResult::Malformed;
                default:                               return NetPacketResult::Rejected;
            }
        }

        constexpr std::string_view rejectReasonName(const MsgReject::EReason reason)
        {
            switch (reason)
            {
                case MsgReject::EReason::VersionMismatch: return "version mismatch";
                case MsgReject::EReason::ServerFull:      return "server full";
                case MsgReject::EReason::Banned:          return "banned";
                case MsgReject::EReason::Other:           return "other";
                default:                                  return "unknown";
            }
        }

        void recordPeerLifecycle(net::NetDiagnostics* diag,
                                 const NetPeerLifecycleType type,
                                 const uint8_t peerId,
                                 const uint32_t transportPeerId,
                                 std::string_view note = {})
        {
            if (!diag)
                return;

            diag->recordPeerLifecycle(type, peerId, transportPeerId, note);
        }

        /** @brief Sends a Reject packet and initiates a graceful disconnect. */
        void sendReject(ServerContext& ctx, MsgReject::EReason reason)
        {
            MsgReject reject{};
            reject.reason = reason;
            reject.expectedProtocolVersion = (reason == MsgReject::EReason::VersionMismatch) ? kProtocolVersion : 0;

            recordPeerLifecycle(ctx.diag,
                                NetPeerLifecycleType::PeerRejected,
                                ctx.diagPeerId,
                                static_cast<uint32_t>(ctx.peer->incomingPeerID),
                                rejectReasonName(reason));

            const bool sent = queueReliableControl(ctx.peer, makeRejectPacket(reject));
            if (sent)
            {
                flush(ctx.state.host);
                LOG_NET_CONN_INFO("Sent Reject (reason={}) to peer {}",
                                  static_cast<int>(reason), ctx.peer->incomingPeerID);

                if (ctx.diag)
                {
                    ctx.diag->recordPacketSent(EMsgType::Reject,
                                               ctx.diagPeerId,
                                               static_cast<uint8_t>(EChannel::ControlReliable),
                                               kPacketHeaderSize + kMsgRejectSize);
                }
            }
            else if (ctx.diag)
            {
                ctx.diag->recordPacketSent(EMsgType::Reject,
                                           ctx.diagPeerId,
                                           static_cast<uint8_t>(EChannel::ControlReliable),
                                           kPacketHeaderSize + kMsgRejectSize,
                                           NetPacketResult::Dropped);
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

        uint8_t gameplayPeerId(ENetPeer* peer)
        {
            const auto* client = getClientState(peer);
            return client ? client->playerId : 0xFF;
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
            LOG_NET_CONN_DEBUG("Duplicate Hello from already-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
            ctx.receiveResult = ReceiveDispatchResult::Rejected;
            ctx.diagPeerId = gameplayPeerId(ctx.peer);
            return;
        }

        // Reject if the session is full (no player IDs available).
        if (ctx.state.playerIdPoolSize == 0)
        {
            LOG_NET_CONN_WARN("Server full ({}/{}) – rejecting peer {}",
                              static_cast<int>(kMaxPlayers), static_cast<int>(kMaxPlayers),
                              ctx.peer->incomingPeerID);
            ctx.receiveResult = ReceiveDispatchResult::Rejected;
            sendReject(ctx, MsgReject::EReason::ServerFull);
            return;
        }

        MsgHello msgHello{};
        if (!deserializeMsgHello(payload, payloadSize, msgHello))
        {
            LOG_NET_PROTO_WARN("Failed to parse Hello payload from peer {}", ctx.peer->incomingPeerID);
            ctx.receiveResult = ReceiveDispatchResult::Malformed;
            return;
        }

        // Reject on protocol version mismatch.
        if (msgHello.protocolVersion != kProtocolVersion)
        {
            LOG_NET_PROTO_ERROR("Protocol mismatch: peer {} sent version {}, expected {}",
                                ctx.peer->incomingPeerID, msgHello.protocolVersion, kProtocolVersion);
            ctx.receiveResult = ReceiveDispatchResult::Rejected;
            sendReject(ctx, MsgReject::EReason::VersionMismatch);
            return;
        }

        const std::string_view playerName(msgHello.name, boundedStrLen(msgHello.name, kPlayerNameMax));
        LOG_NET_CONN_INFO("Hello from \"{}\" (peer {})", playerName, ctx.peer->incomingPeerID);

        // Allocate a player ID from the pool.
        const std::optional<uint8_t> playerId = acquirePlayerId(ctx.state);
        if(!playerId.has_value())
        {
            ctx.receiveResult = ReceiveDispatchResult::Rejected;
            sendReject(ctx, MsgReject::EReason::ServerFull);
            return;
        }

        // Build and send Welcome response.
        MsgWelcome welcome{};
        welcome.protocolVersion = kProtocolVersion;
        welcome.playerId        = playerId.value();
        welcome.serverTickRate  = sim::kTickRate;

        if (!queueReliableControl(ctx.peer, makeWelcomePacket(welcome)))
        {
            LOG_NET_CONN_ERROR("Failed to send Welcome to peer {} - rejecting", ctx.peer->incomingPeerID);
            if (ctx.diag)
                ctx.diag->recordPacketSent(EMsgType::Welcome,
                                           playerId.value(),
                                           static_cast<uint8_t>(EChannel::ControlReliable),
                                           kPacketHeaderSize + kMsgWelcomeSize,
                                           NetPacketResult::Dropped);
            // Return playerId to pool on failure.
            releasePlayerId(ctx.state, playerId.value());
            ctx.receiveResult = ReceiveDispatchResult::Rejected;
            sendReject(ctx, MsgReject::EReason::Other);
            return;
        }
        LOG_NET_CONN_INFO("Queued Welcome to playerId={}", playerId.value());

        if (ctx.diag)
            ctx.diag->recordPacketSent(EMsgType::Welcome,
                                       playerId.value(),
                                       static_cast<uint8_t>(EChannel::ControlReliable),
                                       kPacketHeaderSize + kMsgWelcomeSize);

        // Temporarily, the level info packet is considered part of the handshake, will be separated later.
        MsgLevelInfo levelInfo{};
        levelInfo.mapSeed = ctx.state.mapSeed;
        if (!queueReliableControl(ctx.peer, makeLevelInfoPacket(levelInfo)))
        {
            LOG_NET_CONN_ERROR("Failed to send LevelInfo to peer {} - rejecting", ctx.peer->incomingPeerID);
            if (ctx.diag)
                ctx.diag->recordPacketSent(EMsgType::LevelInfo,
                                           playerId.value(),
                                           static_cast<uint8_t>(EChannel::ControlReliable),
                                           kPacketHeaderSize + kMsgLevelInfoSize,
                                           NetPacketResult::Dropped);
            releasePlayerId(ctx.state, playerId.value());
            ctx.receiveResult = ReceiveDispatchResult::Rejected;
            sendReject(ctx, MsgReject::EReason::Other);
            return;
        }

        if (ctx.diag)
            ctx.diag->recordPacketSent(EMsgType::LevelInfo,
                                       playerId.value(),
                                       static_cast<uint8_t>(EChannel::ControlReliable),
                                       kPacketHeaderSize + kMsgLevelInfoSize);

        flush(ctx.state.host);
        LOG_NET_CONN_INFO("Sent handshake bundle (Welcome + LevelInfo seed={}) to playerId={}",
                          levelInfo.mapSeed, playerId.value());

        // Initialize per-client state in the stable-address array slot.
        auto& slot = ctx.state.clients[playerId.value()];
        slot.emplace();
        slot->playerId = playerId.value();
        slot->peer = ctx.peer;
        // Spawn at the center of the start tile in Q8 (center convention: col*256+128, row*256+128).
        // TODO: individual spawn points per player.
        slot->pos = { playerStartX * 256 + 128, playerStartY * 256 + 128 };

        // Store stable pointer in peer->data for fast lookup in handlers and disconnect path.
        ctx.peer->data = &slot.value();

        recordPeerLifecycle(ctx.diag,
                            NetPeerLifecycleType::PlayerAccepted,
                            playerId.value(),
                            static_cast<uint32_t>(ctx.peer->incomingPeerID));
        ctx.diagPeerId = playerId.value();
        ctx.receiveResult = ReceiveDispatchResult::Ok;
    }

    void onInput(ServerContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t size)
    {
        // Ignore input from peers that haven't completed the handshake yet.
        if (!hasClientState(ctx.peer))
        {
            LOG_NET_INPUT_WARN("Input from non-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
            ctx.receiveResult = ReceiveDispatchResult::Rejected;
            return;
        }

        MsgInput msgInput{};
        if (!deserializeMsgInput(payload, size, msgInput))
        {
            LOG_NET_PROTO_WARN("Failed to parse Input payload from peer {}", ctx.peer->incomingPeerID);
            ctx.receiveResult = ReceiveDispatchResult::Malformed;
            return;
        }

        const uint32_t highestSeq = msgInput.baseInputSeq + msgInput.count - 1;

        auto* client = getClientState(ctx.peer);
        if (client == nullptr)
        {
            LOG_NET_INPUT_ERROR("Input peer {} has no client state after guard - ignoring", ctx.peer->incomingPeerID);
            ctx.receiveResult = ReceiveDispatchResult::Rejected;
            return;
        }
        ctx.diagPeerId = client->playerId;

        uint8_t redundantCount = 0;
        uint8_t outsideWindowCount = 0;
        uint8_t acceptedCount = 0;
        uint32_t firstOutsideWindowSeq = 0;
        uint32_t lastOutsideWindowSeq = 0;
        const uint32_t maxAcceptableSeq = client->lastConsumedInputSeq + kInputWindowAhead;

        if (ctx.diag)
            ctx.diag->recordInputBatchReceived(msgInput.count);

        // If even the newest input in the batch is already consumed, the packet was still
        // received successfully, but the entire batch is fully stale at the input-stream layer.
        if (highestSeq <= client->lastConsumedInputSeq)
        {
            if (ctx.diag)
            {
                ctx.diag->recordInputBatchFullyStale();
                ctx.diag->recordInputEntriesRedundant(msgInput.count);
            }

            ctx.receiveResult = ReceiveDispatchResult::Ok;
            return;
        }

        // Store each entry from the batch into the ring buffer.
        for (uint8_t i = 0; i < msgInput.count; ++i)
        {
            const uint32_t seq = msgInput.baseInputSeq + i;
            const uint8_t  buttons = msgInput.inputs[i];

            // Drop late arrivals: anything already consumed is discarded.
            if (seq <= client->lastConsumedInputSeq)
            {
                ++redundantCount;
                continue;
            }

            // Reject entries too far ahead of the consume window to prevent ring stomping.
            if (seq > maxAcceptableSeq)
            {
                if (outsideWindowCount == 0)
                    firstOutsideWindowSeq = seq;
                lastOutsideWindowSeq = seq;
                ++outsideWindowCount;
                continue;
            }

            auto& slot = client->inputRing[seq % kServerInputBufferSize];
            slot.seq     = seq;
            slot.buttons = buttons;
            slot.valid   = true;
            ++acceptedCount;

            if (seq > client->lastReceivedInputSeq)
                client->lastReceivedInputSeq = seq;
        }

        if (ctx.diag)
        {
            ctx.diag->recordInputEntriesAccepted(acceptedCount);
            ctx.diag->recordInputEntriesRedundant(redundantCount);
            ctx.diag->recordInputEntriesRejectedOutsideWindow(outsideWindowCount);
        }

        if (outsideWindowCount > 0)
        {
            ++client->consecutiveOutsideWindowBatches;
            const uint16_t streak = client->consecutiveOutsideWindowBatches;
            if (streak >= kRepeatedInputWarnThreshold
                && ctx.state.serverTick >= client->nextOutsideWindowWarnTick)
            {
                LOG_NET_INPUT_WARN(
                    "Repeated outside-window input rejections playerId={} streak={} latestRejectedSeqs=[{}..{}] count={} batch=[{}..{}] maxAcceptable={} lastRecv={} lastConsumed={}",
                    client->playerId, streak,
                    firstOutsideWindowSeq, lastOutsideWindowSeq, outsideWindowCount,
                    msgInput.baseInputSeq, highestSeq,
                    maxAcceptableSeq, client->lastReceivedInputSeq, client->lastConsumedInputSeq);

                client->nextOutsideWindowWarnTick = ctx.state.serverTick + kRepeatedInputWarnCooldownTicks;
            }
        }
        else
        {
            client->consecutiveOutsideWindowBatches = 0;
        }

        // Periodic logging.
        if (highestSeq % kInputBatchLogIntervalTicks == 0)
        {
            LOG_NET_INPUT_DEBUG("Input playerId={} batch=[{}..{}] lastRecv={} lastConsumed={}",
                                client->playerId, msgInput.baseInputSeq, highestSeq,
                                client->lastReceivedInputSeq, client->lastConsumedInputSeq);
        }

        ctx.receiveResult = ReceiveDispatchResult::Ok;
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
        const std::size_t dataLength = event.packet->dataLength;
        const uint8_t channelId = event.channelID;

        LOG_NET_PACKET_TRACE("Received {} bytes on channel {}", dataLength, channelName(channelId));

        ServerContext ctx{state, event.peer, &state.diag};
        ctx.receiveResult = ReceiveDispatchResult::Rejected;
        ctx.diagPeerId = gameplayPeerId(event.peer);

        PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;

        if (!tryParsePacket(event.packet->data, dataLength, header, payload, payloadSize))
        {
            LOG_NET_PACKET_WARN("Failed to deserialize PacketHeader (malformed or truncated, {} bytes)", dataLength);
            state.diag.recordMalformedPacketRecv(ctx.diagPeerId, channelId, dataLength, "header parse failed");
            return;
        }

        if (!gDispatcher.dispatch(ctx, header, payload, payloadSize))
        {
            LOG_NET_PACKET_TRACE("No handler for message type 0x{:02x}", static_cast<int>(header.type));
            state.diag.recordPacketRecv(header.type, ctx.diagPeerId, channelId, dataLength, NetPacketResult::Rejected);
            return;
        }

        state.diag.recordPacketRecv(header.type, ctx.diagPeerId, channelId, dataLength,
                                    toNetPacketResult(ctx.receiveResult));
    }

} // namespace bomberman::server
