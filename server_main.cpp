#include <csignal>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <enet/enet.h>

#include "Net/NetCommon.h"
#include "Server/ServerHandlers.h"
#include "Server/ServerSession.h"
#include "Util/CliCommon.h"
#include "Util/Log.h"

using namespace bomberman::net;

// TODO: Read these from a config file or CLI arguments
namespace
{
    using ServerClock = std::chrono::steady_clock;

    constexpr std::size_t kMaxPeers = 2;
    constexpr int kServiceTimeoutMs = 16;
    constexpr uint32_t kSimStepMs = 1000u / bomberman::server::kServerTickRate;
    constexpr uint32_t kMaxFrameClampMs = 250u;
    constexpr uint32_t kMaxStepsPerFrame = 8;

    /// Global flag for graceful shutdown
    volatile std::sig_atomic_t gRunning = 1;
    void onSignal(int /*sig*/) { gRunning = 0; }

    struct CliOptions
    {
        spdlog::level::level_enum logLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        std::string logFile;
        uint16_t port = kDefaultServerPort;
    };

    void printUsage(const char* exeName)
    {
        std::cout
            << "Usage: " << exeName
            << " [--log-level <trace|debug|info|warn|error|critical>] [--log-file <path>] [--port <port override>]\n";
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
                if (!bomberman::cli::parseLogLevel(value, outOptions.logLevel))
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
                if (!bomberman::cli::parsePort(value, outOptions.port))
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

    LOG_SERVER_INFO("==== BOMBERMAN DEDICATED SERVER ===================================================================");
    LOG_SERVER_INFO("Listening on port {} with max {} peers", cli.port, kMaxPeers);

    bomberman::server::ServerState state{};
    state.host = server;

    auto lastTickTime = ServerClock::now();
    uint32_t accumulatorMs = 0;

    // Main event loop - drain all pending events each iteration.
    while (gRunning)
    {
        const auto currentTickTime = ServerClock::now();
        uint32_t frameDeltaMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(currentTickTime - lastTickTime).count());
        lastTickTime = currentTickTime;

        if (frameDeltaMs > kMaxFrameClampMs)
        {
            frameDeltaMs = kMaxFrameClampMs;
        }
        accumulatorMs += frameDeltaMs;

        int stepCount = 0;
        while (accumulatorMs >= kSimStepMs && stepCount < kMaxStepsPerFrame)
        {
            bomberman::server::simulateServerTick(state);

            accumulatorMs -= kSimStepMs;
            ++stepCount;
        }

        if (stepCount >= kMaxStepsPerFrame)
        {
            LOG_SERVER_WARN("Exceeded max server tick steps ({}), accumulator={}ms", kMaxStepsPerFrame, accumulatorMs);
        }
        
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
                    bomberman::server::handleEventReceive(event, state);
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
