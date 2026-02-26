
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
    constexpr uint16_t kServerTickRate = 60;   ///< Server simulation tick rate (Hz), communicated to clients via MsgWelcome
} // namespace

using namespace bomberman::net;

/** @brief Context passed through the dispatcher's void* to every handler. */
struct ServerContext
{
    ENetHost* host = nullptr;
    ENetPeer* peer = nullptr;   ///< The peer that sent the current packet
};

// ---- Message Handlers ----

void onHello(void* ctx,
             const PacketHeader& /*header*/,
             const uint8_t* payload,
             std::size_t payloadSize)
{
    auto& sc = *static_cast<ServerContext*>(ctx);

    MsgHello msgHello{};
    if (!deserializeMsgHello(payload, payloadSize, msgHello))
    {
        std::cerr << "[server] Failed to parse Hello payload\n";
        return;
    }

    // Check protocol version: if it doesn't match, the payload layout beyond the version field may be incompatible.
    if (msgHello.protocolVersion != kProtocolVersion)
    {
        std::cerr << "[server] Protocol mismatch (client " << msgHello.protocolVersion
                  << ", server " << kProtocolVersion << ")\n";
        return;
    }

    const std::size_t nameLen = boundedStrLen(msgHello.name, kPlayerNameMax);
    const std::string playerName(msgHello.name, nameLen);

    std::cout << "[server] Hello: version=" << msgHello.protocolVersion
              << ", name=\"" << playerName << "\"\n";

    MsgWelcome welcomePayload{};
    welcomePayload.protocolVersion = kProtocolVersion;
    welcomePayload.clientId = sc.peer->incomingPeerID;
    welcomePayload.serverTickRate = kServerTickRate;

    const auto outBytes = makeWelcomePacket(welcomePayload, 0, 0);

    ENetPacket* out = enet_packet_create(outBytes.data(), outBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    if (out == nullptr)
    {
        std::cerr << "[server] Failed to allocate Welcome packet\n";
        return;
    }

    if (enet_peer_send(sc.peer, 0, out) != 0)
    {
        std::cerr << "[server] Failed to queue Welcome packet\n";
        enet_packet_destroy(out);
        return;
    }

    enet_host_flush(sc.host);
    std::cout << "[server] Sent Welcome to clientId=" << welcomePayload.clientId << '\n';
}

// ---- Dispatcher Setup ----

PacketDispatcher makeServerDispatcher()
{
    PacketDispatcher d{};
    d.bind(EMsgType::Hello, &onHello);
    // Future: d.bind(EMsgType::Input, &onInput);
    return d;
}

// Single global instance — initialized once at startup.
static const PacketDispatcher gDispatcher = makeServerDispatcher();

// ---- Receive Entry Point ----

void HandleEventReceive(const ENetEvent& event, ServerContext& ctx)
{
    std::cout << "[server] Received " << event.packet->dataLength
              << " bytes on channel " << static_cast<int>(event.channelID) << '\n';

    PacketHeader header{};
    if (!deserializeHeader(event.packet->data, event.packet->dataLength, header))
    {
        std::cerr << "[server] Failed to deserialize PacketHeader (malformed or truncated packet, "
                  << event.packet->dataLength << " bytes)\n";
        return;
    }

    ctx.peer = event.peer;

    if (!gDispatcher.dispatch(&ctx, header, event.packet->data + kPacketHeaderSize, header.payloadSize))
    {
        std::cerr << "[server] No handler for message type " << static_cast<int>(header.type) << '\n';
    }
}

// ---- Main ----

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

    ServerContext ctx{};
    ctx.host = server;

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
                HandleEventReceive(event, ctx);
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

