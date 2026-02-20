
#include "NetClient.h"

#include <iostream>
#include <enet/enet.h>

#include "Net/NetCommon.h"

namespace bomberman::net
{
    namespace
    {
        constexpr int kConnectTimeoutMs = 2000;
        constexpr int kResponseTimeoutMs = 1000;
    }

    bool NetClient::handshake(const std::string& host, uint16_t port, std::string_view playerName)
    {
        if(enet_initialize() != 0)
        {
            std::cerr << "[client] ENet init failed; running offline\n";
            return false;
        }

        // Lambda to clean up ENet resources on any failure path
        auto cleanup = [&](ENetHost* h)
        {
            if(h) enet_host_destroy(h);
            enet_deinitialize();
        };

        // Create an ENet client host with no specific address (nullptr), allowing 1 peer, 2 channels, and default bandwidth limits
        ENetHost* client = enet_host_create(nullptr, 1, 2, 0, 0);
        if(client == nullptr)
        {
            std::cerr << "[client] Failed to create ENet client host; running offline\n";
            cleanup(nullptr);
            return false;
        }

        // Set up the server address structure with the provided host and port
        ENetAddress address{};
        if (enet_address_set_host(&address, host.c_str()) != 0)
        {
            std::cerr << "[client] Invalid host address; running offline\n";
            cleanup(client);
            return false;
        }

        // Try to connect to the server at the specified address and port, requesting 2 channels and no special data
        address.port = port;
        ENetPeer* peer = enet_host_connect(client, &address, 2, 0);
        if(peer == nullptr)
        {
            std::cerr << "[client] Failed to create ENet peer; running offline\n";
            cleanup(client);
            return false;
        }

        // Wait for the connection event from the server within the specified timeout
        ENetEvent event{};
        if(enet_host_service(client, &event, kConnectTimeoutMs) <= 0 || event.type != ENET_EVENT_TYPE_CONNECT)
        {
            std::cerr << "[client] Connect timeout; running offline\n";
            enet_peer_reset(peer);
            cleanup(client);
            return false;
        }
        std::cout << "[client] Connected to server\n";


        // Construct the Hello packet with the player's name and protocol version, then send it to the server reliably
        const auto helloPacket = makeHelloPacket(playerName, kProtocolVersion, 1, 0);

        ENetPacket* packet = enet_packet_create(helloPacket.data(), helloPacket.size(), ENET_PACKET_FLAG_RELIABLE);

        if(packet == nullptr || enet_peer_send(peer, 0, packet) != 0)
        {
            std::cerr << "[client] Failed to send Hello; running offline\n";
            if(packet != nullptr)
            {
                enet_packet_destroy(packet);
            }
            enet_peer_reset(peer);
            cleanup(client);
            return false;
        }
        // Flush the host to ensure the packet is sent immediately
        enet_host_flush(client);
        std::cout << "[client] Sent Hello\n";

        // Wait for the Welcome response from the server within the specified timeout
        bool handshakeOk = false;
        if(enet_host_service(client, &event, kResponseTimeoutMs) > 0 && event.type == ENET_EVENT_TYPE_RECEIVE)
        {
            // Attempt to deserialize the packet header and verify that it is a Welcome message with the expected payload size
            PacketHeader header{};
            if(deserializeHeader(event.packet->data, event.packet->dataLength, header) && header.type == EMsgType::Welcome)
            {
                MsgWelcome welcome{};
                if(deserializeMsgWelcome(event.packet->data + kPacketHeaderSize, header.payloadSize, welcome))
                {
                    std::cout << "[client] Welcome: clientId=" << welcome.clientId
                              << ", tickRate=" << welcome.serverTickRate << '\n';

                    handshakeOk = true;
                }
                else
                {
                    std::cerr << "[client] Failed to deserialize Welcome message\n";
                }
            }
            // Destroy the packet after processing to free resources
            enet_packet_destroy(event.packet);
        }

        // Clean up the connection and ENet resources before returning the result of the handshake
        enet_peer_disconnect(peer, 0);
        enet_host_service(client, &event, 100);
        // Destroy any received packet if it wasn't already destroyed during processing
        if (event.type == ENET_EVENT_TYPE_RECEIVE)
            enet_packet_destroy(event.packet);
        cleanup(client);

        if(!handshakeOk)
        {
            std::cerr << "[client] Handshake failed; running offline\n";
        }
        return handshakeOk;
    }

} // namespace bomberman::net