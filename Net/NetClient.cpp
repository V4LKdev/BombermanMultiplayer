#include "NetClient.h"

#include <iostream>
#include <enet/enet.h>

#include "Net/NetCommon.h"
#include "Net/PacketDispatch.h"

namespace bomberman::net
{
    namespace
    {
        constexpr int kConnectTimeoutMs = 2000;
        constexpr int kResponseTimeoutMs = 1000;
    }

    /** @brief Opaque ENet implementation — only visible inside this translation unit. */
    struct NetClient::Impl
    {
        ENetHost* host = nullptr;
        ENetPeer* peer = nullptr;
        PacketDispatcher<NetClient> dispatcher;  // Message dispatcher for incoming packets
        bool handshakeComplete = false;  // Set to true when Welcome is successfully processed

        // ---- Dispatcher handler trampolines ----
        // Impl is a nested type of NetClient, so it has access to private members.

        /** @brief Dispatcher trampoline for Welcome messages. */
        static void onWelcome(NetClient& client,
                              const PacketHeader& /*header*/,
                              const uint8_t* payload,
                              std::size_t payloadSize)
        {
            client.handleWelcome(payload, payloadSize);
        }
    };

    // ---- Message Handler Implementations ----

    void NetClient::handleWelcome(const uint8_t* payload, std::size_t payloadSize)
    {
        MsgWelcome welcome{};
        if (!deserializeMsgWelcome(payload, payloadSize, welcome))
        {
            std::cerr << "[client] Failed to parse Welcome payload\n";
            return;
        }

        if (welcome.protocolVersion != kProtocolVersion)
        {
            std::cerr << "[client] Protocol mismatch (server " << welcome.protocolVersion
                      << ", client " << kProtocolVersion << ")\n";
            return;
        }

        clientId_ = welcome.clientId;
        serverTickRate_ = welcome.serverTickRate;
        impl_->handshakeComplete = true;

        std::cout << "[client] Welcome: clientId=" << welcome.clientId
                  << ", tickRate=" << welcome.serverTickRate << "\n";
    }

    // ---- Special members ----
    NetClient::NetClient() : impl_(std::make_unique<Impl>())
    {
        // Bind message handlers via Impl trampolines
        impl_->dispatcher.bind(EMsgType::Welcome, &Impl::onWelcome);
        // Future: impl_->dispatcher.bind(EMsgType::Snapshot, &Impl::onSnapshot);
    }
    NetClient::~NetClient() { disconnect(); }
    NetClient::NetClient(NetClient&&) noexcept = default;
    NetClient& NetClient::operator=(NetClient&&) noexcept = default;



    bool NetClient::connect(const std::string& host, uint16_t port, std::string_view playerName)
    {
        if (!impl_) return false;

        // Skip if already connected
        if (isConnected()) return true;

        if (!initializeENet())
        {
            return false;
        }

        // 1. Create Host
        impl_->host = enet_host_create(nullptr, 1, kChannelCount, 0, 0); // 1 peer, kChannelCount channels, unlimited bandwidth
        if (impl_->host == nullptr)
        {
            std::cerr << "[client] Failed to create ENet client host\n";
            disconnect();
            return false;
        }

        // 2. Connect to Peer (Server)
        ENetAddress address{};
        if (enet_address_set_host(&address, host.c_str()) != 0)
        {
            std::cerr << "[client] Invalid host address\n";
            disconnect();
            return false;
        }

        address.port = port;
        impl_->peer = enet_host_connect(impl_->host, &address, kChannelCount, 0); // Request kChannelCount channels, no special data
        if (impl_->peer == nullptr)
        {
            std::cerr << "[client] Failed to create ENet peer\n";
            disconnect();
            return false;
        }

        // 3. Wait for Connection Event
        ENetEvent event{};
        if (enet_host_service(impl_->host, &event, kConnectTimeoutMs) <= 0 || event.type != ENET_EVENT_TYPE_CONNECT)
        {
            std::cerr << "[client] Connect timeout\n";
            disconnect();
            return false;
        }

        // 4. Perform Handshake (Send Hello, Wait for Welcome)
        if (!performHandshake(playerName))
        {
            std::cerr << "[client] Handshake failed\n";
            disconnect();
            return false;
        }

        connected_ = true;
        std::cout << "[client] Successfully connected and completed handshake\n";
        return true;
    }

