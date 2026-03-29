/**
 * @file ServerHandlers.cpp
 * @ingroup authoritative_server
 * @brief Authoritative server receive-path validation and dispatcher entry point.
 */

#include "ServerHandlers.h"

#include "Net/PacketDispatch.h"
#include "Util/Log.h"

namespace bomberman::server
{
    namespace
    {
        [[nodiscard]]
        net::PacketDispatcher<PacketDispatchContext> makeServerDispatcher()
        {
            net::PacketDispatcher<PacketDispatchContext> dispatcher{};
            dispatcher.bind(net::EMsgType::Hello, &onHello);
            dispatcher.bind(net::EMsgType::LobbyReady, &onLobbyReady);
            dispatcher.bind(net::EMsgType::MatchLoaded, &onMatchLoaded);
            dispatcher.bind(net::EMsgType::Input, &onInput);
            return dispatcher;
        }

        const net::PacketDispatcher<PacketDispatchContext> gDispatcher = makeServerDispatcher();
    } // namespace

    std::optional<uint8_t> acceptedPlayerId(const ENetPeer* peer)
    {
        const auto* session = getPeerSession(peer);
        return (session != nullptr) ? session->playerId : std::nullopt;
    }

    void handleReceiveEvent(const ENetEvent& event, ServerState& state)
    {
        const std::size_t dataLength = event.packet->dataLength;
        const uint8_t channelId = event.channelID;

        LOG_NET_PACKET_TRACE("Received {} bytes on channel {}", dataLength, net::channelName(channelId));

        PacketDispatchContext ctx{state, event.peer, &state.diag};
        ctx.receiveResult = net::NetPacketResult::Rejected;
        ctx.recordedPlayerId = acceptedPlayerId(event.peer);

        net::PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;

        if (!net::tryParsePacket(event.packet->data, dataLength, header, payload, payloadSize))
        {
            LOG_NET_PACKET_WARN("Failed to deserialize PacketHeader (malformed or truncated, {} bytes)", dataLength);
            state.diag.recordMalformedPacketRecv(ctx.recordedPlayerId.value_or(0xFF),
                                                 channelId,
                                                 dataLength,
                                                 "header parse failed");
            return;
        }

        if (!net::isExpectedChannelFor(header.type, channelId))
        {
            LOG_NET_PACKET_WARN("Rejected {} on wrong channel: got {}, expected {}",
                                net::msgTypeName(header.type),
                                net::channelName(channelId),
                                net::channelName(static_cast<uint8_t>(net::expectedChannelFor(header.type))));
            state.diag.recordPacketRecv(header.type,
                                        ctx.recordedPlayerId.value_or(0xFF),
                                        channelId,
                                        dataLength,
                                        net::NetPacketResult::Rejected);
            return;
        }

        if (!gDispatcher.dispatch(ctx, header, payload, payloadSize))
        {
            LOG_NET_PACKET_WARN("No handler for incoming {} (type=0x{:02x}, channel={}, bytes={})",
                                net::msgTypeName(header.type),
                                static_cast<int>(header.type),
                                net::channelName(channelId),
                                dataLength);
            state.diag.recordPacketRecv(header.type,
                                        ctx.recordedPlayerId.value_or(0xFF),
                                        channelId,
                                        dataLength,
                                        net::NetPacketResult::Rejected);
            return;
        }

        state.diag.recordPacketRecv(header.type,
                                    ctx.recordedPlayerId.value_or(0xFF),
                                    channelId,
                                    dataLength,
                                    ctx.receiveResult);
    }
} // namespace bomberman::server
