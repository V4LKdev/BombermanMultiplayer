#include <string>
#include <array>
#include <cstring>
#include <iostream>

#include "SDL.h"
#include <enet/enet.h>

#include "Game.h"
#include "Net/NetCommon.h"

namespace
{
    constexpr enet_uint16 kServerPort = 12345;
    constexpr int kConnectTimeoutMs = 2000;
    constexpr int kResponseTimeoutMs = 1000;

    bool tryHandshakeWithServer()
    {
        using namespace bomberman::net;

        if(enet_initialize() != 0)
        {
            std::cerr << "[client] ENet init failed; running offline\n";
            return false;
        }

        ENetHost* client = enet_host_create(nullptr, 1, 2, 0, 0);
        if(client == nullptr)
        {
            std::cerr << "[client] Failed to create ENet client host; running offline\n";
            enet_deinitialize();
            return false;
        }

        ENetAddress address{};
        enet_address_set_host(&address, "127.0.0.1");
        address.port = kServerPort;

        ENetPeer* peer = enet_host_connect(client, &address, 2, 0);
        if(peer == nullptr)
        {
            std::cerr << "[client] Failed to create ENet peer; running offline\n";
            enet_host_destroy(client);
            enet_deinitialize();
            return false;
        }

        ENetEvent event{};
        if(enet_host_service(client, &event, kConnectTimeoutMs) <= 0 || event.type != ENET_EVENT_TYPE_CONNECT)
        {
            std::cerr << "[client] Connect timeout; running offline\n";
            enet_peer_reset(peer);
            enet_host_destroy(client);
            enet_deinitialize();
            return false;
        }
        std::cout << "[client] Connected to server\n";

        MsgHello hello{};
        hello.protocolVersion = kProtocolVersion;
        std::memset(hello.name, 0, kPlayerNameMax);
        std::memcpy(hello.name, "Player", 6);

        const auto helloPacket = makeHelloPacket(hello, 1, 0);
        ENetPacket* packet =
            enet_packet_create(helloPacket.data(), helloPacket.size(), ENET_PACKET_FLAG_RELIABLE);
        if(packet == nullptr || enet_peer_send(peer, 0, packet) != 0)
        {
            std::cerr << "[client] Failed to send Hello; running offline\n";
            if(packet != nullptr)
            {
                enet_packet_destroy(packet);
            }
            enet_peer_reset(peer);
            enet_host_destroy(client);
            enet_deinitialize();
            return false;
        }
        enet_host_flush(client);
        std::cout << "[client] Sent Hello\n";

        bool handshakeOk = false;
        if(enet_host_service(client, &event, kResponseTimeoutMs) > 0 && event.type == ENET_EVENT_TYPE_RECEIVE)
        {
            PacketHeader header{};
            if(deserializeHeader(event.packet->data, event.packet->dataLength, header) &&
               header.type == EMsgType::Welcome &&
               header.payloadSize == kMsgWelcomeSize &&
               event.packet->dataLength >= kPacketHeaderSize + kMsgWelcomeSize)
            {
                MsgWelcome welcome{};
                if(deserializeMsgWelcome(event.packet->data + kPacketHeaderSize, kMsgWelcomeSize, welcome))
                {
                    std::cout << "[client] Welcome: clientId=" << welcome.clientId
                              << ", tickRate=" << welcome.serverTickRate << '\n';
                    handshakeOk = true;
                }
            }
            enet_packet_destroy(event.packet);
        }

        enet_peer_disconnect(peer, 0);
        enet_host_service(client, &event, 100);
        enet_host_destroy(client);
        enet_deinitialize();

        if(!handshakeOk)
        {
            std::cerr << "[client] Handshake failed; running offline\n";
        }
        return handshakeOk;
    }
} // namespace

int main(int /*argc*/, char** /*argv[]*/)
{
    tryHandshakeWithServer();
    // init game
    bomberman::Game game(std::string("bomberman"), 800, 600);
    // run game loop
    game.run();

    return 0;
}
