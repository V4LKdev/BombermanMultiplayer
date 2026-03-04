#include <charconv>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

#include <enet/enet.h>
#include "Net/NetCommon.h"
#include "Net/NetSend.h"
#include "Net/PacketDispatch.h"
#include "Util/Log.h"

using namespace bomberman::net;

// TODO: Read these from a config file or CLI arguments
namespace
{
    constexpr enet_uint16 kServerPort = 12345;
    constexpr std::size_t kMaxPeers = 2;
    constexpr int kServiceTimeoutMs = 16;
    constexpr uint16_t kServerTickRate = 60;

    /// Global flag for graceful shutdown
    volatile std::sig_atomic_t gRunning = 1;
    void onSignal(int /*sig*/) { gRunning = 0; }

    struct CliOptions
    {
        spdlog::level::level_enum logLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        std::string logFile;
        uint16_t port = kServerPort;
    };

    void printUsage(const char* exeName)
    {
        std::cout
            << "Usage: " << exeName
            << " [--log-level <trace|debug|info|warn|error|critical>] [--log-file <path>] [--port <port override>]\n";
    }

    bool parseLogLevel(std::string_view text, spdlog::level::level_enum& outLevel)
    {
        if (text == "trace")    { outLevel = spdlog::level::trace; return true; }
        if (text == "debug")    { outLevel = spdlog::level::debug; return true; }
        if (text == "info")     { outLevel = spdlog::level::info; return true; }
        if (text == "warn")     { outLevel = spdlog::level::warn; return true; }
        if (text == "error")    { outLevel = spdlog::level::err; return true; }
        if (text == "critical") { outLevel = spdlog::level::critical; return true; }
        return false;
    }

    bool parsePort(std::string_view text, uint16_t& outPort)
    {
        unsigned int value = 0;
        const char* begin = text.data();
        const char* end = begin + text.size();
        const auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec != std::errc{} || ptr != end || value == 0 ||
            value > std::numeric_limits<uint16_t>::max())
        {
            return false;
        }

        outPort = static_cast<uint16_t>(value);
        return true;
    }

    bool parseCli(int argc, char** argv, CliOptions& outOptions)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];

            if (arg == "--help")
            {
                printUsage(argv[0]);
                return false;
            }

            if (arg == "--log-level")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Missing value for --log-level\n";
                    printUsage(argv[0]);
                    return false;
                }

                const std::string_view value = argv[++i];
                if (!parseLogLevel(value, outOptions.logLevel))
                {
                    std::cerr << "Invalid log level: " << value << '\n';
                    printUsage(argv[0]);
                    return false;
                }
                continue;
            }

            if (arg == "--log-file")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Missing value for --log-file\n";
                    printUsage(argv[0]);
                    return false;
                }

                outOptions.logFile = argv[++i];
                continue;
            }

            if (arg == "--port")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Missing value for --port\n";
                    printUsage(argv[0]);
                    return false;
                }

                const std::string_view value = argv[++i];
                if (!parsePort(value, outOptions.port))
                {
                    std::cerr << "Invalid port: " << value << '\n';
                    printUsage(argv[0]);
                    return false;
                }
                continue;
            }

            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage(argv[0]);
            return false;
        }

        return true;
    }
} // namespace


// =====================================================================================================================
// ==== Server-Side Types ==============================================================================================
// =====================================================================================================================

// TODO: Evolve into a better ClientState with input history, ack tracking, etc.

/** @brief Long-lived server state shared across all dispatch calls. */
struct ServerState
{
    ENetHost* host = nullptr;
    std::unordered_map<uint32_t, MsgInput> inputs;              ///< Latest input state per connected client
    std::unordered_map<uint32_t, uint16_t> lastBombCommandId;   ///< Last seen bombCommandId per client (for dedup)
};

/** @brief Per-dispatch context passed to handlers - bundles shared state with the sending peer. */
struct ServerContext
{
    ServerState& state;
    ENetPeer*    peer;
};


// =====================================================================================================================
// ==== Message Handlers ===============================================================================================
// =====================================================================================================================

/** @brief Handles a Hello handshake message - validates protocol version and replies with Welcome. */
void onHello(ServerContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t payloadSize)
{
    MsgHello msgHello{};
    if (!deserializeMsgHello(payload, payloadSize, msgHello))
    {
        LOG_SERVER_WARN("Failed to parse Hello payload");
        return;
    }

    // TODO: Send a Reject message instead of silently dropping on protocol mismatch
    if (msgHello.protocolVersion != kProtocolVersion)
    {
        LOG_SERVER_ERROR("Protocol mismatch (client={}, server={})", msgHello.protocolVersion, kProtocolVersion);
        return;
    }

    const std::string_view playerName(msgHello.name, boundedStrLen(msgHello.name, kPlayerNameMax));
    LOG_SERVER_INFO("Hello from \"{}\"", playerName);

    // Build and send Welcome response
    MsgWelcome welcome{};
    welcome.protocolVersion = kProtocolVersion;
    welcome.clientId = ctx.peer->incomingPeerID;
    welcome.serverTickRate = kServerTickRate;

    if (sendReliable(ctx.state.host, ctx.peer, makeWelcomePacket(welcome, 0, 0)))
    {
        LOG_SERVER_INFO("Sent Welcome to clientId={}", welcome.clientId);
    }
}

