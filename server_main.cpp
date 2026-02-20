
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <string>

#include <enet/enet.h>
#include "Net/NetCommon.h"

namespace
{
    constexpr enet_uint16 kServerPort = 12345;
    constexpr std::size_t kMaxPeers = 2;
    constexpr std::size_t kChannelCount = 2;
    constexpr int kServiceTimeoutMs = 16;
} // namespace

void HandleEventReceive(ENetHost* server, const ENetEvent& event);

int main(int /*argc*/, char** /*argv*/)
{
    if(enet_initialize() != 0)
    {
        std::cerr << "[server] ENet initialization failed\n";
        return EXIT_FAILURE;
    }

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = kServerPort;

    ENetHost* server = enet_host_create(&address, kMaxPeers, kChannelCount, 0, 0);
    if(server == nullptr)
    {
        std::cerr << "[server] Failed to create ENet host on port " << kServerPort << '\n';
        enet_deinitialize();
        return EXIT_FAILURE;
    }

    std::cout << "[server] Listening on port " << kServerPort << '\n';

    bool running = true;

    while(running)
    {
        ENetEvent event{};
        const int serviceResult = enet_host_service(server, &event, kServiceTimeoutMs);
        if(serviceResult < 0)
        {
            std::cerr << "[server] enet_host_service failed, shutting down\n";
            break;
        }
        if(serviceResult == 0)
        {
            continue;
        }
        // if serviceResult > 0

        switch(event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
                std::cout << "[server] Peer connected\n";
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                HandleEventReceive(server, event);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "[server] Peer disconnected\n";
                event.peer->data = nullptr;
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
        }
    }

    enet_host_destroy(server);
    enet_deinitialize();
    std::cout << "[server] Shutdown complete\n";
    return EXIT_SUCCESS;
}

void HandleEventReceive(ENetHost* server, const ENetEvent& event)
{
    std::cout << "[server] Received " << event.packet->dataLength
                << " bytes on channel " << static_cast<int>(event.channelID) << '\n';

    using namespace bomberman::net;
    PacketHeader header{};
    if(!deserializeHeader(event.packet->data, event.packet->dataLength, header))
    {
        std::cerr << "[server] Packet too small for PacketHeader\n";
        return;
    }

    if(header.type != EMsgType::Hello)
    {
        std::cerr << "[server] Unexpected message type " << static_cast<int>(header.type) << '\n';
        return;
    }

    const std::size_t payloadOffset = kPacketHeaderSize;
    const std::size_t availablePayload = event.packet->dataLength - payloadOffset;
    if(header.payloadSize != kMsgHelloSize || availablePayload < kMsgHelloSize)
    {
        std::cerr << "[server] Invalid Hello payload size\n";
        return;
    }

    MsgHello msgHello{};
    if(!deserializeMsgHello(event.packet->data + payloadOffset, availablePayload, msgHello))
    {
        std::cerr << "[server] Failed to parse Hello payload\n";
        return;
    }

    const std::size_t nameLen = strnlen(msgHello.name, kPlayerNameMax);
    const std::string playerName(msgHello.name, nameLen);

    std::cout << "[server] Hello: version=" << msgHello.protocolVersion
              << ", name=\"" << playerName << "\"\n";

    if(msgHello.protocolVersion != kProtocolVersion)
    {
        std::cerr << "[server] Protocol mismatch (client " << msgHello.protocolVersion
                  << ", server " << kProtocolVersion << ")\n";
        return;
    }

    MsgWelcome welcomePayload{};
    welcomePayload.protocolVersion = kProtocolVersion;
    welcomePayload.clientId = event.peer->incomingPeerID;
    welcomePayload.serverTickRate = kServerTickRate;

    std::array<uint8_t, kPacketHeaderSize + kMsgWelcomeSize> outBytes{};
    PacketHeader outHeader{};
    outHeader.type = EMsgType::Welcome;
    outHeader.payloadSize = static_cast<uint16_t>(kMsgWelcomeSize);
    outHeader.sequence = 0;
    outHeader.tick = 0;
    outHeader.flags = 0;

    serializeHeader(outHeader, outBytes.data());
    serializeMsgWelcome(welcomePayload, outBytes.data() + kPacketHeaderSize);

    ENetPacket* out = enet_packet_create(outBytes.data(), outBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    if(out == nullptr)
    {
        std::cerr << "[server] Failed to allocate Welcome packet\n";
        return;
    }

    if(enet_peer_send(event.peer, 0, out) != 0)
    {
        std::cerr << "[server] Failed to queue Welcome packet\n";
        enet_packet_destroy(out);
        return;
    }

    enet_host_flush(server);
    std::cout << "[server] Sent Welcome to clientId=" << welcomePayload.clientId << '\n';
}
