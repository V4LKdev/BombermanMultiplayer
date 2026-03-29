/**
 * @file NetClient.Runtime.cpp
 * @brief Network pumping and outgoing runtime messaging for the client-side multiplayer connection hub.
 * @ingroup net_client
 */

#include "Net/Client/NetClientInternal.h"

#include "Net/NetSend.h"
#include "Net/NetTransportConfig.h"
#include "Util/Log.h"

#include <algorithm>

namespace bomberman::net
{
    using namespace net_client_internal;

    bool NetClient::handleReceiveEvent(const uint8_t* data, std::size_t dataLength, uint8_t channelID)
    {
        LOG_NET_PACKET_TRACE("Received {} bytes on channel {}", dataLength, channelName(channelID));

        PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;
        if (!tryParsePacket(data, dataLength, header, payload, payloadSize))
        {
            LOG_NET_PACKET_WARN("Failed to deserialize PacketHeader (malformed or truncated, {} bytes)", dataLength);
            impl_->diagnostics.recordMalformedPacket(channelID, dataLength, "header parse failed");
            if (state_ == EConnectState::Handshaking)
            {
                LOG_NET_PROTO_ERROR("Malformed packet received during handshake - failing handshake");
                failConnection(EConnectState::FailedHandshake);
                return true;
            }
            return false;
        }

        if (!isExpectedChannelFor(header.type, channelID))
        {
            LOG_NET_PACKET_WARN("Rejected {} on wrong channel: got {}, expected {}",
                                msgTypeName(header.type),
                                channelName(channelID),
                                channelName(static_cast<uint8_t>(expectedChannelFor(header.type))));
            impl_->diagnostics.recordPacketRecv(header.type,
                                                channelID,
                                                dataLength,
                                                NetPacketResult::Rejected);

            if (state_ == EConnectState::Handshaking && isHandshakeControlMessage(header.type))
            {
                LOG_NET_PROTO_ERROR("Handshake control message {} received on wrong channel - failing handshake",
                                    msgTypeName(header.type));
                failConnection(EConnectState::FailedHandshake);
                return true;
            }
            return false;
        }

        if (state_ == EConnectState::Handshaking &&
            channelID == static_cast<uint8_t>(EChannel::ControlReliable) &&
            !isHandshakeControlMessage(header.type))
        {
            LOG_NET_PROTO_ERROR("Unexpected control message {} received during handshake - failing handshake",
                                msgTypeName(header.type));
            impl_->diagnostics.recordPacketRecv(header.type,
                                                channelID,
                                                dataLength,
                                                NetPacketResult::Rejected);
            failConnection(EConnectState::FailedHandshake);
            return true;
        }

        if (!impl_->dispatcher.dispatch(*this, header, payload, payloadSize))
        {
            LOG_NET_PACKET_TRACE("No handler for message type 0x{:02x}", static_cast<int>(header.type));
            impl_->diagnostics.recordPacketRecv(header.type,
                                                channelID,
                                                dataLength,
                                                NetPacketResult::Rejected);

            if (state_ == EConnectState::Handshaking && channelID == static_cast<uint8_t>(EChannel::ControlReliable))
            {
                LOG_NET_PROTO_ERROR("Unhandled control message {} received during handshake - failing handshake",
                                    msgTypeName(header.type));
                failConnection(EConnectState::FailedHandshake);
                return true;
            }
            return false;
        }

        impl_->diagnostics.recordPacketRecv(header.type,
                                            channelID,
                                            dataLength,
                                            NetPacketResult::Ok);

        if (isFailedState(state_))
        {
            failConnection(state_, !lastRejectReason_.has_value());
            return true;
        }

        return false;
    }

