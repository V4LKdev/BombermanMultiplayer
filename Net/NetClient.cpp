#include "NetClient.h"
#include "NetSend.h"
#include "PacketDispatch.h"

#include "Util/Log.h"

#include <enet/enet.h>

namespace bomberman::net
{
    namespace
    {
        constexpr int kConnectTimeoutMs = 2000;
    }

    /** @brief Opaque ENet implementation visible only in this translation unit. */
    struct NetClient::Impl
    {
        ENetHost* host = nullptr;
        ENetPeer* peer = nullptr;
        PacketDispatcher<NetClient> dispatcher;  ///< Dispatcher for incoming packets.
        EConnectState handshakeResult = EConnectState::Handshaking; ///< Set by Welcome handler.
        uint32_t nextInputSequence = 0;

        /** @brief Dispatcher trampoline for Welcome messages. */
        static void onWelcome(NetClient& client,
                              const PacketHeader& /*header*/,
                              const uint8_t* payload,
                              std::size_t payloadSize)
        {
            client.handleWelcome(payload, payloadSize);
        }
    };

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
            impl_->handshakeResult = EConnectState::FailedProtocol;
            return;
        }

        clientId_ = welcome.clientId;
        serverTickRate_ = welcome.serverTickRate;
        impl_->handshakeResult = EConnectState::Connected;

        LOG_CLIENT_INFO("Welcome: clientId={}, tickRate={}", welcome.clientId, welcome.serverTickRate);
    }

    /** @brief Constructs client and binds packet handlers. */
    NetClient::NetClient() : impl_(std::make_unique<Impl>())
    {
        impl_->dispatcher.bind(EMsgType::Welcome, &Impl::onWelcome);
        // TODO: Bind Snapshot and Event handlers when those message types are added.
    }

    NetClient::~NetClient()
    {
        disconnect();
        shutdownENet();
    }

    NetClient::NetClient(NetClient&&) noexcept = default;
    NetClient& NetClient::operator=(NetClient&&) noexcept = default;

    /** @brief Connects and performs handshake. */
    EConnectState NetClient::connect(const std::string& host, uint16_t port, std::string_view playerName)
    {
        if (!impl_)
        {
            state_ = EConnectState::FailedInit;
            return state_;
        }

        // Already connected: treat as success.
        if (isConnected()) return EConnectState::Connected;

        if (!initializeENet())
        {
            state_ = EConnectState::FailedInit;
            return state_;
        }

        // Create client ENet host: 1 peer, kChannelCount channels, unlimited bandwidth.
        impl_->host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
        if (impl_->host == nullptr)
        {
            LOG_CLIENT_ERROR("Failed to create ENet client host");
            state_ = EConnectState::FailedInit;
            releaseResources();
            return state_;
        }

        // Resolve server host.
        ENetAddress address{};
        if (enet_address_set_host(&address, host.c_str()) != 0)
        {
            LOG_CLIENT_ERROR("Invalid host address: {}", host);
            state_ = EConnectState::FailedResolve;
            releaseResources();
            return state_;
        }

        address.port = port;
        // Request kChannelCount channels, no custom connect data.
        impl_->peer = enet_host_connect(impl_->host, &address, kChannelCount, 0);
        if (impl_->peer == nullptr)
        {
            LOG_CLIENT_ERROR("Failed to create ENet peer");
            state_ = EConnectState::FailedConnect;
            releaseResources();
            return state_;
        }

        state_ = EConnectState::Connecting;

        // Wait for connect event.
        ENetEvent event{};
        if (enet_host_service(impl_->host, &event, kConnectTimeoutMs) <= 0 || event.type != ENET_EVENT_TYPE_CONNECT)
        {
            LOG_CLIENT_ERROR("Connect timeout ({}ms)", kConnectTimeoutMs);
            state_ = EConnectState::FailedConnect;
            releaseResources();
            return state_;
        }

        state_ = EConnectState::Handshaking;

        // Perform handshake.
        const EConnectState handshakeState = performHandshake(playerName);
        if (handshakeState != EConnectState::Connected)
        {
            LOG_CLIENT_ERROR("Handshake failed: {}", connectStateName(handshakeState));
            state_ = handshakeState;
            releaseResources();
            return state_;
        }

        state_ = EConnectState::Connected;
        LOG_CLIENT_INFO("Successfully connected and completed handshake");
        return EConnectState::Connected;
    }

    /** @brief Disconnects and releases client host/peer resources. */
    void NetClient::disconnect()
    {
        if (impl_ && impl_->peer)
        {
            enet_peer_disconnect(impl_->peer, 0);

            // Wait for disconnect acknowledgement.
            ENetEvent event{};
            bool disconnectAckReceived = false;
            while (!disconnectAckReceived && enet_host_service(impl_->host, &event, 100) > 0)
            {
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
            impl_->handshakeResult = EConnectState::Handshaking;
            impl_->nextInputSequence = 0;
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

    /** @brief Pumps ENet events and dispatches received packets. */
    void NetClient::pump(uint16_t timeoutMs)
    {
        if (!impl_ || !impl_->host)
            return;

        ENetEvent event{};
        bool shouldDisconnect = false;

        while (enet_host_service(impl_->host, &event, timeoutMs) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_RECEIVE:
            {
                LOG_CLIENT_TRACE("Received {} bytes on channel {}",
                                event.packet->dataLength, channelName(event.channelID));

                dispatchPacket(impl_->dispatcher, *this, event.packet->data, event.packet->dataLength);

                enet_packet_destroy(event.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
                LOG_CLIENT_INFO("Disconnected from server");
                shouldDisconnect = true;
                break;

            case ENET_EVENT_TYPE_CONNECT:
            case ENET_EVENT_TYPE_NONE:
                break;
            }

            // Drain remaining events without blocking after first event.
            timeoutMs = 0;

            if (shouldDisconnect) break;
        }

        if (shouldDisconnect) handleRemoteDisconnect();
    }

    void NetClient::sendInput(const MsgInput& input, uint32_t clientTick)
    {
        if (!impl_ || !isConnected())
            return;

        PacketHeader header{};
        header.type = EMsgType::Input;
        header.payloadSize = static_cast<uint16_t>(kMsgInputSize);
        header.sequence = ++impl_->nextInputSequence;
        header.tick = clientTick;
        header.flags = 0;

        std::array<uint8_t, kPacketHeaderSize + kMsgInputSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgInput(input, bytes.data() + kPacketHeaderSize);

        if (!sendUnreliable(impl_->host, impl_->peer, bytes))
            return;

        if ((header.sequence % kInputLogEveryN) == 0)
        {
            LOG_CLIENT_DEBUG("Sent Input seq={} tick={} move=({},{}) bombCmdId={}",
                             header.sequence, header.tick,
                             static_cast<int>(input.moveX), static_cast<int>(input.moveY),
                             input.bombCommandId);
        }
    }

    /** @brief Sends Hello and waits for Welcome to complete handshake. */
    EConnectState NetClient::performHandshake(std::string_view playerName)
    {
        if (!impl_) return EConnectState::FailedInit;

        // Reset handshake state.
        impl_->handshakeResult = EConnectState::Handshaking;

        // Send Hello packet.
        const auto helloPacket =
            makeHelloPacket(playerName, kProtocolVersion, 1, 0);

        if (!sendReliable(impl_->host, impl_->peer, helloPacket))
        {
            LOG_CLIENT_ERROR("Failed to send Hello packet");
            return EConnectState::FailedHandshake;
        }

        // Poll for Welcome response until handshake completes or timeout.
        int totalWaitMs = 0;
        constexpr int kHandshakeTimeoutMs = 5000;
        constexpr int kPollIntervalMs = 16;

        while (totalWaitMs < kHandshakeTimeoutMs && impl_->handshakeResult == EConnectState::Handshaking)
        {
            if (!impl_ || !impl_->host)
                break;
            pump(kPollIntervalMs);
            totalWaitMs += kPollIntervalMs;
        }

        if (impl_->handshakeResult != EConnectState::Connected)
        {
            if (impl_->handshakeResult == EConnectState::Handshaking)
            {
                LOG_CLIENT_ERROR("Handshake timeout ({}ms)", kHandshakeTimeoutMs);
                return EConnectState::FailedHandshake;
            }
            return impl_->handshakeResult; // e.g. FailedProtocol
        }

        return EConnectState::Connected;
    }

} // namespace bomberman::net
