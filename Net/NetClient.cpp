/**
 * @file NetClient.cpp
 * @brief Implementation of the client-side multiplayer connection hub.
 */

#include "NetClient.h"

#include "ClientDiagnostics.h"
#include "NetSend.h"
#include "NetTransportConfig.h"
#include "PacketDispatch.h"
#include "Util/Log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <enet/enet.h>

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Internal Constants and Types ==============================================================================
    // =================================================================================================================

    namespace
    {
        constexpr int kConnectTimeoutMs = 5000;
        constexpr int kDisconnectTimeoutMs = 5000;
        constexpr int kDisconnectPollTimeoutMs = 100;
        // Preserve the previous combined backlog headroom (64 per event type)
        // while moving both reliable gameplay types into one ordered queue.
        constexpr std::size_t kMaxPendingGameplayEvents = 128;

        using SteadyClock = std::chrono::steady_clock;
        using TimePoint = SteadyClock::time_point;

        // Returns the elapsed milliseconds since the given time point.
        int elapsedMs(const TimePoint& since)
        {
            return static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - since).count());
        }

        // Returns elapsed milliseconds from the latest receive time, or from the fallback if none exists yet.
        uint32_t elapsedSinceOrZero(const TimePoint& latest, const TimePoint& fallback)
        {
            const TimePoint since = latest != TimePoint{} ? latest : fallback;
            if (since == TimePoint{})
            {
                return 0;
            }

            return static_cast<uint32_t>(elapsedMs(since));
        }

        std::string currentLocalTimeTagForFilename()
        {
            const auto now = std::chrono::system_clock::now();
            const auto nowTimeT = std::chrono::system_clock::to_time_t(now);

            std::tm localTm{};
#if defined(_WIN32)
            localtime_s(&localTm, &nowTimeT);
#else
            localtime_r(&nowTimeT, &localTm);
#endif

            std::ostringstream out;
            out << std::put_time(&localTm, "%H%M%S");
            return out.str();
        }

        std::string makeUniqueJsonReportPath(const std::string_view basePathWithoutExtension)
        {
            std::string candidate = std::string(basePathWithoutExtension) + ".json";
            if (!std::filesystem::exists(candidate))
            {
                return candidate;
            }

            for (uint32_t suffix = 2; suffix < 1000; ++suffix)
            {
                candidate = std::string(basePathWithoutExtension) + "_" + std::to_string(suffix) + ".json";
                if (!std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }

            return std::string(basePathWithoutExtension) + "_overflow.json";
        }

        // Returns true if the message type is a control message expected during handshake.
        constexpr bool isHandshakeControlMessage(EMsgType type)
        {
            using enum EMsgType;

            switch (type)
            {
                case Welcome:
                case Reject:
                    return true;
                case Hello:
                case Input:
                case Snapshot:
                case Correction:
                case BombPlaced:
                case ExplosionResolved:
                case LobbyState:
                case LevelInfo:
                case MatchLoaded:
                case MatchStart:
                case MatchCancelled:
                case MatchResult:
                default:
                    return false;
            }
        }
    } // namespace

    // =================================================================================================================
    // ===== NetClient::Impl definition ================================================================================
    // =================================================================================================================

    struct NetClient::Impl
    {
        ENetHost* host = nullptr;
        ENetPeer* peer = nullptr;
        PacketDispatcher<NetClient> dispatcher;

        // Input sequencing and history for resendable batches.
        uint32_t nextInputSeq = 0;
        uint8_t inputHistory[kMaxInputBatchSize]{};

        // Async connect and handshake state. Cleared by clearSessionState().
        std::string pendingPlayerName;
        TimePoint connectStartTime;
        TimePoint handshakeStartTime;
        TimePoint disconnectStartTime;
        TimePoint connectedStartTime;
        TimePoint lastLobbyStateReceiveTime;
        TimePoint lastGameplayReceiveTime;
        TimePoint lastSnapshotReceiveTime;
        TimePoint lastCorrectionReceiveTime;
        TimePoint lastTransportSampleTime;
        TimePoint lastInputSendTime;

        ClientDiagnostics diagnostics{};
        ClientLiveStats liveStats{};
        bool diagnosticsEnabled = false;
        bool diagnosticsSessionActive = false;
        bool diagnosticsPredictionEnabled = true;
        bool diagnosticsRemoteSmoothingEnabled = true;

        // ----- Cached session data -----
        struct CachedSnapshot
        {
            MsgSnapshot snapshot{};
            bool valid = false;
        };

        struct CachedCorrection
        {
            MsgCorrection correction{};
            bool valid = false;
        };

        struct CachedLobbyState
        {
            MsgLobbyState lobbyState{};
            bool valid = false;
        };
        CachedLobbyState cachedLobbyState{};

        struct MatchFlowState
        {
            MsgLevelInfo levelInfo{};
            bool hasLevelInfo = false;
            bool levelInfoPending = false;
            bool brokenGameplayEventStream = false;
            std::optional<MsgMatchStart> matchStart{};
            std::optional<uint32_t> cancelledMatchId{};
            std::optional<MsgMatchResult> matchResult{};
            CachedSnapshot snapshot{};
            CachedCorrection correction{};
            std::deque<GameplayEvent> pendingGameplayEvents{};

            void clearRuntime()
            {
                snapshot = {};
                correction = {};
                matchStart.reset();
                cancelledMatchId.reset();
                matchResult.reset();
                brokenGameplayEventStream = false;
                pendingGameplayEvents.clear();
            }

            void beginBootstrap(const MsgLevelInfo& newLevelInfo)
            {
                clearRuntime();
                levelInfo = newLevelInfo;
                hasLevelInfo = true;
                levelInfoPending = true;
            }

            [[nodiscard]]
            bool isActiveMatch(const uint32_t matchId) const
            {
                return hasLevelInfo && levelInfo.matchId == matchId;
            }

            [[nodiscard]]
            uint32_t currentMatchId() const
            {
                return hasLevelInfo ? levelInfo.matchId : 0u;
            }

            void clearBootstrap()
            {
                clearRuntime();
                levelInfo = {};
                hasLevelInfo = false;
                levelInfoPending = false;
            }

            void clearAll()
            {
                clearBootstrap();
            }
        };
        MatchFlowState matchFlow{};

        // ----- Protocol handlers -----
        static void onWelcome(NetClient& client,
                              const PacketHeader& /*header*/,
                              const uint8_t* payload,
                              std::size_t payloadSize)
        {
            client.handleWelcome(payload, payloadSize);
        }

        static void onReject(NetClient& client,
                             const PacketHeader& /*header*/,
                             const uint8_t* payload,
                             std::size_t payloadSize)
        {
            client.handleReject(payload, payloadSize);
        }

        static void onLevelInfo(NetClient& client,
                                const PacketHeader& /*header*/,
                                const uint8_t* payload,
                                std::size_t payloadSize)
        {
            client.handleLevelInfo(payload, payloadSize);
        }

        static void onLobbyState(NetClient& client,
                                 const PacketHeader& /*header*/,
                                 const uint8_t* payload,
                                 std::size_t payloadSize)
        {
            client.handleLobbyState(payload, payloadSize);
        }

        static void onMatchStart(NetClient& client,
                                 const PacketHeader& /*header*/,
                                 const uint8_t* payload,
                                 std::size_t payloadSize)
        {
            client.handleMatchStart(payload, payloadSize);
        }

        static void onMatchCancelled(NetClient& client,
                                     const PacketHeader& /*header*/,
                                     const uint8_t* payload,
                                     std::size_t payloadSize)
        {
            client.handleMatchCancelled(payload, payloadSize);
        }

        static void onMatchResult(NetClient& client,
                                  const PacketHeader& /*header*/,
                                  const uint8_t* payload,
                                  std::size_t payloadSize)
        {
            client.handleMatchResult(payload, payloadSize);
        }

        static void onSnapshot(NetClient& client,
                               const PacketHeader& /*header*/,
                               const uint8_t* payload,
                               std::size_t payloadSize)
        {
            client.handleSnapshot(payload, payloadSize);
        }

        static void onCorrection(NetClient& client,
                                 const PacketHeader& /*header*/,
                                 const uint8_t* payload,
                                 std::size_t payloadSize)
        {
            client.handleCorrection(payload, payloadSize);
        }

        static void onBombPlaced(NetClient& client,
                                 const PacketHeader& /*header*/,
                                 const uint8_t* payload,
                                 std::size_t payloadSize)
        {
            client.handleBombPlaced(payload, payloadSize);
        }

        static void onExplosionResolved(NetClient& client,
                                        const PacketHeader& /*header*/,
                                        const uint8_t* payload,
                                        std::size_t payloadSize)
        {
            client.handleExplosionResolved(payload, payloadSize);
        }
    };

    // =================================================================================================================
    // ===== Construction and Lifecycle ================================================================================
    // =================================================================================================================

    NetClient::NetClient()
    {
        using enum EMsgType;

        impl_ = std::make_unique<Impl>();

        impl_->dispatcher.bind(Welcome, &Impl::onWelcome);
        impl_->dispatcher.bind(Reject, &Impl::onReject);
        impl_->dispatcher.bind(LevelInfo, &Impl::onLevelInfo);
        impl_->dispatcher.bind(LobbyState, &Impl::onLobbyState);
        impl_->dispatcher.bind(MatchStart, &Impl::onMatchStart);
        impl_->dispatcher.bind(MatchCancelled, &Impl::onMatchCancelled);
        impl_->dispatcher.bind(MatchResult, &Impl::onMatchResult);
        impl_->dispatcher.bind(Snapshot, &Impl::onSnapshot);
        impl_->dispatcher.bind(Correction, &Impl::onCorrection);
        impl_->dispatcher.bind(BombPlaced, &Impl::onBombPlaced);
        impl_->dispatcher.bind(ExplosionResolved, &Impl::onExplosionResolved);
    }

    NetClient::~NetClient() noexcept
    {
        try
        {
            disconnectBlocking();
        }
        catch (...)
        {
            destroyTransport();
            resetState();
        }

        shutdownENet();
    }

    void NetClient::setDiagnosticsConfig(const bool enabled,
                                         const bool predictionEnabled,
                                         const bool remoteSmoothingEnabled)
    {
        if (impl_ == nullptr)
            return;

        impl_->diagnosticsEnabled = enabled;
        impl_->diagnosticsPredictionEnabled = predictionEnabled;
        impl_->diagnosticsRemoteSmoothingEnabled = remoteSmoothingEnabled;
    }

    ClientDiagnostics& NetClient::clientDiagnostics()
    {
        return impl_->diagnostics;
    }

    const ClientDiagnostics& NetClient::clientDiagnostics() const
    {
        return impl_->diagnostics;
    }

    void NetClient::updateLiveTransportStats(const uint32_t rttMs,
                                             const uint32_t rttVarianceMs,
                                             const uint32_t lossPermille,
                                             const uint32_t lastSnapshotTick,
                                             const uint32_t lastCorrectionTick,
                                             const uint32_t snapshotAgeMs,
                                             const uint32_t gameplaySilenceMs)
    {
        if (impl_ == nullptr)
            return;

        impl_->liveStats.rttMs = rttMs;
        impl_->liveStats.rttVarianceMs = rttVarianceMs;
        impl_->liveStats.lossPermille = lossPermille;
        impl_->liveStats.lastSnapshotTick = lastSnapshotTick;
        impl_->liveStats.lastCorrectionTick = lastCorrectionTick;
        impl_->liveStats.snapshotAgeMs = snapshotAgeMs;
        impl_->liveStats.gameplaySilenceMs = gameplaySilenceMs;
    }

    void NetClient::updateLivePredictionStats(const bool predictionActive,
                                              const bool recoveryActive,
                                              const uint32_t correctionCount,
                                              const uint32_t mismatchCount,
                                              const uint32_t lastCorrectionDeltaQ,
                                              const uint32_t maxPendingInputDepth)
    {
        if (impl_ == nullptr)
            return;

        impl_->liveStats.predictionActive = predictionActive;
        impl_->liveStats.recoveryActive = recoveryActive;
        impl_->liveStats.correctionCount = correctionCount;
        impl_->liveStats.mismatchCount = mismatchCount;
        impl_->liveStats.lastCorrectionDeltaQ = lastCorrectionDeltaQ;
        impl_->liveStats.maxPendingInputDepth = maxPendingInputDepth;
    }

    const NetClient::ClientLiveStats& NetClient::liveStats() const
    {
        return impl_->liveStats;
    }

    // =================================================================================================================
    // ===== Connection lifecycle ======================================================================================
    // =================================================================================================================

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

        clearSessionState();
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

        // Client host: one peer, project channel count, unlimited bandwidth.
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
        finalizeDiagnosticsSession(EConnectState::Disconnected);
        destroyTransport();
        resetState();
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
            finalizeDiagnosticsSession(EConnectState::Disconnected);
            destroyTransport();
            resetState();
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
        finalizeDiagnosticsSession(EConnectState::Disconnected);
        destroyTransport();
        resetState();
        return completedGracefully;
    }

    void NetClient::clearActiveMatchRuntimeCaches() const
    {
        if (impl_ == nullptr)
        {
            return;
        }

        impl_->matchFlow.clearRuntime();
    }

    void NetClient::resetLocalInputStream() const
    {
        if (impl_ == nullptr)
        {
            return;
        }

        impl_->nextInputSeq = 0;
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

    void NetClient::clearCurrentMatchSession() const
    {
        if (impl_ == nullptr || !impl_->matchFlow.hasLevelInfo)
        {
            return;
        }

        clearActiveMatchRuntimeCaches();
        resetLocalInputStream();
        impl_->matchFlow.clearBootstrap();
    }

    // =================================================================================================================
    // ===== Disconnect Helpers ========================================================================================
    // =================================================================================================================

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

    // =================================================================================================================
    // ===== pumpNetwork() helpers =====================================================================================
    // =================================================================================================================

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
        finalizeDiagnosticsSession(EConnectState::Disconnected);
        destroyTransport();
        resetState();
        return true;
    }

    bool NetClient::handleConnectEvent()
    {
        if (state_ != EConnectState::Connecting)
        {
            LOG_NET_CONN_DEBUG("Ignoring unexpected CONNECT event in state {}", connectStateName(state_));
            return false;
        }

        // Should be called exactly once per connect attempt.
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
            finalizeDiagnosticsSession(EConnectState::Disconnected);
            destroyTransport();
            resetState();
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
        finalizeDiagnosticsSession(EConnectState::Disconnected);
        destroyTransport();
        resetState();
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

                    //  We can return early if the event was handled successfully.
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
                finalizeDiagnosticsSession(EConnectState::Disconnected);
                destroyTransport();
                resetState();
            }
        };

        const auto sampleLiveTransportAndSilence = [&]
        {
            if (impl_ == nullptr || !isConnected() || impl_->peer == nullptr)
                return;

            const bool hasActiveMatch = impl_->matchFlow.hasLevelInfo;
            const uint32_t snapshotTick =
                (hasActiveMatch && impl_->matchFlow.snapshot.valid)
                    ? impl_->matchFlow.snapshot.snapshot.serverTick
                    : 0u;
            const uint32_t correctionTick =
                (hasActiveMatch && impl_->matchFlow.correction.valid)
                    ? impl_->matchFlow.correction.correction.serverTick
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
            // If the event was handled and requires an early return, break the loop immediately.
            if (handleServiceEvent(event))
            {
                return;
            }

            if (disconnectEventSeen)
            {
                break;
            }

            // Drain remaining queued events without blocking after the first one.
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

    // =================================================================================================================
    // ===== Runtime API ===============================================================================================
    // =================================================================================================================

    std::optional<uint32_t> NetClient::sendInput(uint8_t buttons) const
    {
        if (impl_ == nullptr || !isConnected())
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

        // Advance the input sequence and store the input in history for potential resend in later batches.
        impl_->nextInputSeq++;
        const uint32_t seq = impl_->nextInputSeq;

        impl_->inputHistory[seq % kMaxInputBatchSize] = buttons;

        //  Batch together as many recent inputs as possible up to the max batch size, starting from the most recent.
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
            // Return the assigned sequence anyway so later redundant batches can still cover it.
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

        // Log every Nth input batch for development.
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

    bool NetClient::sendLobbyReady(const bool ready) const
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

    bool NetClient::sendMatchLoaded(const uint32_t matchId) const
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

    bool NetClient::tryGetLatestSnapshot(MsgSnapshot& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.snapshot.valid)
        {
            return false;
        }

        out = impl_->matchFlow.snapshot.snapshot;
        return true;
    }

    uint32_t NetClient::lastSnapshotTick() const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.snapshot.valid)
        {
            return 0;
        }

        return impl_->matchFlow.snapshot.snapshot.serverTick;
    }

    bool NetClient::tryGetLatestCorrection(MsgCorrection& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.correction.valid)
        {
            return false;
        }

        out = impl_->matchFlow.correction.correction;
        return true;
    }

    uint32_t NetClient::lastCorrectionTick() const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.correction.valid)
        {
            return 0;
        }

        return impl_->matchFlow.correction.correction.serverTick;
    }

    bool NetClient::tryGetLatestLobbyState(MsgLobbyState& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->cachedLobbyState.valid)
        {
            return false;
        }

        out = impl_->cachedLobbyState.lobbyState;
        return true;
    }

    uint32_t NetClient::lobbySilenceMs() const
    {
        if (impl_ == nullptr || !isConnected())
        {
            return 0;
        }

        return elapsedSinceOrZero(impl_->lastLobbyStateReceiveTime, impl_->connectedStartTime);
    }

    bool NetClient::consumePendingLevelInfo(MsgLevelInfo& out)
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.hasLevelInfo || !impl_->matchFlow.levelInfoPending)
        {
            return false;
        }

        out = impl_->matchFlow.levelInfo;
        impl_->matchFlow.levelInfoPending = false;
        return true;
    }

    bool NetClient::tryDequeueGameplayEvent(GameplayEvent& out)
    {
        if (impl_ == nullptr ||
            !isConnected() ||
            impl_->matchFlow.brokenGameplayEventStream ||
            impl_->matchFlow.pendingGameplayEvents.empty())
        {
            return false;
        }

        out = impl_->matchFlow.pendingGameplayEvents.front();
        impl_->matchFlow.pendingGameplayEvents.pop_front();
        return true;
    }

    bool NetClient::hasBrokenGameplayEventStream() const
    {
        return impl_ != nullptr &&
               isConnected() &&
               impl_->matchFlow.hasLevelInfo &&
               impl_->matchFlow.brokenGameplayEventStream;
    }

    uint32_t NetClient::gameplaySilenceMs() const
    {
        if (impl_ == nullptr || !isConnected())
        {
            return 0;
        }

        return elapsedSinceOrZero(impl_->lastGameplayReceiveTime, impl_->connectedStartTime);
    }

    bool NetClient::tryGetLatestMatchStart(MsgMatchStart& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.matchStart.has_value())
        {
            return false;
        }

        out = *impl_->matchFlow.matchStart;
        return true;
    }

    bool NetClient::hasMatchStarted(const uint32_t matchId) const
    {
        return impl_ != nullptr &&
               matchId != 0 &&
               impl_->matchFlow.matchStart.has_value() &&
               impl_->matchFlow.matchStart->matchId == matchId;
    }

    bool NetClient::isMatchCancelled(const uint32_t matchId) const
    {
        return impl_ != nullptr &&
               matchId != 0 &&
               impl_->matchFlow.cancelledMatchId.has_value() &&
               impl_->matchFlow.cancelledMatchId.value() == matchId;
    }

    bool NetClient::tryGetLatestMatchResult(MsgMatchResult& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.matchResult.has_value())
        {
            return false;
        }

        out = *impl_->matchFlow.matchResult;
        return true;
    }

    bool NetClient::tryGetMapSeed(uint32_t& outSeed) const
    {
        if (impl_ == nullptr || !impl_->matchFlow.hasLevelInfo)
        {
            return false;
        }
        outSeed = impl_->matchFlow.levelInfo.mapSeed;
        return true;
    }

    // =================================================================================================================
    // ===== Protocol Handlers =========================================================================================
    // =================================================================================================================

    void NetClient::handleWelcome(const uint8_t* payload, std::size_t payloadSize)
    {
        if (state_ != EConnectState::Handshaking)
        {
            LOG_NET_PROTO_WARN("Welcome received in invalid state {} - ignoring", connectStateName(state_));
            return;
        }

        MsgWelcome welcome{};
        if (!deserializeMsgWelcome(payload, payloadSize, welcome))
        {
            LOG_NET_PROTO_WARN("Failed to parse Welcome payload");
            state_ = EConnectState::FailedHandshake;
            return;
        }
        if (welcome.protocolVersion != kProtocolVersion)
        {
            LOG_NET_PROTO_ERROR("Protocol mismatch (server={}, client={})", welcome.protocolVersion, kProtocolVersion);
            state_ = EConnectState::FailedProtocol;
            return;
        }
        playerId_ = welcome.playerId;
        serverTickRate_ = welcome.serverTickRate;
        state_ = EConnectState::Connected;
        impl_->connectedStartTime = SteadyClock::now();
        impl_->diagnostics.recordWelcome(welcome.playerId,
                                         welcome.serverTickRate,
                                         impl_->handshakeStartTime != TimePoint{}
                                             ? static_cast<uint64_t>(elapsedMs(impl_->handshakeStartTime))
                                             : 0u,
                                         impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0);
        LOG_NET_CONN_INFO("Welcome: playerId={}, tickRate={} - session connected, waiting for lobby or next round start",
                          welcome.playerId,
                          welcome.serverTickRate);
    }

    void NetClient::handleReject(const uint8_t* payload, std::size_t payloadSize)
    {
        if (state_ != EConnectState::Handshaking && state_ != EConnectState::Connecting)
        {
            LOG_NET_PROTO_WARN("Reject received in invalid state {} - ignoring", connectStateName(state_));
            return;
        }

        MsgReject reject{};
        if (!deserializeMsgReject(payload, payloadSize, reject))
        {
            LOG_NET_PROTO_WARN("Failed to parse Reject payload - treating as generic handshake failure");
            lastRejectReason_.reset();
            state_ = EConnectState::FailedHandshake;
            return;
        }

        lastRejectReason_ = reject.reason;
        std::string rejectNote = "server reject";
        switch (reject.reason)
        {
            using enum MsgReject::EReason;
            using enum EConnectState;

            case VersionMismatch:
                rejectNote = "server reject version mismatch";
                LOG_NET_CONN_ERROR("Server rejected: version mismatch (server expects v{})", reject.expectedProtocolVersion);
                state_ = FailedProtocol;
                break;
            case ServerFull:
                rejectNote = "server reject full";
                LOG_NET_CONN_WARN("Server rejected: server is full");
                state_ = FailedHandshake;
                break;
            case Banned:
                rejectNote = "server reject banned";
                LOG_NET_CONN_WARN("Server rejected: banned");
                state_ = FailedHandshake;
                break;
            case GameInProgress:
                rejectNote = "server reject game in progress";
                LOG_NET_CONN_WARN("Server rejected: game already in progress");
                state_ = FailedHandshake;
                break;
            case Other:
            default:
                rejectNote = "server reject other";
                LOG_NET_CONN_WARN("Server rejected: reason={}", static_cast<int>(reject.reason));
                state_ = FailedHandshake;
                break;
        }
        impl_->diagnostics.recordPeerLifecycle(NetPeerLifecycleType::PeerRejected,
                                               std::nullopt,
                                               impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0,
                                               rejectNote);
    }

    void NetClient::handleLevelInfo(const uint8_t* payload, std::size_t payloadSize)
    {
        if (impl_ == nullptr)
        {
            return;
        }
        if (state_ != EConnectState::Connected)
        {
            LOG_NET_PROTO_WARN("LevelInfo received in invalid state {} - ignoring", connectStateName(state_));
            return;
        }
        if (playerId_ == kInvalidPlayerId || serverTickRate_ == 0)
        {
            LOG_NET_PROTO_WARN("LevelInfo received without a valid connected session - ignoring");
            return;
        }

        MsgLevelInfo levelInfo{};
        if (!deserializeMsgLevelInfo(payload, payloadSize, levelInfo))
        {
            LOG_NET_PROTO_WARN("Failed to parse LevelInfo payload");
            return;
        }

        if (impl_->matchFlow.hasLevelInfo &&
            levelInfo.matchId <= impl_->matchFlow.levelInfo.matchId)
        {
            LOG_NET_CONN_DEBUG("Ignoring stale or duplicate LevelInfo matchId={} latestMatchId={}",
                               levelInfo.matchId,
                               impl_->matchFlow.levelInfo.matchId);
            return;
        }

        impl_->matchFlow.beginBootstrap(levelInfo);
        impl_->cachedLobbyState = {};
        resetLocalMatchBootstrapState();
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = playerId_;
            event.detailA = levelInfo.matchId;
            event.detailB = levelInfo.mapSeed;
            event.note = "level info received";
            impl_->diagnostics.recordEvent(event);
        }
        LOG_NET_CONN_INFO("Received LevelInfo matchId={} seed={}",
                          levelInfo.matchId,
                          levelInfo.mapSeed);
    }

    void NetClient::handleLobbyState(const uint8_t* payload, std::size_t payloadSize) const
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_CONN_DEBUG("Received LobbyState payload while not connected - ignoring");
            return;
        }

        MsgLobbyState lobbyState{};
        if (!deserializeMsgLobbyState(payload, payloadSize, lobbyState))
        {
            LOG_NET_PROTO_WARN("Failed to parse LobbyState payload");
            return;
        }

        if (impl_->matchFlow.hasLevelInfo)
        {
            clearCurrentMatchSession();
        }

        impl_->cachedLobbyState = {lobbyState, true};
        impl_->lastLobbyStateReceiveTime = SteadyClock::now();
        LOG_NET_CONN_TRACE("Received LobbyState update");
    }

    void NetClient::handleMatchStart(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_CONN_DEBUG("Received MatchStart payload while not connected - ignoring");
            return;
        }

        MsgMatchStart matchStart{};
        if (!deserializeMsgMatchStart(payload, payloadSize, matchStart))
        {
            LOG_NET_PROTO_WARN("Failed to parse MatchStart payload");
            return;
        }

        if (impl_->matchFlow.matchStart.has_value() &&
            matchStart.matchId <= impl_->matchFlow.matchStart->matchId)
        {
            LOG_NET_CONN_DEBUG("Ignoring stale or duplicate MatchStart matchId={} latestMatchId={}",
                               matchStart.matchId,
                               impl_->matchFlow.matchStart->matchId);
            return;
        }

        if (!impl_->matchFlow.isActiveMatch(matchStart.matchId))
        {
            LOG_NET_CONN_DEBUG("Ignoring MatchStart for inactive matchId={} currentMatchId={}",
                               matchStart.matchId,
                               impl_->matchFlow.currentMatchId());
            return;
        }

        impl_->matchFlow.matchStart = matchStart;
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = playerId_;
            event.detailA = matchStart.matchId;
            event.detailB = matchStart.unlockServerTick;
            event.note = "match start received";
            impl_->diagnostics.recordEvent(event);
        }
        LOG_NET_CONN_INFO("Received MatchStart matchId={} goTick={} unlockTick={}",
                          matchStart.matchId,
                          matchStart.goShowServerTick,
                          matchStart.unlockServerTick);
    }

    void NetClient::handleMatchCancelled(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_CONN_DEBUG("Received MatchCancelled payload while not connected - ignoring");
            return;
        }

        MsgMatchCancelled matchCancelled{};
        if (!deserializeMsgMatchCancelled(payload, payloadSize, matchCancelled))
        {
            LOG_NET_PROTO_WARN("Failed to parse MatchCancelled payload");
            return;
        }

        if (impl_->matchFlow.cancelledMatchId.has_value() &&
            matchCancelled.matchId <= impl_->matchFlow.cancelledMatchId.value())
        {
            LOG_NET_CONN_DEBUG("Ignoring stale or duplicate MatchCancelled matchId={} latestMatchId={}",
                               matchCancelled.matchId,
                               impl_->matchFlow.cancelledMatchId.value());
            return;
        }

        if (impl_->matchFlow.isActiveMatch(matchCancelled.matchId))
        {
            clearCurrentMatchSession();
        }

        impl_->matchFlow.cancelledMatchId = matchCancelled.matchId;
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = playerId_;
            event.detailA = matchCancelled.matchId;
            event.note = "match cancelled received";
            impl_->diagnostics.recordEvent(event);
        }
        LOG_NET_CONN_INFO("Received MatchCancelled matchId={}", matchCancelled.matchId);
    }

    void NetClient::handleMatchResult(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_CONN_DEBUG("Received MatchResult payload while not connected - ignoring");
            return;
        }

        MsgMatchResult matchResult{};
        if (!deserializeMsgMatchResult(payload, payloadSize, matchResult))
        {
            LOG_NET_PROTO_WARN("Failed to parse MatchResult payload");
            return;
        }

        if (!impl_->matchFlow.isActiveMatch(matchResult.matchId))
        {
            LOG_NET_CONN_DEBUG("Ignoring MatchResult for inactive matchId={} currentMatchId={}",
                               matchResult.matchId,
                               impl_->matchFlow.currentMatchId());
            return;
        }

        if (impl_->matchFlow.matchResult.has_value() &&
            matchResult.matchId <= impl_->matchFlow.matchResult->matchId)
        {
            LOG_NET_CONN_DEBUG("Ignoring stale or duplicate MatchResult matchId={} latestMatchId={}",
                               matchResult.matchId,
                               impl_->matchFlow.matchResult->matchId);
            return;
        }

        impl_->matchFlow.matchResult = matchResult;
        const bool localWon = matchResult.result == MsgMatchResult::EResult::Win &&
                              playerId_ != kInvalidPlayerId &&
                              matchResult.winnerPlayerId == playerId_;
        const char* localResultName = matchResult.result == MsgMatchResult::EResult::Draw
                                          ? "draw"
                                          : (localWon ? "win" : "loss");
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = matchResult.result == MsgMatchResult::EResult::Win
                               ? matchResult.winnerPlayerId
                               : kInvalidPlayerId;
            event.detailA = matchResult.matchId;
            event.note = std::string("match result received: ") + localResultName;
            impl_->diagnostics.recordEvent(event);
        }
        LOG_NET_CONN_INFO("Received MatchResult matchId={} result={}",
                          matchResult.matchId,
                          localResultName);
    }

    void NetClient::handleSnapshot(const uint8_t* payload, std::size_t payloadSize) const
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Snapshot payload while not connected - ignoring");
            return;
        }

        MsgSnapshot snapshot;
        if (!deserializeMsgSnapshot(payload, payloadSize, snapshot))
        {
            LOG_NET_PROTO_WARN("Failed to parse Snapshot payload");
            return;
        }
        if (!impl_->matchFlow.isActiveMatch(snapshot.matchId))
        {
            LOG_NET_SNAPSHOT_DEBUG("Ignoring Snapshot for inactive matchId={} currentMatchId={}",
                                   snapshot.matchId,
                                   impl_->matchFlow.currentMatchId());
            return;
        }

        if (impl_->matchFlow.snapshot.valid && snapshot.serverTick <= impl_->matchFlow.snapshot.snapshot.serverTick)
        {
            impl_->diagnostics.recordStaleSnapshotIgnored(snapshot.serverTick);
            return;
        }

        impl_->matchFlow.snapshot = {snapshot, true};
        impl_->lastSnapshotReceiveTime = SteadyClock::now();
        impl_->lastGameplayReceiveTime = impl_->lastSnapshotReceiveTime;

        if (snapshot.serverTick % kSnapshotLogEveryN == 0)
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Snapshot tick={} playerCount={} bombCount={}",
                                   snapshot.serverTick,
                                   snapshot.playerCount,
                                   snapshot.bombCount);
        }
    }

    void NetClient::handleCorrection(const uint8_t* payload, std::size_t payloadSize) const
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Correction payload while not connected - ignoring");
            return;
        }

        MsgCorrection correction{};
        if (!deserializeMsgCorrection(payload, payloadSize, correction))
        {
            LOG_NET_PROTO_WARN("Failed to parse Correction payload");
            return;
        }
        if (!impl_->matchFlow.isActiveMatch(correction.matchId))
        {
            LOG_NET_SNAPSHOT_DEBUG("Ignoring Correction for inactive matchId={} currentMatchId={}",
                                   correction.matchId,
                                   impl_->matchFlow.currentMatchId());
            return;
        }

        // Only cache the correction if it's newer than the currently cached one.
        if (impl_->matchFlow.correction.valid && correction.serverTick <= impl_->matchFlow.correction.correction.serverTick)
        {
            impl_->diagnostics.recordStaleCorrectionIgnored(correction.serverTick, correction.lastProcessedInputSeq);
            return;
        }

        impl_->matchFlow.correction = {correction, true};
        impl_->lastCorrectionReceiveTime = SteadyClock::now();
        impl_->lastGameplayReceiveTime = impl_->lastCorrectionReceiveTime;

        if (correction.serverTick % kSnapshotLogEveryN == 0)
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Correction tick={} lastProcessed={} pos=({}, {})",
                                   correction.serverTick,
                                   correction.lastProcessedInputSeq,
                                   correction.xQ,
                                   correction.yQ);
        }
    }

    void NetClient::handleBombPlaced(const uint8_t* payload, std::size_t payloadSize) const
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received BombPlaced payload while not connected - ignoring");
            return;
        }

        MsgBombPlaced bombPlaced{};
        if (!deserializeMsgBombPlaced(payload, payloadSize, bombPlaced))
        {
            LOG_NET_PROTO_WARN("Failed to parse BombPlaced payload");
            return;
        }
        if (!impl_->matchFlow.isActiveMatch(bombPlaced.matchId))
        {
            LOG_NET_SNAPSHOT_DEBUG("Ignoring BombPlaced for inactive matchId={} currentMatchId={}",
                                   bombPlaced.matchId,
                                   impl_->matchFlow.currentMatchId());
            return;
        }

        enqueueGameplayEvent(GameplayEvent::fromBombPlaced(bombPlaced));

        LOG_NET_SNAPSHOT_DEBUG("Received BombPlaced tick={} ownerId={} cell=({}, {}) radius={} explodeTick={}",
                               bombPlaced.serverTick,
                               bombPlaced.ownerId,
                               bombPlaced.col,
                               bombPlaced.row,
                               bombPlaced.radius,
                               bombPlaced.explodeTick);
    }

    void NetClient::handleExplosionResolved(const uint8_t* payload, std::size_t payloadSize) const
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received ExplosionResolved payload while not connected - ignoring");
            return;
        }

        MsgExplosionResolved explosion{};
        if (!deserializeMsgExplosionResolved(payload, payloadSize, explosion))
        {
            LOG_NET_PROTO_WARN("Failed to parse ExplosionResolved payload");
            return;
        }
        if (!impl_->matchFlow.isActiveMatch(explosion.matchId))
        {
            LOG_NET_SNAPSHOT_DEBUG("Ignoring ExplosionResolved for inactive matchId={} currentMatchId={}",
                                   explosion.matchId,
                                   impl_->matchFlow.currentMatchId());
            return;
        }

        enqueueGameplayEvent(GameplayEvent::fromExplosionResolved(explosion));

        LOG_NET_SNAPSHOT_DEBUG("Received ExplosionResolved tick={} ownerId={} origin=({}, {}) blastCells={} destroyedBricks={} killedMask=0x{:02x}",
                               explosion.serverTick,
                               explosion.ownerId,
                               explosion.originCol,
                               explosion.originRow,
                               explosion.blastCellCount,
                               explosion.destroyedBrickCount,
                               explosion.killedPlayerMask);
    }

    void NetClient::enqueueGameplayEvent(const GameplayEvent& event) const
    {
        if (impl_ == nullptr)
        {
            return;
        }

        if (impl_->matchFlow.brokenGameplayEventStream)
        {
            return;
        }

        if (impl_->matchFlow.pendingGameplayEvents.size() >= kMaxPendingGameplayEvents)
        {
            LOG_NET_SNAPSHOT_ERROR(
                "Reliable gameplay event queue overflow for matchId={} - invalidating gameplay event stream",
                impl_->matchFlow.currentMatchId());
            impl_->diagnostics.recordBrokenGameplayEventStream(impl_->matchFlow.currentMatchId());
            impl_->matchFlow.brokenGameplayEventStream = true;
            impl_->matchFlow.pendingGameplayEvents.clear();
            return;
        }

        impl_->matchFlow.pendingGameplayEvents.push_back(event);
        impl_->lastGameplayReceiveTime = SteadyClock::now();
        impl_->diagnostics.samplePendingGameplayEventDepth(impl_->matchFlow.pendingGameplayEvents.size());
    }

    // =================================================================================================================
    // ===== Internal Helpers ==========================================================================================
    // =================================================================================================================

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

    void NetClient::finalizeDiagnosticsSession(const EConnectState finalState, const bool /*preserveRejectReason*/)
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

    void NetClient::destroyTransport() const
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
        clearSessionState();
    }

    void NetClient::clearSessionState(const bool clearRejectReason)
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
            impl_->matchFlow.clearAll();
            impl_->cachedLobbyState = {};
        }
    }

    void NetClient::failConnection(const EConnectState failureState, const bool clearRejectReason)
    {
        finalizeDiagnosticsSession(failureState, !clearRejectReason);
        destroyTransport();
        clearSessionState(clearRejectReason);
        state_ = failureState;
    }

} // namespace bomberman::net