    void NetClient::pumpNetwork(uint16_t timeoutMs)
    {
        if (impl_ == nullptr || impl_->host == nullptr)
        {
            return;
        }

        if (checkDisconnectTimeout() || checkConnectTimeouts())
        {
            return;
        }

        ENetEvent event{};
        bool disconnectEventSeen = false;
        int serviceResult = 0;

        const auto handleServiceEvent = [&](const ENetEvent& currentEvent) -> bool
        {
            switch (currentEvent.type)
            {
                case ENET_EVENT_TYPE_RECEIVE:
                {
                    if (state_ == EConnectState::Disconnecting)
                    {
                        enet_packet_destroy(currentEvent.packet);
                        return false;
                    }

                    const bool shouldReturnEarly =
                        handleReceiveEvent(currentEvent.packet->data, currentEvent.packet->dataLength, currentEvent.channelID);
                    enet_packet_destroy(currentEvent.packet);
                    return shouldReturnEarly;
                }

                case ENET_EVENT_TYPE_DISCONNECT:
                    if (state_ != EConnectState::Disconnecting)
                    {
                        LOG_NET_CONN_WARN("Disconnected from server (transport close or timeout)");
                    }
                    disconnectEventSeen = true;
                    return false;
                case ENET_EVENT_TYPE_CONNECT:
                    return handleConnectEvent();
                case ENET_EVENT_TYPE_NONE:
                    return false;
            }

            return false;
        };

        const auto handleServiceError = [&]
        {
            LOG_NET_CONN_ERROR("ENet host service error: result={} - tearing down transport", serviceResult);

            using enum EConnectState;

            if (state_ == Handshaking)
            {
                failConnection(FailedHandshake);
            }
            else if (state_ == Connecting)
            {
                failConnection(FailedConnect);
            }
            else
            {
                transitionToDisconnected();
            }
        };

        const auto sampleLiveTransportAndSilence = [&]
        {
            if (impl_ == nullptr || !isConnected() || impl_->peer == nullptr)
                return;

            const bool hasActiveMatch = impl_->matchFlow.hasLevelInfo;
            const uint32_t snapshotTick =
                (hasActiveMatch && impl_->matchFlow.snapshot.has_value())
                    ? impl_->matchFlow.snapshot->serverTick
                    : 0u;
            const uint32_t correctionTick =
                (hasActiveMatch && impl_->matchFlow.correction.has_value())
                    ? impl_->matchFlow.correction->serverTick
                    : 0u;
            const uint32_t snapshotAgeMs =
                (hasActiveMatch && impl_->lastSnapshotReceiveTime != TimePoint{})
                    ? elapsedSinceOrZero(impl_->lastSnapshotReceiveTime, TimePoint{})
                    : 0u;
            const uint32_t gameplaySilenceMsValue = hasActiveMatch ? gameplaySilenceMs() : 0u;

            updateLiveTransportStats(impl_->peer->roundTripTime,
                                     impl_->peer->roundTripTimeVariance,
                                     impl_->peer->packetLoss,
                                     snapshotTick,
                                     correctionTick,
                                     snapshotAgeMs,
                                     gameplaySilenceMsValue);

            if (hasActiveMatch)
            {
                impl_->diagnostics.sampleGameplaySilence(gameplaySilenceMsValue);
            }
            else
            {
                impl_->diagnostics.sampleLobbySilence(lobbySilenceMs());
            }

            if (impl_->lastTransportSampleTime == TimePoint{} || elapsedMs(impl_->lastTransportSampleTime) >= 1000)
            {
                impl_->lastTransportSampleTime = SteadyClock::now();
                impl_->diagnostics.sampleTransport(impl_->peer->roundTripTime,
                                                   impl_->peer->roundTripTimeVariance,
                                                   impl_->peer->packetLoss);
            }
        };

        while ((serviceResult = enet_host_service(impl_->host, &event, timeoutMs)) > 0)
        {
            if (handleServiceEvent(event))
            {
                return;
            }

            if (disconnectEventSeen)
            {
                break;
            }

            timeoutMs = 0;
        }

        if (serviceResult < 0)
        {
            handleServiceError();
        }
        if (disconnectEventSeen)
        {
            handleDisconnectEvent();
            return;
        }

        sampleLiveTransportAndSilence();
    }

