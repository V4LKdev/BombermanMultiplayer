
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

#include <enet/enet.h>
#include "Net/NetCommon.h"
#include "Net/PacketDispatch.h"

namespace
{
    constexpr enet_uint16 kServerPort = 12345;
    constexpr std::size_t kMaxPeers = 2;
    constexpr int kServiceTimeoutMs = 16;
    constexpr uint16_t kServerTickRate = 60;
} // namespace


using namespace bomberman::net;

/** @brief Input state structure for each connected client */
struct ClientInputState
{
    int8_t moveX;
    int8_t moveY;
    uint8_t actionFlags;
};

/** @brief Context passed through the dispatcher's handler calls. */
struct ServerContext
{
    ENetHost* host = nullptr;
    ENetPeer* peer = nullptr;   ///< The peer that sent the current packet

    std::unordered_map<uint32_t /* ClientId */, ClientInputState> inputs; ///< Stores the latest input state for each connected peer
};

// =====================================================================================================================
// ==== Message Handler Implementations ================================================================================
// =====================================================================================================================

/**
 *  @brief Handler for Hello messages received from clients during the handshake process.
 *
 *  This handler performs the following steps:
 *  1. Deserializes the MsgHello payload and validates it.
 *  2. Checks the protocol version for compatibility.
 *  3. Logs the client's name and protocol version.
 *  4. Constructs a MsgWelcome response with the assigned client ID and server tick rate.
 *  5. Serializes the MsgWelcome into a packet and sends it back to the client reliably.
 *
 *  @param ctx The server context containing the ENet host and peer information.
 *  @param header The deserialized packet header (not used in this handler).
 *  @param payload The raw payload bytes of the Hello message.
 *  @param payloadSize The size of the payload in bytes.
 */
void onHello(ServerContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t payloadSize)
{
    /* ---- Receive Hello from client ----*/

    MsgHello msgHello{};
    if (!deserializeMsgHello(payload, payloadSize, msgHello))
    {
        std::cerr << "[server] Failed to parse Hello payload\n";
        return;
    }

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


    /* ---- Send Welcome response to client ---- */

    MsgWelcome welcomePayload{};
    welcomePayload.protocolVersion = kProtocolVersion;
    welcomePayload.clientId = ctx.peer->incomingPeerID;
    welcomePayload.serverTickRate = kServerTickRate;

    const auto outBytes = makeWelcomePacket(welcomePayload, 0, 0);

    ENetPacket* out = enet_packet_create(outBytes.data(), outBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    if (out == nullptr)
    {
        std::cerr << "[server] Failed to allocate Welcome packet\n";
        return;
    }

    if (enet_peer_send(ctx.peer, static_cast<uint8_t>(EChannel::Control), out) != 0)
    {
        std::cerr << "[server] Failed to queue Welcome packet\n";
        enet_packet_destroy(out);
        return;
    }

    enet_host_flush(ctx.host);
    std::cout << "[server] Sent Welcome to clientId=" << welcomePayload.clientId << '\n';
}

void onInput(ServerContext& ctx, const PacketHeader& header, const uint8_t* payload, std::size_t size)
{
    MsgInput msgInput{};
    if (!deserializeMsgInput(payload, size, msgInput))
    {
        std::cerr << "[server] Failed to parse input payload\n";
        return;
    }

    const uint32_t clientId = ctx.peer->incomingPeerID;
    ctx.inputs[clientId] = {
        msgInput.moveX,
        msgInput.moveY,
        msgInput.actionFlags
    };

    std::cout << "[server] Received incoming input packet from clientId=" << clientId
              << ", Sequence: " << header.sequence
              << ", Tick: " << header.tick
              << "  : moveX=" << static_cast<int>(msgInput.moveX)
              << "  , moveY=" << static_cast<int>(msgInput.moveY)
              << "  , actionFlags=" << static_cast<int>(msgInput.actionFlags) << '\n';

}

// =====================================================================================================================
// ==== Packet Dispatcher Setup ========================================================================================
// =====================================================================================================================

/**
 * @brief Creates and configures the PacketDispatcher for the server,
 * binding message types to their respective handlers.
 */
PacketDispatcher<ServerContext> makeServerDispatcher()
{
    PacketDispatcher<ServerContext> d{};

    d.bind(EMsgType::Hello, &onHello);
    d.bind(EMsgType::Input, &onInput);

    return d;
}

/**
 *  @brief Global PacketDispatcher instance for the server, initialized with the configured handlers.
 */
static const PacketDispatcher<ServerContext> gDispatcher = makeServerDispatcher();

/**
 *  @brief Handles an ENet receive event by dispatching the received packet through the server's PacketDispatcher.
 */
void handleEventReceive(const ENetEvent& event, ServerContext& ctx)
{
    std::cout << "[server] Received " << event.packet->dataLength
              << " bytes on channel " << static_cast<int>(event.channelID) << '\n';

    ctx.peer = event.peer;

    dispatchPacket("[server]", gDispatcher, ctx, event.packet->data, event.packet->dataLength);
}


int main(int /*argc*/, char** /*argv*/)
{
    // 1. Initialize ENet
    if(enet_initialize() != 0)
    {
        std::cerr << "[server] ENet initialization failed\n";
        return EXIT_FAILURE;
    }

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = kServerPort;

    // 2. Create ENet host for the server
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

    // 3. Main event loop
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
                handleEventReceive(event, ctx);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "[server] Peer disconnected\n";
                ctx.inputs.erase(event.peer->incomingPeerID);
                event.peer->data = nullptr;
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
        }
    }

    // 4. Clean up and shutdown
    enet_host_destroy(server);
    enet_deinitialize();
    std::cout << "[server] Shutdown complete\n";
    return EXIT_SUCCESS;
}