    void NetClient::disconnect()
    {
        if (impl_ && impl_->peer)
        {
            enet_peer_disconnect(impl_->peer, 0);

            // Wait for disconnect acknowledgment
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

            // Force disconnect if no acknowledgment received
            enet_peer_reset(impl_->peer);
            impl_->peer = nullptr;
        }

        if (impl_ && impl_->host)
        {
            enet_host_destroy(impl_->host);
            impl_->host = nullptr;
        }

        connected_ = false;
        clientId_ = 0;
        serverTickRate_ = 0;
        if (impl_) impl_->handshakeComplete = false;
        shutdownENet();
    }

    bool NetClient::initializeENet()
    {
        if (initialized_) return true;

        if (enet_initialize() != 0)
        {
            std::cerr << "[client] ENet initialization failed\n";
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

    void NetClient::handleRemoteDisconnect()
    {
        // Server already disconnected us — just clean up locally, don't send a disconnect request.
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

            impl_->handshakeComplete = false;
        }

        connected_ = false;
        clientId_ = 0;
        serverTickRate_ = 0;
        shutdownENet();
    }

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
                std::cout << "[client] Received " << event.packet->dataLength
                          << " bytes on channel " << static_cast<int>(event.channelID) << '\n';

                dispatchPacket("[client]", impl_->dispatcher, *this,
                               event.packet->data, event.packet->dataLength);

                enet_packet_destroy(event.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "[client] Disconnected from server\n";
                shouldDisconnect = true;
                break;

            case ENET_EVENT_TYPE_CONNECT:
            case ENET_EVENT_TYPE_NONE:
                break;
            }

            // After first event, drain remaining events without blocking
            timeoutMs = 0;

            if (shouldDisconnect) break;
        }

        if (shouldDisconnect) handleRemoteDisconnect();
    }

    bool NetClient::performHandshake(std::string_view playerName)
    {
        if (!impl_) return false;

        // Reset handshake state — ensures reconnect doesn't see stale success from a previous session
        impl_->handshakeComplete = false;

        // Send Hello packet to server
        const auto helloPacket = makeHelloPacket(playerName, kProtocolVersion, 1, 0);
        ENetPacket* packet = enet_packet_create(helloPacket.data(), helloPacket.size(), ENET_PACKET_FLAG_RELIABLE);

        if (packet == nullptr || enet_peer_send(impl_->peer, static_cast<uint8_t>(EChannel::Control), packet) != 0)
        {
            std::cerr << "[client] Failed to send Hello packet\n";
            if (packet != nullptr)
            {
                enet_packet_destroy(packet);
            }
            return false;
        }

        enet_host_flush(impl_->host);

        // Poll for Welcome response until handshake completes or timeout
        int totalWaitMs = 0;
        constexpr int kHandshakeTimeoutMs = 5000;  // 5 second timeout for full handshake
        constexpr int kPollIntervalMs = 16;        // Poll every 16ms

        while (totalWaitMs < kHandshakeTimeoutMs && !impl_->handshakeComplete)
        {
            // Early exit if remote dropped us and host is gone
            if (!impl_ || !impl_->host)
                break;
            // Use pump() to process incoming packets through dispatcher
            pump(kPollIntervalMs);
            totalWaitMs += kPollIntervalMs;
        }

        if (!impl_->handshakeComplete)
        {
            std::cerr << "[client] Handshake timeout\n";
            return false;
        }

        return true;
    }

} // namespace bomberman::net
