#include "ServerHandlers.h"

#include <string_view>

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
        /** @brief Sends a Reject packet and initiates a graceful disconnect. */
        void sendReject(ServerContext& ctx, MsgReject::EReason reason)
        {
            MsgReject reject{};
            reject.reason = reason;
            reject.expectedProtocolVersion =
                (reason == MsgReject::EReason::VersionMismatch) ? kProtocolVersion : 0;

            if (sendReliable(ctx.state.host, ctx.peer, makeRejectPacket(reject, 0, 0)))
            {
                LOG_SERVER_INFO("Sent Reject (reason={}) to peer {}",
                                static_cast<int>(reason), ctx.peer->incomingPeerID);
            }

            enet_peer_disconnect_later(ctx.peer, 0);
        }

        /** @brief Returns true if this peer has already completed the Hello/Welcome handshake. */
        bool isHandshaked(const ENetPeer* peer)
        {
            return peer->data != nullptr;
        }

        /** @brief Marks a peer as handshaked. */
        void markHandshaked(ENetPeer* peer)
        {
            peer->data = reinterpret_cast<void*>(1);
        }
    } // namespace

    // =================================================================================================================
    // ==== Message Handlers ===========================================================================================
    // =================================================================================================================

    void onHello(ServerContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t payloadSize)
    {
        // Guard: ignore duplicate Hello from an already-handshaked peer.
        if (isHandshaked(ctx.peer))
        {
            LOG_SERVER_WARN("Duplicate Hello from already-handshaked peer {} – ignoring",
                            ctx.peer->incomingPeerID);
            return;
        }

        // Guard: reject if the session is full (all input slots taken).
        if (ctx.state.inputs.size() >= kMaxPlayers)
        {
            LOG_SERVER_WARN("Server full ({}/{}) – rejecting peer {}",
                            ctx.state.inputs.size(), static_cast<int>(kMaxPlayers),
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

        // Build and send Welcome response.
        MsgWelcome welcome{};
        welcome.protocolVersion = kProtocolVersion;
        welcome.clientId        = ctx.peer->incomingPeerID;
        welcome.serverTickRate  = kServerTickRate;

        if (sendReliable(ctx.state.host, ctx.peer, makeWelcomePacket(welcome, 0, 0)))
        {
            LOG_SERVER_INFO("Sent Welcome to clientId={}", welcome.clientId);
        }

        // Mark handshake complete and initialize per-client input state.
        markHandshaked(ctx.peer);
        const uint32_t clientId = ctx.peer->incomingPeerID;
        ctx.state.inputs[clientId] = MsgInput{};
        ctx.state.lastBombCommandId[clientId] = 0; // Client's first real bombCommandId is always >= 1.
    }

    void onInput(ServerContext& ctx, const PacketHeader& header, const uint8_t* payload, std::size_t size)
    {
        // Ignore input from peers that haven't completed the handshake yet.
        if (!isHandshaked(ctx.peer))
        {
            LOG_SERVER_WARN("Input from non-handshaked peer {} – ignoring", ctx.peer->incomingPeerID);
            return;
        }

        MsgInput msgInput{};
        if (!deserializeMsgInput(payload, size, msgInput))
        {
            LOG_SERVER_WARN("Failed to parse Input payload from peer {}", ctx.peer->incomingPeerID);
            return;
        }

        const uint32_t clientId = ctx.peer->incomingPeerID;

        // Detect new bomb command by comparing to last seen value.
        auto& lastCmd = ctx.state.lastBombCommandId[clientId];
        if (msgInput.bombCommandId != lastCmd)
        {
            lastCmd = msgInput.bombCommandId;
            LOG_SERVER_DEBUG("Bomb request: clientId={} bombCmdId={}",
                             clientId, msgInput.bombCommandId);
            // TODO: Validate & spawn bomb (cooldown, max bombs, position check)
        }

        ctx.state.inputs[clientId] = msgInput;

        if (header.sequence % kInputLogEveryN == 0)
        {
            LOG_SERVER_DEBUG("Input clientId={} seq={} tick={} move=({},{}) bombCmdId={}",
                             clientId, header.sequence, header.tick,
                             static_cast<int>(msgInput.moveX), static_cast<int>(msgInput.moveY),
                             msgInput.bombCommandId);
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
