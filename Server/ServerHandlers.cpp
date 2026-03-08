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
    // ==== Message Handlers ===========================================================================================
    // =================================================================================================================

    void onHello(ServerContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t payloadSize)
    {
        MsgHello msgHello{};
        if (!deserializeMsgHello(payload, payloadSize, msgHello))
        {
            LOG_SERVER_WARN("Failed to parse Hello payload");
            return;
        }

        // TODO: Send a Reject message instead of silently dropping on protocol mismatch
        if (msgHello.protocolVersion != kProtocolVersion)
        {
            LOG_SERVER_ERROR("Protocol mismatch (client={}, server={})", msgHello.protocolVersion, kProtocolVersion);
            return;
        }

        const std::string_view playerName(msgHello.name, boundedStrLen(msgHello.name, kPlayerNameMax));
        LOG_SERVER_INFO("Hello from \"{}\"", playerName);

        // Build and send Welcome response
        MsgWelcome welcome{};
        welcome.protocolVersion = kProtocolVersion;
        welcome.clientId = ctx.peer->incomingPeerID;
        welcome.serverTickRate = kServerTickRate;

        if (sendReliable(ctx.state.host, ctx.peer, makeWelcomePacket(welcome, 0, 0)))
        {
            LOG_SERVER_INFO("Sent Welcome to clientId={}", welcome.clientId);
        }
    }

    void onInput(ServerContext& ctx, const PacketHeader& header, const uint8_t* payload, std::size_t size)
    {
        MsgInput msgInput{};
        if (!deserializeMsgInput(payload, size, msgInput))
        {
            LOG_SERVER_WARN("Failed to parse Input payload");
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
    // ==== Packet Dispatcher ======================================================================================
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
