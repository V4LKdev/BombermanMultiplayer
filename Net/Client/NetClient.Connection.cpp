/**
 * @file NetClient.Connection.cpp
 * @brief Connection lifecycle and transport teardown for the client-side multiplayer connection hub.
 * @ingroup net_client
 */

#include "Net/Client/NetClientInternal.h"

#include "Net/NetSend.h"
#include "Net/NetTransportConfig.h"
#include "Util/Log.h"

#include <cstring>

namespace bomberman::net
{
    using namespace net_client_internal;

    void NetClient::beginConnect(const std::string& host, uint16_t port, std::string_view playerName)
    {
        if (!impl_)
        {
            state_ = EConnectState::FailedInit;
            return;
        }

        if (isConnected() ||
            state_ == EConnectState::Connecting ||
            state_ == EConnectState::Handshaking ||
            state_ == EConnectState::Disconnecting)
        {
            LOG_NET_CONN_DEBUG("beginConnect() called while already in state {} - ignoring", connectStateName(state_));
            return;
        }

        resetSessionState();
        impl_->diagnostics.beginSession("client",
                                        impl_->diagnosticsEnabled,
                                        impl_->diagnosticsPredictionEnabled,
                                        impl_->diagnosticsRemoteSmoothingEnabled);
        impl_->diagnosticsSessionActive = impl_->diagnosticsEnabled;

        if (!initializeENet())
        {
            state_ = EConnectState::FailedInit;
            finalizeDiagnosticsSession(state_);
            return;
        }

        impl_->host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
        if (impl_->host == nullptr)
        {
            LOG_NET_CONN_ERROR("Failed to create ENet client host");
            state_ = EConnectState::FailedInit;
            destroyTransport();
            finalizeDiagnosticsSession(state_);
            return;
        }

        ENetAddress address{};
        if (enet_address_set_host(&address, host.c_str()) != 0)
        {
            LOG_NET_CONN_WARN("Invalid host address: {}", host);
            state_ = EConnectState::FailedResolve;
            destroyTransport();
            finalizeDiagnosticsSession(state_);
            return;
        }

        address.port = port;

        impl_->peer = enet_host_connect(impl_->host, &address, kChannelCount, 0);
        if (impl_->peer == nullptr)
        {
            LOG_NET_CONN_ERROR("Failed to create ENet peer");
            state_ = EConnectState::FailedConnect;
            destroyTransport();
            finalizeDiagnosticsSession(state_);
            return;
        }

        impl_->pendingPlayerName = std::string(playerName);
        impl_->connectStartTime = SteadyClock::now();
        lastRejectReason_.reset();

        state_ = EConnectState::Connecting;
        LOG_NET_CONN_DEBUG("Async connect initiated to {}:{}", host, port);
    }

    void NetClient::cancelConnect()
    {
        if (state_ != EConnectState::Connecting && state_ != EConnectState::Handshaking)
        {
            return;
        }

        LOG_NET_CONN_DEBUG("Connect attempt cancelled (was {})", connectStateName(state_));
        transitionToDisconnected();
    }

    void NetClient::disconnectAsync()
    {
        if (impl_ == nullptr)
        {
            return;
        }

        if (state_ == EConnectState::Disconnected || state_ == EConnectState::Disconnecting)
        {
            return;
        }

        if (state_ == EConnectState::Connecting || state_ == EConnectState::Handshaking)
        {
            LOG_NET_CONN_DEBUG("Disconnect requested during {} - cancelling connect attempt", connectStateName(state_));
            cancelConnect();
            return;
        }

        if (impl_->peer == nullptr || impl_->host == nullptr)
        {
            transitionToDisconnected();
            return;
        }

        startGracefulDisconnect();
        LOG_NET_CONN_INFO("Queued graceful disconnect");
    }

    bool NetClient::disconnectBlocking()
    {
        const bool completedGracefully = drainGracefulDisconnect();
        if (completedGracefully && impl_ != nullptr)
        {
            impl_->diagnostics.recordPeerLifecycle(NetPeerLifecycleType::PeerDisconnected,
                                                   playerId_ != kInvalidPlayerId ? std::optional<uint8_t>(playerId_) : std::nullopt,
                                                   impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0,
                                                   "graceful disconnect completed");
        }
        transitionToDisconnected();
        return completedGracefully;
    }

    void NetClient::resetLocalInputStream()
    {
        if (impl_ == nullptr)
        {
            return;
        }

        impl_->nextInputSeq = kFirstInputSeq - 1u;
        std::memset(impl_->inputHistory, 0, sizeof(impl_->inputHistory));
    }

    void NetClient::resetLocalMatchBootstrapState()
    {
        resetLocalInputStream();
        if (impl_ == nullptr)
        {
            return;
        }

        impl_->lastGameplayReceiveTime = SteadyClock::now();
        impl_->lastSnapshotReceiveTime = TimePoint{};
        impl_->lastCorrectionReceiveTime = TimePoint{};
    }

