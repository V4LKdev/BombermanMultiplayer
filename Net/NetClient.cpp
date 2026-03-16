#include "NetClient.h"
#include "NetSend.h"
#include "NetTransportConfig.h"
#include "PacketDispatch.h"

#include "Util/Log.h"

#include <algorithm>
#include <chrono>
#include <enet/enet.h>

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Local Helpers =============================================================================================
    // =================================================================================================================

    namespace
    {
        constexpr int kConnectTimeoutMs = 5000;
        constexpr int kDisconnectTimeoutMs = 5000;
        constexpr int kDisconnectPollTimeoutMs = 100;

        using SteadyClock = std::chrono::steady_clock;
        using TimePoint   = SteadyClock::time_point;

        /** @brief Returns elapsed milliseconds since a given time point. */
        int elapsedMs(const TimePoint& since)
        {
            return static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    SteadyClock::now() - since).count());
        }
    }

    /** @brief Opaque ENet implementation visible only in this translation unit. */
    struct NetClient::Impl
    {
        ENetHost* host = nullptr;
        ENetPeer* peer = nullptr;
        PacketDispatcher<NetClient> dispatcher;

        // ---- Input batching state ----
        uint32_t nextInputSeq = 0;                      ///< Monotonic input sequence counter.
        uint8_t inputHistory[kMaxInputBatchSize]{};     ///< Recent button states for redundant resend batching.

        // ---- Async connect/handshake state (cleared by resetState()) ----
        std::string pendingPlayerName;
        TimePoint connectStartTime{};
        TimePoint handshakeStartTime{};
        TimePoint disconnectStartTime{};
        TimePoint connectedStartTime{};
        TimePoint lastGameplayReceiveTime{};
        TimePoint lastSnapshotReceiveTime{};
        TimePoint lastCorrectionReceiveTime{};

        // ---- Session caches (populated by protocol handlers, cleared on disconnect) ----

        struct ReceivedSnapshotCache
        {
            MsgSnapshot snapshot{};
            bool valid = false;
        };
        ReceivedSnapshotCache lastSnapshot{};

        struct ReceivedCorrectionCache
        {
            MsgCorrection correction{};
            bool valid = false;
        };
        ReceivedCorrectionCache lastCorrection{};

        struct ReceivedLevelInfoCache
        {
            MsgLevelInfo levelInfo{};
            bool valid = false;
        };
        ReceivedLevelInfoCache lastLevelInfo{};

        /** @brief Dispatcher trampolines. */
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
    };

    // =================================================================================================================
    // ===== Construction And Lifetime =================================================================================
    // =================================================================================================================

    NetClient::NetClient() : impl_(std::make_unique<Impl>())
    {
        impl_->dispatcher.bind(EMsgType::Welcome,   &Impl::onWelcome);
        impl_->dispatcher.bind(EMsgType::Reject,    &Impl::onReject);
        impl_->dispatcher.bind(EMsgType::LevelInfo, &Impl::onLevelInfo);
        impl_->dispatcher.bind(EMsgType::Snapshot,  &Impl::onSnapshot);
        impl_->dispatcher.bind(EMsgType::Correction,&Impl::onCorrection);
    }

    NetClient::~NetClient()
    {
        disconnect();
        shutdownENet();
    }

    NetClient::NetClient(NetClient&&) noexcept = default;
    NetClient& NetClient::operator=(NetClient&&) noexcept = default;

    // =================================================================================================================
    // ===== Connection Lifecycle ======================================================================================
    // =================================================================================================================

    void NetClient::beginConnect(const std::string& host, uint16_t port, std::string_view playerName)
    {
        if (!impl_)
        {
            state_ = EConnectState::FailedInit;
            return;
        }

        // Already connected or in-progress: ignore duplicate calls.
        if (isConnected() ||
            state_ == EConnectState::Connecting ||
            state_ == EConnectState::Handshaking ||
            state_ == EConnectState::Disconnecting)
        {
            LOG_NET_CONN_DEBUG("beginConnect() called while already in state {} - ignoring", connectStateName(state_));
            return;
        }

        if (!initializeENet())
        {
            state_ = EConnectState::FailedInit;
            return;
        }

        // Create client ENet host: 1 peer, kChannelCount channels, unlimited bandwidth.
        impl_->host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
        if (impl_->host == nullptr)
        {
            LOG_CLIENT_ERROR("Failed to create ENet client host");
            state_ = EConnectState::FailedInit;
            destroyTransport();
            return;
        }

        // Resolve server host.
        ENetAddress address{};
        if (enet_address_set_host(&address, host.c_str()) != 0)
        {
            LOG_CLIENT_ERROR("Invalid host address: {}", host);
            state_ = EConnectState::FailedResolve;
            destroyTransport();
            return;
        }

        address.port = port;

        // Request kChannelCount channels, no custom connect data.
        impl_->peer = enet_host_connect(impl_->host, &address, kChannelCount, 0);
        if (impl_->peer == nullptr)
        {
            LOG_CLIENT_ERROR("Failed to create ENet peer");
            state_ = EConnectState::FailedConnect;
            destroyTransport();
            return;
        }

        // Store pending state for async handshake in pump().
        impl_->pendingPlayerName = std::string(playerName);
        impl_->connectStartTime = SteadyClock::now();
        lastRejectReason_.reset();

        state_ = EConnectState::Connecting;
        LOG_NET_CONN_DEBUG("Async connect initiated to {}:{}", host, port);
    }

    void NetClient::cancelConnect()
    {
        if (state_ != EConnectState::Connecting && state_ != EConnectState::Handshaking)
            return;

        LOG_NET_CONN_DEBUG("Connect attempt cancelled (was {})", connectStateName(state_));
        destroyTransport();
        resetState();
    }

    void NetClient::requestGracefulDisconnect()
    {
        if (!impl_ || !impl_->peer)
            return;

        if (state_ == EConnectState::Disconnecting)
            return;

        enet_peer_disconnect(impl_->peer, 0);
        flush(impl_->host);
        impl_->disconnectStartTime = SteadyClock::now();
        state_ = EConnectState::Disconnecting;
    }

    void NetClient::beginDisconnect()
    {
        if (!impl_)
            return;

        if (state_ == EConnectState::Disconnected)
            return;

        if (state_ == EConnectState::Connecting || state_ == EConnectState::Handshaking)
        {
            LOG_NET_CONN_DEBUG("Disconnect requested during {} - cancelling connect attempt", connectStateName(state_));
            cancelConnect();
            return;
        }

        if (state_ == EConnectState::Disconnecting)
            return;

        if (!impl_->peer || !impl_->host)
        {
            destroyTransport();
            resetState();
            return;
        }

        requestGracefulDisconnect();
        LOG_NET_CONN_INFO("Queued graceful disconnect");
    }

    bool NetClient::drainGracefulDisconnect()
    {
        if (!impl_)
            return false;

        if (state_ == EConnectState::Disconnected)
            return true;

        if (state_ == EConnectState::Connecting || state_ == EConnectState::Handshaking)
        {
            cancelConnect();
            return true;
        }

        if (!impl_->peer || !impl_->host)
            return false;

        requestGracefulDisconnect();
        ENetEvent event{};

        while (elapsedMs(impl_->disconnectStartTime) < kDisconnectTimeoutMs)
        {
            const int serviceResult = enet_host_service(impl_->host, &event, kDisconnectPollTimeoutMs);
            if (serviceResult < 0)
                break;
            if (serviceResult == 0)
                continue;

            if (event.type == ENET_EVENT_TYPE_RECEIVE)
            {
                enet_packet_destroy(event.packet);
                continue;
            }

            if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                return true;
        }

        LOG_NET_CONN_WARN("Graceful disconnect timed out after {}ms; tearing down transport locally", kDisconnectTimeoutMs);
        return false;
    }

    bool NetClient::disconnect()
    {
        const bool graceful = drainGracefulDisconnect();
        destroyTransport();
        resetState();
        return graceful;
    }

    // =================================================================================================================
    // ===== Event Pump and Sub-Helpers ================================================================================
    // =================================================================================================================

    bool NetClient::checkConnectTimeouts()
    {
        if (state_ == EConnectState::Connecting)
        {
            if (elapsedMs(impl_->connectStartTime) >= kConnectTimeoutMs)
            {
                LOG_NET_CONN_WARN("Async connect timeout ({}ms)", kConnectTimeoutMs);
                state_ = EConnectState::FailedConnect;
                destroyTransport();
                return true;
            }
        }
        else if (state_ == EConnectState::Handshaking)
        {
            if (elapsedMs(impl_->handshakeStartTime) >= kConnectTimeoutMs)
            {
                LOG_NET_CONN_WARN("Async handshake timeout ({}ms)", kConnectTimeoutMs);
                state_ = EConnectState::FailedHandshake;
                destroyTransport();
                return true;
            }
        }
        return false;
    }

    bool NetClient::checkDisconnectTimeout()
    {
        if (state_ != EConnectState::Disconnecting || !impl_)
            return false;

        if (elapsedMs(impl_->disconnectStartTime) < kDisconnectTimeoutMs)
            return false;

        LOG_NET_CONN_WARN("Graceful disconnect timed out after {}ms; tearing down transport locally", kDisconnectTimeoutMs);
        destroyTransport();
        resetState();
        return true;
    }

    bool NetClient::handleEnetConnect()
    {
        if (state_ != EConnectState::Connecting)
        {
            LOG_NET_CONN_DEBUG("Ignoring unexpected CONNECT event in state {}", connectStateName(state_));
            return false;
        }

        applyDefaultPeerTransportConfig(impl_->peer);

        LOG_NET_CONN_DEBUG("ENet connect event received, sending Hello");

        const auto helloPacket =
            makeHelloPacket(impl_->pendingPlayerName, kProtocolVersion);

        if (!queueReliableControl(impl_->peer, helloPacket))
        {
            LOG_NET_CONN_ERROR("Failed to send Hello packet");
            state_ = EConnectState::FailedHandshake;
            destroyTransport();
            return true;
        }
        flush(impl_->host);

        state_ = EConnectState::Handshaking;
        impl_->handshakeStartTime = SteadyClock::now();
        // handleWelcome() will receive the Welcome, then handleLevelInfo()
        // will flip state_ to Connected once the full handshake completes.
        return false;
    }

    bool NetClient::handleEnetReceive(const uint8_t* data, std::size_t dataLength, uint8_t channelID)
    {
        LOG_NET_PACKET_TRACE("Received {} bytes on channel {}", dataLength, channelName(channelID));

        PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;
        if (!tryParsePacket(data, dataLength, header, payload, payloadSize))
        {
            LOG_NET_PACKET_WARN("Failed to deserialize PacketHeader (malformed or truncated, {} bytes)", dataLength);
            return false;
        }

        if (!isExpectedChannelFor(header.type, channelID))
        {
            LOG_NET_PACKET_WARN("Rejected {} on wrong channel: got {}, expected {}",
                                msgTypeName(header.type),
                                channelName(channelID),
                                channelName(static_cast<uint8_t>(expectedChannelFor(header.type))));
            return false;
        }

        if (!impl_->dispatcher.dispatch(*this, header, payload, payloadSize))
        {
            LOG_NET_PACKET_TRACE("No handler for message type 0x{:02x}", static_cast<int>(header.type));
            return false;
        }

        // A handler may have moved us into a terminal failure state (e.g. protocol mismatch).
        if (isFailedState(state_))
        {
            destroyTransport();
            return true;
        }

        return false;
    }

    void NetClient::handleEnetDisconnect()
    {
        if (state_ == EConnectState::Disconnecting)
        {
            LOG_NET_CONN_INFO("Graceful disconnect completed");
        }

        destroyTransport();
        resetState();
    }

    void NetClient::pump(uint16_t timeoutMs)
    {
        if (!impl_ || !impl_->host)
            return;

        if (checkDisconnectTimeout())
            return;

        if (checkConnectTimeouts())
            return;

        ENetEvent event{};
        bool shouldDisconnect = false;

        while (enet_host_service(impl_->host, &event, timeoutMs) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_RECEIVE:
            {
                if (state_ == EConnectState::Disconnecting)
                {
                    enet_packet_destroy(event.packet);
                    break;
                }

                const bool earlyOut = handleEnetReceive(event.packet->data, event.packet->dataLength, event.channelID);
                enet_packet_destroy(event.packet);
                if (earlyOut) return;
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
                if (state_ != EConnectState::Disconnecting)
                {
                    LOG_NET_CONN_WARN("Disconnected from server (transport close or timeout)");
                }
                shouldDisconnect = true;
                break;

            case ENET_EVENT_TYPE_CONNECT:
                if (handleEnetConnect()) return;
                break;

            case ENET_EVENT_TYPE_NONE:
                break;
            }

            // Drain remaining events without blocking after first event.
            timeoutMs = 0;

            if (shouldDisconnect) break;
        }

        if (shouldDisconnect) handleEnetDisconnect();
    }

    // =================================================================================================================
    // ===== Public Runtime I/O ========================================================================================
    // =================================================================================================================

    std::optional<uint32_t> NetClient::sendInput(uint8_t buttons)
    {
        if (!impl_ || !isConnected())
            return std::nullopt;

        // Advance the monotonic input sequence.
        const uint32_t seq = ++impl_->nextInputSeq;

        // Store in the recent input history for redundant resend batching.
        impl_->inputHistory[seq % kMaxInputBatchSize] = buttons;

        // Build a batched input packet with up to kMaxInputBatchSize recent entries.
        const uint8_t batchCount = static_cast<uint8_t>(
            std::min<uint32_t>(seq, kMaxInputBatchSize));
        const uint32_t baseSeq = seq - batchCount + 1;

        MsgInput msg{};
        msg.baseInputSeq = baseSeq;
        msg.count = batchCount;
        for (uint8_t i = 0; i < batchCount; ++i)
        {
            msg.inputs[i] = impl_->inputHistory[(baseSeq + i) % kMaxInputBatchSize];
        }

        const auto bytes = makeInputPacket(msg);

        if (!queueUnreliableInput(impl_->peer, bytes))
            return seq;

        if ((seq % kInputLogEveryN) == 0)
        {
            LOG_NET_INPUT_DEBUG("Sent Input seq={} batch=[{}..{}] buttons=0x{:02x}",
                                seq, baseSeq, seq, buttons);
        }

        return seq;
    }

    void NetClient::flushOutgoing()
    {
        if (!impl_ || !impl_->host)
            return;

        flush(impl_->host);
    }

    bool NetClient::tryGetLatestSnapshot(MsgSnapshot& out) const
    {
        if (!impl_ || !isConnected() || !impl_->lastSnapshot.valid)
            return false;

        out = impl_->lastSnapshot.snapshot;
        return true;
    }

    uint32_t NetClient::lastSnapshotTick() const
    {
        if (!impl_ || !isConnected() || !impl_->lastSnapshot.valid)
            return 0;

        return impl_->lastSnapshot.snapshot.serverTick;
    }

    bool NetClient::tryGetLatestCorrection(MsgCorrection& out) const
    {
        if (!impl_ || !isConnected() || !impl_->lastCorrection.valid)
            return false;

        out = impl_->lastCorrection.correction;
        return true;
    }

    uint32_t NetClient::lastCorrectionTick() const
    {
        if (!impl_ || !isConnected() || !impl_->lastCorrection.valid)
            return 0;

        return impl_->lastCorrection.correction.serverTick;
    }

    uint32_t NetClient::gameplaySilenceMs() const
    {
        if (!impl_ || !isConnected())
            return 0;

        const TimePoint since =
            (impl_->lastGameplayReceiveTime != TimePoint{})
                ? impl_->lastGameplayReceiveTime
                : impl_->connectedStartTime;
        if (since == TimePoint{})
            return 0;

        return static_cast<uint32_t>(elapsedMs(since));
    }

    bool NetClient::tryGetMapSeed(uint32_t& outSeed) const
    {
        if (!impl_ || !impl_->lastLevelInfo.valid)
            return false;
        outSeed = impl_->lastLevelInfo.levelInfo.mapSeed;
        return true;
    }

    // =================================================================================================================
    // ===== Private Protocol Handlers =================================================================================
    // =================================================================================================================

    void NetClient::handleWelcome(const uint8_t* payload, std::size_t payloadSize)
    {
        MsgWelcome welcome{};
        if (!deserializeMsgWelcome(payload, payloadSize, welcome))
        {
            LOG_NET_PROTO_WARN("Failed to parse Welcome payload");
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

        LOG_NET_CONN_INFO("Welcome: playerId={}, tickRate={} - awaiting LevelInfo", welcome.playerId, welcome.serverTickRate);
    }

    void NetClient::handleReject(const uint8_t* payload, std::size_t payloadSize)
    {
        MsgReject reject{};
        if (!deserializeMsgReject(payload, payloadSize, reject))
        {
            LOG_NET_PROTO_WARN("Failed to parse Reject payload - treating as generic handshake failure");
            lastRejectReason_.reset();
            state_ = EConnectState::FailedHandshake;
            return;
        }

        lastRejectReason_ = reject.reason;

        switch (reject.reason)
        {
            case MsgReject::EReason::VersionMismatch:
                LOG_NET_CONN_ERROR("Server rejected: version mismatch (server expects v{})", reject.expectedProtocolVersion);
                state_ = EConnectState::FailedProtocol;
                break;

            case MsgReject::EReason::ServerFull:
                LOG_NET_CONN_WARN("Server rejected: server is full");
                state_ = EConnectState::FailedHandshake;
                break;

            case MsgReject::EReason::Banned:
                LOG_NET_CONN_WARN("Server rejected: banned");
                state_ = EConnectState::FailedHandshake;
                break;

            case MsgReject::EReason::Other:
            default:
                LOG_NET_CONN_WARN("Server rejected: reason={}", static_cast<int>(reject.reason));
                state_ = EConnectState::FailedHandshake;
                break;
        }
    }

    void NetClient::handleLevelInfo(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_)
            return;

        MsgLevelInfo msgLevelInfo{};
        if (!deserializeMsgLevelInfo(payload, payloadSize, msgLevelInfo))
        {
            LOG_NET_PROTO_WARN("Failed to parse LevelInfo payload");
            return;
        }

        impl_->lastLevelInfo = {msgLevelInfo, true};

        // LevelInfo is the final handshake packet - the session is now fully ready.
        state_ = EConnectState::Connected;
        impl_->connectedStartTime = SteadyClock::now();

        LOG_NET_CONN_INFO("Received LevelInfo seed={} - handshake complete, session ready", msgLevelInfo.mapSeed);
    }

    void NetClient::handleSnapshot(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Snapshot payload while not connected - ignoring");
            return;
        }

        MsgSnapshot snapshot{};
        if (!deserializeMsgSnapshot(payload, payloadSize, snapshot))
        {
            LOG_NET_PROTO_WARN("Failed to parse Snapshot payload");
            return;
        }

        // Only accept if newer than current cached snapshot.
        if (impl_->lastSnapshot.valid && snapshot.serverTick <= impl_->lastSnapshot.snapshot.serverTick)
            return;

        impl_->lastSnapshot = {snapshot, true};
        impl_->lastSnapshotReceiveTime = SteadyClock::now();
        impl_->lastGameplayReceiveTime = impl_->lastSnapshotReceiveTime;

        if (snapshot.serverTick % kSnapshotLogEveryN == 0)
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Snapshot tick={} playerCount={}",
                                   snapshot.serverTick, snapshot.playerCount);
        }
    }

    void NetClient::handleCorrection(const uint8_t* payload, std::size_t payloadSize)
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

        if (impl_->lastCorrection.valid &&
            correction.serverTick <= impl_->lastCorrection.correction.serverTick)
        {
            return;
        }

        impl_->lastCorrection = {correction, true};
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

    // =================================================================================================================
    // ===== Private ENet helpers ======================================================================================
    // =================================================================================================================

    bool NetClient::initializeENet()
    {
        if (initialized_) return true;

        if (enet_initialize() != 0)
        {
            LOG_CLIENT_ERROR("ENet initialization failed");
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

    void NetClient::destroyTransport()
    {
        if (!impl_) return;

        if (impl_->peer)
        {
            enet_peer_reset(impl_->peer);
            impl_->peer = nullptr;
        }

        if (impl_->host)
        {
            enet_host_destroy(impl_->host);
            impl_->host = nullptr;
        }
    }

    void NetClient::resetState()
    {
        state_ = EConnectState::Disconnected;
        playerId_ = kInvalidPlayerId;
        serverTickRate_ = 0;
        lastRejectReason_.reset();
        if (impl_)
        {
            impl_->nextInputSeq = 0;
            std::memset(impl_->inputHistory, 0, sizeof(impl_->inputHistory));
            impl_->pendingPlayerName.clear();
            impl_->connectStartTime   = TimePoint{};
            impl_->handshakeStartTime = TimePoint{};
            impl_->disconnectStartTime = TimePoint{};
            impl_->connectedStartTime = TimePoint{};
            impl_->lastGameplayReceiveTime = TimePoint{};
            impl_->lastSnapshotReceiveTime = TimePoint{};
            impl_->lastCorrectionReceiveTime = TimePoint{};
            impl_->lastSnapshot   = {};
            impl_->lastCorrection = {};
            impl_->lastLevelInfo  = {};
        }
    }

} // namespace bomberman::net
