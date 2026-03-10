#include "NetClient.h"
#include "NetSend.h"
#include "PacketDispatch.h"

#include "Util/Log.h"

#include <chrono>
#include <enet/enet.h>

namespace bomberman::net
{
    // =================================================================================================================
    // ==== Local Helpers ==============================================================================================
    // =================================================================================================================

    namespace
    {
        constexpr int kConnectTimeoutMs = 5000;

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
        uint32_t nextInputSequence = 0;

        // ---- Async connect state (cleared by resetState()) ----
        std::string pendingPlayerName;
        TimePoint connectStartTime{};
        TimePoint handshakeStartTime{};

        struct ReceivedStateCache
        {
            MsgState snapshot{};
            uint32_t tick = 0;
            uint32_t sequence = 0;
            bool valid = false;
        };
        ReceivedStateCache lastState{};

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

        static void onState(NetClient& client,
                            const PacketHeader& header,
                            const uint8_t* payload,
                            std::size_t payloadSize)
        {
            client.handleState(header, payload, payloadSize);
        }
    };

    // =================================================================================================================
    // ==== Construction And Lifetime ==================================================================================
    // =================================================================================================================

    /** @brief Constructs client and binds packet handlers. */
    NetClient::NetClient() : impl_(std::make_unique<Impl>())
    {
        impl_->dispatcher.bind(EMsgType::Welcome,   &Impl::onWelcome);
        impl_->dispatcher.bind(EMsgType::Reject,    &Impl::onReject);
        impl_->dispatcher.bind(EMsgType::LevelInfo, &Impl::onLevelInfo);
        impl_->dispatcher.bind(EMsgType::State,     &Impl::onState);
    }

    NetClient::~NetClient()
    {
        disconnect();
        shutdownENet();
    }

    NetClient::NetClient(NetClient&&) noexcept = default;
    NetClient& NetClient::operator=(NetClient&&) noexcept = default;

    // =================================================================================================================
    // ==== Connection Lifecycle =======================================================================================
    // =================================================================================================================

    void NetClient::beginConnect(const std::string& host, uint16_t port, std::string_view playerName)
    {
        if (!impl_)
        {
            state_ = EConnectState::FailedInit;
            return;
        }

        // Already connected or in-progress: ignore duplicate calls.
        if (isConnected() || state_ == EConnectState::Connecting || state_ == EConnectState::Handshaking)
        {
            LOG_CLIENT_DEBUG("beginConnect() called while already in state {} – ignoring", connectStateName(state_));
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
            releaseResources();
            return;
        }

        // Resolve server host.
        ENetAddress address{};
        if (enet_address_set_host(&address, host.c_str()) != 0)
        {
            LOG_CLIENT_ERROR("Invalid host address: {}", host);
            state_ = EConnectState::FailedResolve;
            releaseResources();
            return;
        }

        address.port = port;

        // Request kChannelCount channels, no custom connect data.
        impl_->peer = enet_host_connect(impl_->host, &address, kChannelCount, 0);
        if (impl_->peer == nullptr)
        {
            LOG_CLIENT_ERROR("Failed to create ENet peer");
            state_ = EConnectState::FailedConnect;
            releaseResources();
            return;
        }

        // Store pending state for async handshake in pump().
        impl_->pendingPlayerName = std::string(playerName);
        impl_->connectStartTime = SteadyClock::now();

        state_ = EConnectState::Connecting;
        LOG_CLIENT_DEBUG("Async connect initiated to {}:{}", host, port);
    }

    /** @brief Aborts an in-progress connect/handshake without sending a disconnect request. */
    void NetClient::cancelConnect()
    {
        if (state_ != EConnectState::Connecting && state_ != EConnectState::Handshaking)
            return;

        LOG_CLIENT_DEBUG("Connect attempt cancelled (was {})", connectStateName(state_));
        releaseResources();
        resetState();
    }

