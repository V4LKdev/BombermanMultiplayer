/**
 * @file ServerHandlers.cpp
 * @brief Authoritative server receive-path validation and dispatcher entry point.
 */

#include "ServerHandlers.h"

#include "Net/PacketDispatch.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    namespace
    {
        [[nodiscard]]
        std::optional<uint8_t> acceptedPlayerId(const ENetPeer* peer)
        {
            const auto* session = getPeerSession(peer);
            return (session != nullptr) ? session->playerId : std::nullopt;
        }

        [[nodiscard]]
        PacketDispatcher<PacketDispatchContext> makeServerDispatcher()
        {
            PacketDispatcher<PacketDispatchContext> dispatcher{};
            dispatcher.bind(EMsgType::Hello, &onHello);
            dispatcher.bind(EMsgType::LobbyReady, &onLobbyReady);
            dispatcher.bind(EMsgType::MatchLoaded, &onMatchLoaded);
            dispatcher.bind(EMsgType::Input, &onInput);
            return dispatcher;
        }

        const PacketDispatcher<PacketDispatchContext> gDispatcher = makeServerDispatcher();
    } // namespace

    void handleReceiveEvent(const ENetEvent& event, ServerState& state)
    {
        const std::size_t dataLength = event.packet->dataLength;
        const uint8_t channelId = event.channelID;

        LOG_NET_PACKET_TRACE("Received {} bytes on channel {}", dataLength, channelName(channelId));

        PacketDispatchContext ctx{state, event.peer, &state.diag};
        ctx.receiveResult = NetPacketResult::Rejected;
        ctx.recordedPlayerId = acceptedPlayerId(event.peer);

        PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;

        if (!tryParsePacket(event.packet->data, dataLength, header, payload, payloadSize))
        {
            LOG_NET_PACKET_WARN("Failed to deserialize PacketHeader (malformed or truncated, {} bytes)", dataLength);
            state.diag.recordMalformedPacketRecv(ctx.recordedPlayerId.value_or(0xFF),
                                                 channelId,
                                                 dataLength,
                                                 "header parse failed");
            return;
        }

        if (!isExpectedChannelFor(header.type, channelId))
        {
            LOG_NET_PACKET_WARN("Rejected {} on wrong channel: got {}, expected {}",
                                msgTypeName(header.type),
                                channelName(channelId),
                                channelName(static_cast<uint8_t>(expectedChannelFor(header.type))));
            state.diag.recordPacketRecv(header.type,
                                        ctx.recordedPlayerId.value_or(0xFF),
                                        channelId,
                                        dataLength,
                                        NetPacketResult::Rejected);
            return;
        }

        if (!gDispatcher.dispatch(ctx, header, payload, payloadSize))
        {
            LOG_NET_PACKET_TRACE("No handler for message type 0x{:02x}", static_cast<int>(header.type));
            state.diag.recordPacketRecv(header.type,
                                        ctx.recordedPlayerId.value_or(0xFF),
                                        channelId,
                                        dataLength,
                                        NetPacketResult::Rejected);
            return;
        }

        state.diag.recordPacketRecv(header.type,
                                    ctx.recordedPlayerId.value_or(0xFF),
                                    channelId,
                                    dataLength,
                                    ctx.receiveResult);
    }
} // namespace bomberman::server