    std::optional<uint32_t> NetClient::sendInput(uint8_t buttons)
    {
        if (impl_ == nullptr || !isConnected() || impl_->peer == nullptr)
        {
            return std::nullopt;
        }

        const TimePoint now = SteadyClock::now();
        if (impl_->lastInputSendTime != TimePoint{})
        {
            impl_->diagnostics.sampleInputSendGap(
                static_cast<uint32_t>(elapsedMs(impl_->lastInputSendTime)));
        }
        impl_->lastInputSendTime = now;

        impl_->nextInputSeq++;
        const uint32_t seq = impl_->nextInputSeq;

        impl_->inputHistory[seq % kMaxInputBatchSize] = buttons;

        const auto batchCount = static_cast<uint8_t>(std::min<uint32_t>(seq, kMaxInputBatchSize));
        const uint32_t baseSeq = seq - batchCount + 1;

        MsgInput msg{};
        msg.baseInputSeq = baseSeq;
        msg.count = batchCount;
        for (uint8_t i = 0; i < batchCount; ++i)
        {
            msg.inputs[i] = impl_->inputHistory[(baseSeq + i) % kMaxInputBatchSize];
        }

        const auto inputPacket = makeInputPacket(msg);
        if (!queueUnreliableInput(impl_->peer, inputPacket))
        {
            impl_->diagnostics.recordPacketSent(EMsgType::Input,
                                                static_cast<uint8_t>(EChannel::InputUnreliable),
                                                inputPacket.size(),
                                                NetPacketResult::Dropped);
            LOG_NET_INPUT_WARN("Failed to queue Input seq={} batch=[{}..{}] buttons=0x{:02x}",
                               seq,
                               baseSeq,
                               seq,
                               buttons);
            return seq;
        }

        impl_->diagnostics.recordPacketSent(EMsgType::Input,
                                            static_cast<uint8_t>(EChannel::InputUnreliable),
                                            inputPacket.size(),
                                            NetPacketResult::Ok);

        if ((seq % kInputLogEveryN) == 0)
        {
            LOG_NET_INPUT_DEBUG("Sent Input seq={} batch=[{}..{}] buttons=0x{:02x}",
                                seq,
                                baseSeq,
                                seq,
                                buttons);
        }

        return seq;
    }

    bool NetClient::sendLobbyReady(const bool ready)
    {
        if (impl_ == nullptr || !isConnected() || impl_->peer == nullptr)
        {
            return false;
        }

        const auto readyPacket = makeLobbyReadyPacket(ready);
        if (!queueReliableControl(impl_->peer, readyPacket))
        {
            impl_->diagnostics.recordPacketSent(EMsgType::LobbyReady,
                                                static_cast<uint8_t>(EChannel::ControlReliable),
                                                readyPacket.size(),
                                                NetPacketResult::Dropped);
            LOG_NET_CONN_WARN("Failed to queue LobbyReady desiredReady={}", ready);
            return false;
        }

        impl_->diagnostics.recordPacketSent(EMsgType::LobbyReady,
                                            static_cast<uint8_t>(EChannel::ControlReliable),
                                            readyPacket.size(),
                                            NetPacketResult::Ok);

        flushOutgoing();
        LOG_NET_CONN_DEBUG("Queued LobbyReady desiredReady={}", ready);
        return true;
    }

    bool NetClient::sendMatchLoaded(const uint32_t matchId)
    {
        if (impl_ == nullptr || !isConnected() || impl_->peer == nullptr || matchId == 0)
        {
            return false;
        }

        const auto loadedPacket = makeMatchLoadedPacket(matchId);
        if (!queueReliableControl(impl_->peer, loadedPacket))
        {
            impl_->diagnostics.recordPacketSent(EMsgType::MatchLoaded,
                                                static_cast<uint8_t>(EChannel::ControlReliable),
                                                loadedPacket.size(),
                                                NetPacketResult::Dropped);
            LOG_NET_CONN_WARN("Failed to queue MatchLoaded matchId={}", matchId);
            return false;
        }

        impl_->diagnostics.recordPacketSent(EMsgType::MatchLoaded,
                                            static_cast<uint8_t>(EChannel::ControlReliable),
                                            loadedPacket.size(),
                                            NetPacketResult::Ok);

        flushOutgoing();
        LOG_NET_CONN_DEBUG("Queued MatchLoaded matchId={}", matchId);
        return true;
    }

    void NetClient::flushOutgoing() const
    {
        if (impl_ == nullptr || impl_->host == nullptr)
        {
            return;
        }

        flush(impl_->host);
    }
} // namespace bomberman::net