    void NetClient::resetCurrentMatchSession()
    {
        if (impl_ == nullptr || !impl_->matchFlow.hasLevelInfo)
        {
            return;
        }

        resetLocalInputStream();
        impl_->matchFlow.reset();
    }

    void NetClient::startGracefulDisconnect()
    {
        if (impl_ == nullptr || impl_->peer == nullptr)
        {
            return;
        }

        if (state_ == EConnectState::Disconnecting)
        {
            return;
        }

        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = playerId_;
            event.note = "graceful disconnect queued";
            impl_->diagnostics.recordEvent(event);
        }

        enet_peer_disconnect(impl_->peer, 0);
        flush(impl_->host);
        impl_->disconnectStartTime = SteadyClock::now();
        state_ = EConnectState::Disconnecting;
    }

    bool NetClient::drainGracefulDisconnect()
    {
        if (impl_ == nullptr)
        {
            return false;
        }

        if (state_ == EConnectState::Disconnected)
        {
            return true;
        }

        if (state_ == EConnectState::Connecting || state_ == EConnectState::Handshaking)
        {
            cancelConnect();
            return false;
        }

        if (impl_->peer == nullptr || impl_->host == nullptr)
        {
            return false;
        }

        startGracefulDisconnect();
        ENetEvent event{};

        while (elapsedMs(impl_->disconnectStartTime) < kDisconnectTimeoutMs)
        {
            const int serviceResult = enet_host_service(impl_->host, &event, kDisconnectPollTimeoutMs);
            if (serviceResult < 0)
            {
                break;
            }
            if (serviceResult == 0)
            {
                continue;
            }

            if (event.type == ENET_EVENT_TYPE_RECEIVE)
            {
                enet_packet_destroy(event.packet);
                continue;
            }

            if (event.type == ENET_EVENT_TYPE_DISCONNECT)
            {
                return true;
            }
        }

        LOG_NET_CONN_WARN("Graceful disconnect timed out after {}ms; tearing down transport locally", kDisconnectTimeoutMs);
        return false;
    }

    bool NetClient::checkConnectTimeouts()
    {
        using enum EConnectState;

        if (state_ == Connecting && elapsedMs(impl_->connectStartTime) >= kConnectTimeoutMs)
        {
            LOG_NET_CONN_WARN("Async connect timeout ({}ms)", kConnectTimeoutMs);
            failConnection(FailedConnect);
            return true;
        }
        if (state_ == Handshaking && elapsedMs(impl_->handshakeStartTime) >= kConnectTimeoutMs)
        {
            LOG_NET_CONN_WARN("Async handshake timeout ({}ms)", kConnectTimeoutMs);
            failConnection(FailedHandshake);
            return true;
        }
        return false;
    }

    bool NetClient::checkDisconnectTimeout()
    {
        if (state_ != EConnectState::Disconnecting || impl_ == nullptr)
        {
            return false;
        }

        if (elapsedMs(impl_->disconnectStartTime) < kDisconnectTimeoutMs)
        {
            return false;
        }

        LOG_NET_CONN_WARN("Graceful disconnect timed out after {}ms; tearing down transport locally", kDisconnectTimeoutMs);
        transitionToDisconnected();
        return true;
    }

    bool NetClient::handleConnectEvent()
    {
        if (state_ != EConnectState::Connecting)
        {
            LOG_NET_CONN_DEBUG("Ignoring unexpected CONNECT event in state {}", connectStateName(state_));
            return false;
        }

        applyDefaultPeerTransportConfig(impl_->peer);

        LOG_NET_CONN_DEBUG("ENet connect event received, sending Hello");

        impl_->diagnostics.recordPeerLifecycle(NetPeerLifecycleType::TransportConnected,
                                               std::nullopt,
                                               impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0,
                                               "enet connect accepted");

        const auto helloPacket = makeHelloPacket(impl_->pendingPlayerName, kProtocolVersion);
        if (!queueReliableControl(impl_->peer, helloPacket))
        {
            impl_->diagnostics.recordPacketSent(EMsgType::Hello,
                                                static_cast<uint8_t>(EChannel::ControlReliable),
                                                helloPacket.size(),
                                                NetPacketResult::Dropped);
            LOG_NET_CONN_ERROR("Failed to send Hello packet");
            failConnection(EConnectState::FailedHandshake);
            return true;
        }
        impl_->diagnostics.recordPacketSent(EMsgType::Hello,
                                            static_cast<uint8_t>(EChannel::ControlReliable),
                                            helloPacket.size(),
                                            NetPacketResult::Ok);

        flush(impl_->host);

        state_ = EConnectState::Handshaking;
        impl_->handshakeStartTime = SteadyClock::now();
        return false;
    }

    void NetClient::handleDisconnectEvent()
    {
        using enum EConnectState;

        if (state_ == Disconnecting)
        {
            impl_->diagnostics.recordPeerLifecycle(NetPeerLifecycleType::PeerDisconnected,
                                                   playerId_ != kInvalidPlayerId ? std::optional<uint8_t>(playerId_) : std::nullopt,
                                                   impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0,
                                                   "graceful disconnect completed");
            LOG_NET_CONN_INFO("Graceful disconnect completed");
            transitionToDisconnected();
            return;
        }
        if (state_ == Connecting)
        {
            impl_->diagnostics.recordPeerLifecycle(NetPeerLifecycleType::TransportDisconnectedBeforeHandshake,
                                                   std::nullopt,
                                                   impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0,
                                                   "remote close during connect");
            LOG_NET_CONN_WARN("Remote close/timeout during Connecting");
            failConnection(FailedConnect);
            return;
        }
        if (state_ == Handshaking)
        {
            impl_->diagnostics.recordPeerLifecycle(NetPeerLifecycleType::TransportDisconnectedBeforeHandshake,
                                                   std::nullopt,
                                                   impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0,
                                                   "remote close during handshake");
            LOG_NET_CONN_WARN("Remote close/timeout during Handshaking");
            failConnection(FailedHandshake);
            return;
        }
        impl_->diagnostics.recordPeerLifecycle(NetPeerLifecycleType::PeerDisconnected,
                                               playerId_ != kInvalidPlayerId ? std::optional<uint8_t>(playerId_) : std::nullopt,
                                               impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0,
                                               "remote close or timeout");
        transitionToDisconnected();
    }

    bool NetClient::initializeENet()
    {
        if (initialized_)
        {
            return true;
        }

        if (enet_initialize() != 0)
        {
            LOG_NET_CONN_ERROR("ENet initialization failed");
            initialized_ = false;
            return false;
        }

        initialized_ = true;
        return true;
    }

    void NetClient::shutdownENet()
    {
        if (initialized_)
        {
            enet_deinitialize();
            initialized_ = false;
        }
    }

    void NetClient::finalizeDiagnosticsSession(const EConnectState finalState)
    {
        if (impl_ == nullptr || !impl_->diagnosticsSessionActive)
            return;

        const uint64_t connectedDurationMs =
            (impl_->connectedStartTime != TimePoint{})
                ? static_cast<uint64_t>(elapsedMs(impl_->connectedStartTime))
                : 0u;

        impl_->diagnostics.recordFinalState(finalState, connectedDurationMs);
        impl_->diagnostics.endSession();

        std::filesystem::create_directories("logs");
        const std::string playerLabel =
            playerId_ != kInvalidPlayerId
                ? ("p" + std::to_string(static_cast<uint32_t>(playerId_) + 1u))
                : std::string("u");
        const std::string reportPath = makeUniqueJsonReportPath(
            "logs/diag_client_" + playerLabel + "_" + currentLocalTimeTagForFilename());

        if (impl_->diagnostics.writeJsonReport(reportPath))
        {
            LOG_NET_DIAG_INFO("Client diagnostics JSON report written to {}", reportPath);
        }
        else
        {
            LOG_NET_DIAG_ERROR("Failed to write client diagnostics JSON report to {}", reportPath);
        }

        impl_->diagnosticsSessionActive = false;
    }

    void NetClient::destroyTransport()
    {
        if (impl_ == nullptr)
        {
            return;
        }

        if (impl_->peer != nullptr)
        {
            enet_peer_reset(impl_->peer);
            impl_->peer = nullptr;
        }

        if (impl_->host != nullptr)
        {
            enet_host_destroy(impl_->host);
            impl_->host = nullptr;
        }
    }

    void NetClient::resetState()
    {
        state_ = EConnectState::Disconnected;
        resetSessionState();
    }

    void NetClient::transitionToDisconnected()
    {
        finalizeDiagnosticsSession(EConnectState::Disconnected);
        destroyTransport();
        resetState();
    }

    void NetClient::resetSessionState(const bool clearRejectReason)
    {
        playerId_ = kInvalidPlayerId;
        serverTickRate_ = 0;
        if (clearRejectReason)
        {
            lastRejectReason_.reset();
        }
        if (impl_)
        {
            resetLocalInputStream();
            impl_->pendingPlayerName.clear();
            impl_->connectStartTime = TimePoint{};
            impl_->handshakeStartTime = TimePoint{};
            impl_->disconnectStartTime = TimePoint{};
            impl_->connectedStartTime = TimePoint{};
            impl_->lastLobbyStateReceiveTime = TimePoint{};
            impl_->lastGameplayReceiveTime = TimePoint{};
            impl_->lastSnapshotReceiveTime = TimePoint{};
            impl_->lastCorrectionReceiveTime = TimePoint{};
            impl_->lastTransportSampleTime = TimePoint{};
            impl_->lastInputSendTime = TimePoint{};
            impl_->liveStats = {};
            impl_->matchFlow.reset();
            impl_->cachedLobbyState.reset();
        }
    }

    void NetClient::failConnection(const EConnectState failureState, const bool clearRejectReason)
    {
        finalizeDiagnosticsSession(failureState);
        destroyTransport();
        resetSessionState(clearRejectReason);
        state_ = failureState;
    }
} // namespace bomberman::net