    /** @brief Disconnects and releases client host/peer resources. */
    void NetClient::disconnect()
    {
        if (impl_ && impl_->peer)
        {
            enet_peer_disconnect(impl_->peer, 0);

            // Wait for disconnect acknowledgement (capped to avoid unbounded spin).
            constexpr int kDisconnectDrainMaxIter = 50;
            ENetEvent event{};
            bool disconnectAckReceived = false;
            int iter = 0;
            while (!disconnectAckReceived && iter < kDisconnectDrainMaxIter
                   && enet_host_service(impl_->host, &event, 100) > 0)
            {
                ++iter;
                switch (event.type)
                {
                    case ENET_EVENT_TYPE_RECEIVE:
                        enet_packet_destroy(event.packet);
                        break;
                    case ENET_EVENT_TYPE_DISCONNECT:
                        disconnectAckReceived = true;
                        break;
                    default:
                        break;
                }
            }

            // Force reset if acknowledgement was not received.
            enet_peer_reset(impl_->peer);
            impl_->peer = nullptr;
        }

        if (impl_ && impl_->host)
        {
            enet_host_destroy(impl_->host);
            impl_->host = nullptr;
        }

        resetState();
    }

    /** @brief Pumps ENet events and dispatches received packets. */
    void NetClient::pump(uint16_t timeoutMs)
    {
        if (!impl_ || !impl_->host)
            return;

        // ---- Async timeout checks (run once per pump, before event drain) ----
        if (state_ == EConnectState::Connecting)
        {
            if (elapsedMs(impl_->connectStartTime) >= kConnectTimeoutMs)
            {
                LOG_CLIENT_WARN("Async connect timeout ({}ms)", kConnectTimeoutMs);
                state_ = EConnectState::FailedConnect;
                releaseResources();
                return;
            }
        }
        else if (state_ == EConnectState::Handshaking)
        {
            if (elapsedMs(impl_->handshakeStartTime) >= kConnectTimeoutMs)
            {
                LOG_CLIENT_WARN("Async handshake timeout ({}ms)", kConnectTimeoutMs);
                state_ = EConnectState::FailedHandshake;
                releaseResources();
                return;
            }
        }

        ENetEvent event{};
        bool shouldDisconnect = false;

        while (enet_host_service(impl_->host, &event, timeoutMs) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_RECEIVE:
            {
                LOG_CLIENT_TRACE("Received {} bytes on channel {}", event.packet->dataLength, channelName(event.channelID));

                dispatchPacket(impl_->dispatcher, *this, event.packet->data, event.packet->dataLength);

                enet_packet_destroy(event.packet);

                // A handler may move us into a terminal failure state (for example protocol mismatch).
                if (isFailedState(state_))
                {
                    releaseResources();
                    return;
                }

                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
                LOG_CLIENT_INFO("Disconnected from server");
                shouldDisconnect = true;
                break;

            case ENET_EVENT_TYPE_CONNECT:
            {
                if (state_ != EConnectState::Connecting)
                {
                    LOG_CLIENT_DEBUG("Ignoring unexpected CONNECT event in state {}", connectStateName(state_));
                    break;
                }

                LOG_CLIENT_DEBUG("ENet connect event received, sending Hello");

                // Phase A: send Hello once, transition to Handshaking.
                const auto helloPacket =
                    makeHelloPacket(impl_->pendingPlayerName, kProtocolVersion, 1, 0);

                if (!sendReliable(impl_->host, impl_->peer, helloPacket))
                {
                    LOG_CLIENT_ERROR("Failed to send Hello packet");
                    state_ = EConnectState::FailedHandshake;
                    releaseResources();
                    return;
                }

                state_ = EConnectState::Handshaking;
                impl_->handshakeStartTime = SteadyClock::now();
                // Phase B: handleWelcome() will flip state_ to Connected
                // on subsequent pump() ticks when the Welcome arrives.
                break;
            }

            case ENET_EVENT_TYPE_NONE:
                break;
            }

            // Drain remaining events without blocking after first event.
            timeoutMs = 0;

            if (shouldDisconnect) break;
        }

        if (shouldDisconnect) handleRemoteDisconnect();
    }

    // =================================================================================================================
    // ==== Public Runtime I/O =========================================================================================
    // =================================================================================================================

    void NetClient::sendInput(const MsgInput& input, uint32_t clientTick)
    {
        if (!impl_ || !isConnected())
            return;

        const uint32_t seq = ++impl_->nextInputSequence;
        const auto bytes = makeInputPacket(input, seq, clientTick);

        if (!sendUnreliable(impl_->host, impl_->peer, bytes))
            return;

        if ((seq % kInputLogEveryN) == 0)
        {
            LOG_CLIENT_DEBUG("Sent Input seq={} tick={} move=({},{}) bombCmdId={}",
                             seq, clientTick,
                             static_cast<int>(input.moveX), static_cast<int>(input.moveY),
                             input.bombCommandId);
        }
    }

