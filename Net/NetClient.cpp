/**
 * @file NetClient.cpp
 * @brief Implementation of the client-side multiplayer connection hub.
 */

#include "NetClient.h"

#include "NetSend.h"
#include "NetTransportConfig.h"
#include "PacketDispatch.h"
#include "Util/Log.h"

#include <algorithm>
#include <chrono>
#include <cstring>

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

        using SteadyClock = std::chrono::steady_clock;
        using TimePoint = SteadyClock::time_point;

        // Returns the elapsed milliseconds since the given time point.
        int elapsedMs(const TimePoint& since)
        {
            return static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - since).count());
        }

        // Returns true if the message type is a control message expected during handshake.
        constexpr bool isHandshakeControlMessage(EMsgType type)
        {
            using enum EMsgType;

            switch (type)
            {
                case Hello:
                case Welcome:
                case Reject:
                case LevelInfo:
                    return true;
                case Input:
                case Snapshot:
                case Correction:
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
        TimePoint lastGameplayReceiveTime;
        TimePoint lastSnapshotReceiveTime;
        TimePoint lastCorrectionReceiveTime;

        // ----- Cached session data -----
        struct CachedSnapshot
        {
            MsgSnapshot snapshot{};
            bool valid = false;
        };
        CachedSnapshot cachedSnapshot{};

        struct CachedCorrection
        {
            MsgCorrection correction{};
            bool valid = false;
        };
        CachedCorrection cachedCorrection{};

        struct CachedLevelInfo
        {
            MsgLevelInfo levelInfo{};
            bool valid = false;
        };
        CachedLevelInfo cachedLevelInfo{};

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
    // ===== Construction and Lifecycle ================================================================================
    // =================================================================================================================

    NetClient::NetClient()
    {
        impl_ = std::make_unique<Impl>();

        impl_->dispatcher.bind(EMsgType::Welcome, &Impl::onWelcome);
        impl_->dispatcher.bind(EMsgType::Reject, &Impl::onReject);
        impl_->dispatcher.bind(EMsgType::LevelInfo, &Impl::onLevelInfo);
        impl_->dispatcher.bind(EMsgType::Snapshot, &Impl::onSnapshot);
        impl_->dispatcher.bind(EMsgType::Correction, &Impl::onCorrection);
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

        if (!initializeENet())
        {
            state_ = EConnectState::FailedInit;
            return;
        }

        // Client host: one peer, project channel count, unlimited bandwidth.
        impl_->host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
        if (impl_->host == nullptr)
        {
            LOG_NET_CONN_ERROR("Failed to create ENet client host");
            state_ = EConnectState::FailedInit;
            destroyTransport();
            return;
        }

        ENetAddress address{};
        if (enet_address_set_host(&address, host.c_str()) != 0)
        {
            LOG_NET_CONN_ERROR("Invalid host address: {}", host);
            state_ = EConnectState::FailedResolve;
            destroyTransport();
            return;
        }

        address.port = port;

        impl_->peer = enet_host_connect(impl_->host, &address, kChannelCount, 0);
        if (impl_->peer == nullptr)
        {
            LOG_NET_CONN_ERROR("Failed to create ENet peer");
            state_ = EConnectState::FailedConnect;
            destroyTransport();
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
        destroyTransport();
        resetState();
        return completedGracefully;
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
        if (state_ == EConnectState::Connecting && elapsedMs(impl_->connectStartTime) >= kConnectTimeoutMs)
        {
            LOG_NET_CONN_WARN("Async connect timeout ({}ms)", kConnectTimeoutMs);
            failConnection(EConnectState::FailedConnect);
            return true;
        }
        if (state_ == EConnectState::Handshaking && elapsedMs(impl_->handshakeStartTime) >= kConnectTimeoutMs)
        {
            LOG_NET_CONN_WARN("Async handshake timeout ({}ms)", kConnectTimeoutMs);
            failConnection(EConnectState::FailedHandshake);
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

        const auto helloPacket = makeHelloPacket(impl_->pendingPlayerName, kProtocolVersion);

        if (!queueReliableControl(impl_->peer, helloPacket))
        {
            LOG_NET_CONN_ERROR("Failed to send Hello packet");
            failConnection(EConnectState::FailedHandshake);
            return true;
        }
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

            if (state_ == EConnectState::Handshaking && isHandshakeControlMessage(header.type))
            {
                LOG_NET_PROTO_ERROR("Handshake control message {} received on wrong channel - failing handshake",
                                    msgTypeName(header.type));
                failConnection(EConnectState::FailedHandshake);
                return true;
            }
            return false;
        }

        if (!impl_->dispatcher.dispatch(*this, header, payload, payloadSize))
        {
            LOG_NET_PACKET_TRACE("No handler for message type 0x{:02x}", static_cast<int>(header.type));

            if (state_ == EConnectState::Handshaking && channelID == static_cast<uint8_t>(EChannel::ControlReliable))
            {
                LOG_NET_PROTO_ERROR("Unhandled control message {} received during handshake - failing handshake",
                                    msgTypeName(header.type));
                failConnection(EConnectState::FailedHandshake);
                return true;
            }
            return false;
        }

        if (isFailedState(state_))
        {
            failConnection(state_, !lastRejectReason_.has_value());
            return true;
        }

        return false;
    }

    void NetClient::handleDisconnectEvent()
    {
        if (state_ == EConnectState::Disconnecting)
        {
            LOG_NET_CONN_INFO("Graceful disconnect completed");
            destroyTransport();
            resetState();
            return;
        }
        if (state_ == EConnectState::Connecting)
        {
            LOG_NET_CONN_WARN("Remote close/timeout during Connecting");
            failConnection(EConnectState::FailedConnect);
            return;
        }
        if (state_ == EConnectState::Handshaking)
        {
            LOG_NET_CONN_WARN("Remote close/timeout during Handshaking");
            failConnection(EConnectState::FailedHandshake);
            return;
        }
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

            if (state_ == EConnectState::Handshaking)
            {
                failConnection(EConnectState::FailedHandshake);
            }
            else if (state_ == EConnectState::Connecting)
            {
                failConnection(EConnectState::FailedConnect);
            }
            else
            {
                destroyTransport();
                resetState();
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
        }
    }

    // =================================================================================================================
    // ===== Runtime API ===============================================================================================
    // =================================================================================================================

    std::optional<uint32_t> NetClient::sendInput(uint8_t buttons) const
    {
        // TODO: Decide how to handle lobby input later.
        if (impl_ == nullptr || !isConnected())
        {
            return std::nullopt;
        }

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

        if (const auto inputPacket = makeInputPacket(msg);
            !queueUnreliableInput(impl_->peer, inputPacket))
        {
            // Return the assigned sequence anyway so later redundant batches can still cover it.
            LOG_NET_INPUT_WARN("Failed to queue Input seq={} batch=[{}..{}] buttons=0x{:02x}",
                               seq,
                               baseSeq,
                               seq,
                               buttons);
            return seq;
        }

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
        if (impl_ == nullptr || !isConnected() || !impl_->cachedSnapshot.valid)
        {
            return false;
        }

        out = impl_->cachedSnapshot.snapshot;
        return true;
    }

    uint32_t NetClient::lastSnapshotTick() const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->cachedSnapshot.valid)
        {
            return 0;
        }

        return impl_->cachedSnapshot.snapshot.serverTick;
    }

    bool NetClient::tryGetLatestCorrection(MsgCorrection& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->cachedCorrection.valid)
        {
            return false;
        }

        out = impl_->cachedCorrection.correction;
        return true;
    }

    uint32_t NetClient::lastCorrectionTick() const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->cachedCorrection.valid)
        {
            return 0;
        }

        return impl_->cachedCorrection.correction.serverTick;
    }

    uint32_t NetClient::gameplaySilenceMs() const
    {
        if (impl_ == nullptr || !isConnected())
        {
            return 0;
        }

        const TimePoint since =
            impl_->lastGameplayReceiveTime != TimePoint{} ?
            impl_->lastGameplayReceiveTime :
            impl_->connectedStartTime;

        if (since == TimePoint{})
        {
            return 0;
        }

        return static_cast<uint32_t>(elapsedMs(since));
    }

    bool NetClient::tryGetMapSeed(uint32_t& outSeed) const
    {
        if (impl_ == nullptr || !impl_->cachedLevelInfo.valid)
        {
            return false;
        }
        outSeed = impl_->cachedLevelInfo.levelInfo.mapSeed;
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
        LOG_NET_CONN_INFO("Welcome: playerId={}, tickRate={} - handshake complete, session ready", welcome.playerId, welcome.serverTickRate);
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
        switch (reject.reason)
        {
            using enum MsgReject::EReason;
            using enum EConnectState;

            case VersionMismatch:
                LOG_NET_CONN_ERROR("Server rejected: version mismatch (server expects v{})", reject.expectedProtocolVersion);
                state_ = FailedProtocol;
                break;
            case ServerFull:
                LOG_NET_CONN_WARN("Server rejected: server is full");
                state_ = FailedHandshake;
                break;
            case Banned:
                LOG_NET_CONN_WARN("Server rejected: banned");
                state_ = FailedHandshake;
                break;
            case Other:
            default:
                LOG_NET_CONN_WARN("Server rejected: reason={}", static_cast<int>(reject.reason));
                state_ = FailedHandshake;
                break;
        }
    }

    void NetClient::handleLevelInfo(const uint8_t* payload, std::size_t payloadSize)
    {
        if (impl_ == nullptr)
        {
            return;
        }
        if (state_ != EConnectState::Handshaking)
        {
            LOG_NET_PROTO_WARN("LevelInfo received in invalid state {} - ignoring", connectStateName(state_));
            return;
        }
        if (playerId_ == kInvalidPlayerId || serverTickRate_ == 0)
        {
            LOG_NET_PROTO_ERROR("LevelInfo received before valid Welcome - failing handshake");
            state_ = EConnectState::FailedHandshake;
            return;
        }

        MsgLevelInfo levelInfo{};
        if (!deserializeMsgLevelInfo(payload, payloadSize, levelInfo))
        {
            LOG_NET_PROTO_WARN("Failed to parse LevelInfo payload");
            state_ = EConnectState::FailedHandshake;
            return;
        }

        impl_->cachedLevelInfo = {levelInfo, true};

        state_ = EConnectState::Connected;
        impl_->connectedStartTime = SteadyClock::now();

        LOG_NET_CONN_INFO("Received LevelInfo seed={}", levelInfo.mapSeed);
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

        if (impl_->cachedSnapshot.valid && snapshot.serverTick <= impl_->cachedSnapshot.snapshot.serverTick)
        {
            return;
        }

        impl_->cachedSnapshot = {snapshot, true};
        impl_->lastSnapshotReceiveTime = SteadyClock::now();
        impl_->lastGameplayReceiveTime = impl_->lastSnapshotReceiveTime;

        if (snapshot.serverTick % kSnapshotLogEveryN == 0)
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Snapshot tick={} playerCount={}",
                                   snapshot.serverTick,
                                   snapshot.playerCount);
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

        // Only cache the correction if it's newer than the currently cached one.
        if (impl_->cachedCorrection.valid && correction.serverTick <= impl_->cachedCorrection.correction.serverTick)
        {
            return;
        }

        impl_->cachedCorrection = {correction, true};
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

    // TODO: Clear seed/level info later also when the level/round has ended.
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
            impl_->nextInputSeq = 0;
            std::memset(impl_->inputHistory, 0, sizeof(impl_->inputHistory));
            impl_->pendingPlayerName.clear();
            impl_->connectStartTime = TimePoint{};
            impl_->handshakeStartTime = TimePoint{};
            impl_->disconnectStartTime = TimePoint{};
            impl_->connectedStartTime = TimePoint{};
            impl_->lastGameplayReceiveTime = TimePoint{};
            impl_->lastSnapshotReceiveTime = TimePoint{};
            impl_->lastCorrectionReceiveTime = TimePoint{};
            impl_->cachedSnapshot = {};
            impl_->cachedCorrection = {};
            impl_->cachedLevelInfo = {};
        }
    }

    void NetClient::failConnection(const EConnectState failureState, const bool clearRejectReason)
    {
        destroyTransport();
        clearSessionState(clearRejectReason);
        state_ = failureState;
    }

} // namespace bomberman::net
