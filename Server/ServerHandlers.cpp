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

        void recordPeerNote(net::NetDiagnostics* diag, const uint8_t peerId, std::string_view note,
                            const uint32_t valueA = 0, const uint32_t valueB = 0)
        {
            if (!diag)
                return;

            NetEvent event{};
            event.type = NetEventType::Note;
            event.peerId = peerId;
            event.valueA = valueA;
            event.valueB = valueB;
            event.note = std::string(note);
            diag->recordEvent(event);
        }

        /** @brief Sends a Reject packet and initiates a graceful disconnect. */
        void sendReject(ServerContext& ctx, MsgReject::EReason reason)
        {
            MsgReject reject{};
            reject.reason = reason;
            reject.expectedProtocolVersion = (reason == MsgReject::EReason::VersionMismatch) ? kProtocolVersion : 0;

            const bool sent = queueReliableControl(ctx.peer, makeRejectPacket(reject));
            if (sent)
            {
                flush(ctx.state.host);
                LOG_NET_CONN_INFO("Sent Reject (reason={}) to peer {}",
                                  static_cast<int>(reason), ctx.peer->incomingPeerID);

                if (ctx.diag)
                {
                    ctx.diag->recordPacketSent(EMsgType::Reject,
                                               static_cast<uint8_t>(EChannel::ControlReliable),
                                               kPacketHeaderSize + kMsgRejectSize);
                    const std::string note = std::string("peer rejected: ") + std::string(rejectReasonName(reason));
                    recordPeerNote(ctx.diag, 0xFF, note,
                                   static_cast<uint32_t>(ctx.peer->incomingPeerID),
                                   static_cast<uint32_t>(reason));
                }
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
            LOG_NET_CONN_DEBUG("Duplicate Hello from already-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
            return;
        }

        // Reject if the session is full (no player IDs available).
        if (ctx.state.playerIdPoolSize == 0)
        {
            LOG_NET_CONN_WARN("Server full ({}/{}) – rejecting peer {}",
                              static_cast<int>(kMaxPlayers), static_cast<int>(kMaxPlayers),
                              ctx.peer->incomingPeerID);
            sendReject(ctx, MsgReject::EReason::ServerFull);
            return;
        }

        MsgHello msgHello{};
        if (!deserializeMsgHello(payload, payloadSize, msgHello))
        {
            LOG_NET_PROTO_WARN("Failed to parse Hello payload from peer {}", ctx.peer->incomingPeerID);
            return;
        }

        // Reject on protocol version mismatch.
        if (msgHello.protocolVersion != kProtocolVersion)
        {
            LOG_NET_PROTO_ERROR("Protocol mismatch: peer {} sent version {}, expected {}",
                                ctx.peer->incomingPeerID, msgHello.protocolVersion, kProtocolVersion);
            sendReject(ctx, MsgReject::EReason::VersionMismatch);
            return;
        }

        const std::string_view playerName(msgHello.name, boundedStrLen(msgHello.name, kPlayerNameMax));
        LOG_NET_CONN_INFO("Hello from \"{}\" (peer {})", playerName, ctx.peer->incomingPeerID);

        // Allocate a player ID from the pool.
        const uint8_t playerId = ctx.state.playerIdPool[--ctx.state.playerIdPoolSize];

        // Build and send Welcome response.
        MsgWelcome welcome{};
        welcome.protocolVersion = kProtocolVersion;
        welcome.playerId        = playerId;
        welcome.serverTickRate  = sim::kTickRate;

        if (!queueReliableControl(ctx.peer, makeWelcomePacket(welcome)))
        {
            LOG_NET_CONN_ERROR("Failed to send Welcome to peer {} - rejecting", ctx.peer->incomingPeerID);
            // Return playerId to pool on failure.
            ctx.state.playerIdPool[ctx.state.playerIdPoolSize++] = playerId;
            sendReject(ctx, MsgReject::EReason::Other);
            return;
        }
        LOG_NET_CONN_INFO("Queued Welcome to playerId={}", playerId);

        if (ctx.diag)
            ctx.diag->recordPacketSent(EMsgType::Welcome,
                                       static_cast<uint8_t>(EChannel::ControlReliable),
                                       kPacketHeaderSize + kMsgWelcomeSize);

        // Temporarily, the level info packet is considered part of the handshake, will be separated later.
        MsgLevelInfo levelInfo{};
        levelInfo.mapSeed = ctx.state.mapSeed;
        if (!queueReliableControl(ctx.peer, makeLevelInfoPacket(levelInfo)))
        {
            LOG_NET_CONN_ERROR("Failed to send LevelInfo to peer {} - rejecting", ctx.peer->incomingPeerID);
            ctx.state.playerIdPool[ctx.state.playerIdPoolSize++] = playerId;
            sendReject(ctx, MsgReject::EReason::Other);
            return;
        }

        if (ctx.diag)
            ctx.diag->recordPacketSent(EMsgType::LevelInfo,
                                       static_cast<uint8_t>(EChannel::ControlReliable),
                                       kPacketHeaderSize + kMsgLevelInfoSize);

        flush(ctx.state.host);
        LOG_NET_CONN_INFO("Sent handshake bundle (Welcome + LevelInfo seed={}) to playerId={}",
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

        recordPeerNote(ctx.diag, playerId, "player accepted",
                       static_cast<uint32_t>(ctx.peer->incomingPeerID),
                       levelInfo.mapSeed);
    }

    void onInput(ServerContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t size)
    {
        // Ignore input from peers that haven't completed the handshake yet.
        if (!hasClientState(ctx.peer))
        {
            LOG_NET_INPUT_WARN("Input from non-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
            return;
        }

        MsgInput msgInput{};
        if (!deserializeMsgInput(payload, size, msgInput))
        {
            LOG_NET_PROTO_WARN("Failed to parse Input payload from peer {}", ctx.peer->incomingPeerID);
            return;
        }

        const uint32_t highestSeq = msgInput.baseInputSeq + msgInput.count - 1;

        auto* client = getClientState(ctx.peer);
        if (client == nullptr)
        {
            LOG_NET_INPUT_ERROR("Input peer {} has no client state after guard - ignoring", ctx.peer->incomingPeerID);
            return;
        }

        uint8_t lateDropCount = 0;
        uint8_t aheadDropCount = 0;
        uint8_t acceptedCount = 0;
        uint32_t firstAheadSeq = 0;
        uint32_t lastAheadSeq = 0;
        const uint32_t maxAcceptableSeq = client->lastConsumedInputSeq + kInputWindowAhead;

        if (ctx.diag)
            ctx.diag->recordInputEntriesReceived(msgInput.count);

        // Store each entry from the batch into the ring buffer.
        for (uint8_t i = 0; i < msgInput.count; ++i)
        {
            const uint32_t seq = msgInput.baseInputSeq + i;
            const uint8_t  buttons = msgInput.inputs[i];

            // Detection logic lives here; telemetry goes to diag.

            // Unknown button bits: record but continue processing (do not reject).
            if ((buttons & ~kInputKnownBits) != 0)
            {
                if (ctx.diag)
                    ctx.diag->recordInputAnomaly(NetInputAnomalyType::UnknownButtons, seq, buttons);
            }

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
                if (ctx.diag)
                    ctx.diag->recordInputAnomaly(NetInputAnomalyType::OutOfOrder, seq, buttons);
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

        client->lateDrops += lateDropCount;
        client->aheadDrops += aheadDropCount;

        if (ctx.diag)
        {
            ctx.diag->recordInputEntriesAccepted(acceptedCount);
            ctx.diag->recordInputEntriesRedundant(lateDropCount);
        }

        if (aheadDropCount > 0)
        {
            ++client->consecutiveAheadDropBatches;
            const uint16_t streak = client->consecutiveAheadDropBatches;
            if (streak >= kRepeatedInputWarnThreshold
                && ctx.state.serverTick >= client->nextAheadWarnTick)
            {
                LOG_NET_INPUT_WARN(
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
        if (highestSeq % kInputBatchLogEveryN == 0)
        {
            LOG_NET_INPUT_DEBUG("Input playerId={} batch=[{}..{}] lastRecv={} lastConsumed={}",
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
        const std::size_t dataLength = event.packet->dataLength;
        const uint8_t channelId = event.channelID;

        LOG_NET_PACKET_TRACE("Received {} bytes on channel {}", dataLength, channelName(channelId));

        ServerContext ctx{state, event.peer, &state.diag};

        PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;

        if (!tryParsePacket(event.packet->data, dataLength, header, payload, payloadSize))
        {
            LOG_NET_PACKET_WARN("Failed to deserialize PacketHeader (malformed or truncated, {} bytes)", dataLength);
            state.diag.recordMalformedPacketRecv(channelId, dataLength, "header parse failed");
            return;
        }

        if (!gDispatcher.dispatch(ctx, header, payload, payloadSize))
        {
            LOG_NET_PACKET_TRACE("No handler for message type 0x{:02x}", static_cast<int>(header.type));
        }

        state.diag.recordPacketRecv(header.type, channelId, dataLength);
    }

} // namespace bomberman::server