    bool NetClient::tryGetLatestState(MsgState& out) const
    {
        if (!impl_ || !isConnected() || !impl_->lastState.valid)
            return false;

        out = impl_->lastState.snapshot;
        return true;
    }

    uint32_t NetClient::lastStateTick() const
    {
        if (!impl_ || !isConnected() || !impl_->lastState.valid)
            return 0;

        return impl_->lastState.tick;
    }

    bool NetClient::tryGetMapSeed(uint32_t& outSeed) const
    {
        if (!impl_ || !impl_->lastLevelInfo.valid)
            return false;
        outSeed = impl_->lastLevelInfo.levelInfo.mapSeed;
        return true;
    }

    // =================================================================================================================
    // ==== Private Protocol Handlers ==================================================================================
    // =================================================================================================================

    /** @brief Processes Welcome payload and updates negotiated client session data. */
    void NetClient::handleWelcome(const uint8_t* payload, std::size_t payloadSize)
    {
        MsgWelcome welcome{};
        if (!deserializeMsgWelcome(payload, payloadSize, welcome))
        {
            LOG_CLIENT_WARN("Failed to parse Welcome payload");
            return;
        }

        if (welcome.protocolVersion != kProtocolVersion)
        {
            LOG_CLIENT_ERROR("Protocol mismatch (server={}, client={})", welcome.protocolVersion, kProtocolVersion);
            state_ = EConnectState::FailedProtocol;
            return;
        }

        clientId_ = welcome.clientId;
        serverTickRate_ = welcome.serverTickRate;

        LOG_CLIENT_INFO("Welcome: clientId={}, tickRate={} - awaiting LevelInfo", welcome.clientId, welcome.serverTickRate);
    }

    void NetClient::handleReject(const uint8_t* payload, std::size_t payloadSize)
    {
        // TODO: implement handling of reject properly
    }

    /** @brief Processes LevelInfo payload, caches it, and completes the handshake. */
    void NetClient::handleLevelInfo(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_)
            return;

        MsgLevelInfo msgLevelInfo{};
        if (!deserializeMsgLevelInfo(payload, payloadSize, msgLevelInfo))
        {
            LOG_CLIENT_WARN("Failed to parse LevelInfo payload");
            return;
        }

        impl_->lastLevelInfo = {msgLevelInfo, true};

        // LevelInfo is the final handshake packet — the session is now fully ready.
        state_ = EConnectState::Connected;

        LOG_CLIENT_INFO("Received LevelInfo seed={} — handshake complete, session ready", msgLevelInfo.mapSeed);
    }

    /** @brief Processes State payload and updates local game state accordingly. */
    void NetClient::handleState(const PacketHeader& header, const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_CLIENT_WARN("Received State payload while not connected - ignoring");
            return;
        }

        MsgState state{};
        if (!deserializeMsgState(payload, payloadSize, state))
        {
            LOG_CLIENT_WARN("Failed to parse State payload");
            return;
        }

        impl_->lastState = {state, header.tick, header.sequence, true};

        if (header.tick % kStateLogEveryN == 0)
        {
            LOG_CLIENT_DEBUG("Received State seq={} tick={} playerCount={}",
                             header.sequence, header.tick, state.playerCount);
        }
    }

    // =================================================================================================================
    // ==== Private ENet helpers =======================================================================================
    // =================================================================================================================

    /** @brief Initializes ENet global state once for this client instance. */
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

    /** @brief Shuts down ENet global state if previously initialized. */
    void NetClient::shutdownENet()
    {
        if (initialized_)
        {
            enet_deinitialize();
            initialized_ = false;
        }
    }

    /** @brief Tears down ENet peer/host without modifying logical state. */
    void NetClient::releaseResources()
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

    /** @brief Cleans local state after server-initiated disconnect. */
    void NetClient::handleRemoteDisconnect()
    {
        // Server already disconnected us. Only clean up local resources.
        if (impl_)
        {
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

        resetState();
    }

    /** @brief Resets client session fields to default values. */
    void NetClient::resetState()
    {
        state_ = EConnectState::Disconnected;
        clientId_ = 0;
        serverTickRate_ = 0;
        if (impl_)
        {
            impl_->nextInputSequence = 0;
            impl_->pendingPlayerName.clear();
            impl_->connectStartTime   = TimePoint{};
            impl_->handshakeStartTime = TimePoint{};
            impl_->lastState     = {};
            impl_->lastLevelInfo = {}; // Clear so a reconnect receives a fresh seed from the server.
        }
    }

} // namespace bomberman::net