/**
 *  @brief Handles an Input message - stores the latest input state for the sending client.
 *
 *  Movement inputs arrive every tick unreliably. The server keeps only the most recent
 *  state per client; older inputs are implicitly superseded.
 */
void onInput(ServerContext& ctx, const PacketHeader& header, const uint8_t* payload, std::size_t size)
{
    MsgInput msgInput{};
    if (!deserializeMsgInput(payload, size, msgInput))
    {
        LOG_SERVER_WARN("Failed to parse Input payload");
        return;
    }

    const uint32_t clientId = ctx.peer->incomingPeerID;

    // Detect new bomb command by comparing to last seen value.
    auto& lastCmd = ctx.state.lastBombCommandId[clientId];
    if (msgInput.bombCommandId != lastCmd)
    {
        lastCmd = msgInput.bombCommandId;

        LOG_SERVER_DEBUG("Bomb request: clientId={} bombCmdId={}",
                         clientId, msgInput.bombCommandId);
        // TODO: Validate & spawn bomb (cooldown, max bombs, position check)
    }

    ctx.state.inputs[clientId] = msgInput;

    if (header.sequence % kInputLogEveryN == 0)
    {
        LOG_SERVER_DEBUG("Input clientId={} seq={} tick={} move=({},{}) bombCmdId={}",
                         clientId, header.sequence, header.tick,
                         static_cast<int>(msgInput.moveX), static_cast<int>(msgInput.moveY),
                         msgInput.bombCommandId);
    }
}

// =====================================================================================================================
// ==== Packet Dispatcher ==============================================================================================
// =====================================================================================================================

/** @brief Creates the server dispatcher and binds all message handlers. */
PacketDispatcher<ServerContext> makeServerDispatcher()
{
    PacketDispatcher<ServerContext> d{};
    d.bind(EMsgType::Hello, &onHello);
    d.bind(EMsgType::Input, &onInput);
    return d;
}

/// Global dispatcher instance - initialized once, immutable thereafter.
static const PacketDispatcher<ServerContext> gDispatcher = makeServerDispatcher();

/** @brief Parses and dispatches a received ENet packet through the server dispatcher. */
void handleEventReceive(const ENetEvent& event, ServerState& state)
{
    LOG_SERVER_TRACE("Received {} bytes on channel {}", event.packet->dataLength, channelName(event.channelID));

    ServerContext ctx{state, event.peer};
    dispatchPacket(gDispatcher, ctx, event.packet->data, event.packet->dataLength);
}

// =====================================================================================================================
// ==== Main Loop ======================================================================================================
// =====================================================================================================================

/**
 * @brief Entry point for the Bomberman dedicated server.
 *
 * Initializes logging and ENet, creates the server host, and runs the
 * event-drain loop until interrupted by SIGINT / SIGTERM.
 */
int main(int argc, char** argv)
{
    CliOptions cli{};
    if (!parseCli(argc, argv, cli))
    {
        return EXIT_FAILURE;
    }

    // Initialize project-wide named loggers.
    bomberman::log::init(cli.logLevel, cli.logFile);

    // Register signal handlers for graceful shutdown.
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    // Initialize ENet.
    if (enet_initialize() != 0)
    {
        LOG_SERVER_ERROR("ENet initialization failed");
        return EXIT_FAILURE;
    }

    // Create the server host.
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = cli.port;

    ENetHost* server = enet_host_create(&address, kMaxPeers, kChannelCount, 0, 0);
    if (server == nullptr)
    {
        LOG_SERVER_ERROR("Failed to create ENet host on port {}", cli.port);
        enet_deinitialize();
        return EXIT_FAILURE;
    }

    LOG_SERVER_INFO("Listening on port {} (max {} peers)", cli.port, kMaxPeers);

    ServerState state{};
    state.host = server;

    // Main event loop - drain all pending events each iteration.
    while (gRunning)
    {
        ENetEvent event{};
        int result = enet_host_service(server, &event, kServiceTimeoutMs);

        while (result > 0)
        {
            switch (event.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                    LOG_SERVER_INFO("Peer connected (id={})", event.peer->incomingPeerID);
                    break;

                case ENET_EVENT_TYPE_RECEIVE:
                    handleEventReceive(event, state);
                    enet_packet_destroy(event.packet);
                    break;

                case ENET_EVENT_TYPE_DISCONNECT:
                    LOG_SERVER_INFO("Peer disconnected (id={})", event.peer->incomingPeerID);
                    state.inputs.erase(event.peer->incomingPeerID);
                    state.lastBombCommandId.erase(event.peer->incomingPeerID);
                    event.peer->data = nullptr;
                    break;

                case ENET_EVENT_TYPE_NONE:
                    break;
            }

            // Drain remaining events without blocking.
            result = enet_host_service(server, &event, 0);
        }

        if (result < 0)
        {
            LOG_SERVER_ERROR("enet_host_service failed, shutting down");
            break;
        }
    }

    // Clean up.
    enet_host_destroy(server);
    enet_deinitialize();
    LOG_SERVER_INFO("Shutdown complete");
    return EXIT_SUCCESS;
}
