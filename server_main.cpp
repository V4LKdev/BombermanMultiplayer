#include <csignal>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <enet/enet.h>
#include <optional>

#include "Net/NetCommon.h"
#include "Server/ServerHandlers.h"
#include "Server/ServerSession.h"
#include "Util/CliCommon.h"
#include "Util/Log.h"
#include "Sim/SimConfig.h"

using namespace bomberman::net;

namespace
{
    enum class ParseCliResult : uint8_t
    {
        Ok,
        Help,
        Error
    };

    using ServerClock = std::chrono::steady_clock;
    using SimDuration = std::chrono::duration<double>;

    // Allow a few extra transport-level peers so overflow clients can reach Hello
    // and receive an explicit ServerFull reject instead of timing out in ENet connect.
    constexpr std::size_t kMaxPeers         = kMaxPlayers + 4;
    constexpr int         kServiceTimeoutMs = 1;
    constexpr SimDuration kSimStep          = SimDuration{1.0 / static_cast<double>(bomberman::sim::kTickRate)};
    constexpr auto        kMaxFrameClamp    = std::chrono::milliseconds(bomberman::sim::kMaxFrameClampMs);

    /// Global flag for graceful shutdown
    volatile std::sig_atomic_t gRunning = 1;
    void onSignal(int /*sig*/) { gRunning = 0; }

    struct CliOptions
    {
        bomberman::cli::LoggingCliOptions logging;
        bomberman::cli::DiagnosticsCliOptions diagnostics;
        uint16_t port = kDefaultServerPort;
        uint32_t seed = 0;
        bool seedOverride = false;
    };

    void printUsage()
    {
        std::cout
            << "Usage: "
            << ' ' << bomberman::cli::kLoggingUsageArgs;

        if constexpr (bomberman::cli::kNetDiagAvailable)
            std::cout << ' ' << bomberman::cli::kDiagnosticsUsageArgs;

        std::cout
            << " [--port <port override>] [--seed <seed override>]\n"
            << "       Default log config location: " << bomberman::log::defaultConfigFilePath() << "\n";
    }

    ParseCliResult parseCli(int argc, char** argv, CliOptions& outOptions)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            std::string error;

            if (bomberman::cli::tryParseLoggingOption(argc, argv, i, outOptions.logging, error))
            {
                if (!error.empty())
                {
                    std::cerr << error << '\n';
                    printUsage();
                    return ParseCliResult::Error;
                }

                continue;
            }

            if (bomberman::cli::tryParseDiagnosticsOption(argc, argv, i, outOptions.diagnostics, error))
            {
                if (!error.empty())
                {
                    std::cerr << error << '\n';
                    printUsage();
                    return ParseCliResult::Error;
                }

                continue;
            }

            if (arg == "--help")
            {
                printUsage();
                return ParseCliResult::Help;
            }

            if (arg == "--port")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Missing value for --port\n";
                    printUsage();
                    return ParseCliResult::Error;
                }

                const std::string_view value = argv[++i];
                if (!bomberman::cli::parsePort(value, outOptions.port))
                {
                    std::cerr << "Invalid port: " << value << '\n';
                    printUsage();
                    return ParseCliResult::Error;
                }
                continue;
            }

            if (arg == "--seed")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Missing value for --seed\n";
                    printUsage();
                    return ParseCliResult::Error;
                }

                const std::string_view value = argv[++i];
                if (!bomberman::cli::parseUint32(value, outOptions.seed))
                {
                    std::cerr << "Invalid seed: " << value << '\n';
                    printUsage();
                    return ParseCliResult::Error;
                }

                outOptions.seedOverride = true;
                continue;
            }

            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage();
            return ParseCliResult::Error;
        }

        return ParseCliResult::Ok;
    }


    void recordServerDiagLifecycle(bomberman::server::ServerState& state,
                                   NetPeerLifecycleType type,
                                   std::optional<uint8_t> playerId = std::nullopt,
                                   uint32_t transportPeerId = 0)
    {
        state.diag.recordPeerLifecycle(type, playerId.value_or(0xFF), transportPeerId);
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
    // Parse CLI options.
    CliOptions cli{};
    switch (parseCli(argc, argv, cli))
    {
        case ParseCliResult::Ok:
            break;
        case ParseCliResult::Help:
            return EXIT_SUCCESS;
        case ParseCliResult::Error:
            return EXIT_FAILURE;
    }

    // Resolve log config from CLI options and defaults.
    bomberman::log::LogConfig logConfig{};
    std::string error;
    if (!bomberman::log::resolveConfig(logConfig,
                                       cli.logging.hasLogLevelOverride,
                                       cli.logging.logLevel,
                                       cli.logging.hasLogFileOverride,
                                       cli.logging.logFile,
                                       error))
    {
        std::cerr << error << '\n';
        return EXIT_FAILURE;
    }

    // Initialize project-wide named loggers.
    bomberman::log::init(logConfig);

    // Register signal handlers.
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
    LOG_NET_DIAG_INFO("Server diagnostics {}", cli.diagnostics.netDiagEnabled ? "enabled" : "disabled");
    LOG_SERVER_INFO("Listening on port {} with max {} peers ({} gameplay slots)", cli.port, kMaxPeers, kMaxPlayers);

    // Initialize server state.
    bomberman::server::ServerState state{};
    bomberman::server::initServerState(state, server, cli.diagnostics.netDiagEnabled, cli.seedOverride, cli.seed);

    auto lastTickTime = ServerClock::now();
    SimDuration accumulator{};



    // ----- Main Event Loop -----
    while (gRunning)
    {
        const auto currentTickTime = ServerClock::now();
        auto frameDelta = currentTickTime - lastTickTime;
        lastTickTime = currentTickTime;

        // Clamp large frame deltas to avoid spiral of death after long stalls.
        if (frameDelta > kMaxFrameClamp)
        {
            frameDelta = kMaxFrameClamp;
        }
        accumulator += std::chrono::duration_cast<SimDuration>(frameDelta);

        // Drain all pending ENet events first before advancing the simulation.
        ENetEvent event{};
        int result = enet_host_service(server, &event, kServiceTimeoutMs);

        while (result > 0)
        {
            switch (event.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                    LOG_SERVER_INFO("Peer connected (id={})", event.peer->incomingPeerID);
                    recordServerDiagLifecycle(state,
                                              NetPeerLifecycleType::TransportConnected,
                                              std::nullopt,
                                              event.peer->incomingPeerID);
                    break;

                case ENET_EVENT_TYPE_RECEIVE:
                    bomberman::server::handleEventReceive(event, state);
                    enet_packet_destroy(event.packet);
                    break;

                case ENET_EVENT_TYPE_DISCONNECT:
                {
                    auto* client = static_cast<bomberman::server::ClientState*>(event.peer->data);
                    if (client)
                    {
                        const uint8_t playerId = client->playerId;
                        LOG_SERVER_INFO("Peer disconnected (playerId={})", playerId);
                        recordServerDiagLifecycle(state,
                                                  NetPeerLifecycleType::PeerDisconnected,
                                                  playerId,
                                                  event.peer->incomingPeerID);

                        // Null peer->data BEFORE resetting client state to prevent dangling reads.
                        event.peer->data = nullptr;
                        state.clients[playerId].reset();

                        // Return playerId to the pool.
                        bomberman::server::releasePlayerId(state, playerId);
                    }
                    else
                    {
                        LOG_SERVER_INFO("Peer disconnected (not handshaked, enetId={})", event.peer->incomingPeerID);
                        recordServerDiagLifecycle(state,
                                                  NetPeerLifecycleType::TransportDisconnectedBeforeHandshake,
                                                  std::nullopt,
                                                  event.peer->incomingPeerID);

                        event.peer->data = nullptr;
                    }
                    break;
                }

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

        // ----- Advance Simulation -----
        int stepCount = 0;
        while (accumulator >= kSimStep && stepCount < bomberman::sim::kMaxStepsPerFrame)
        {

            bomberman::server::simulateServerTick(state);

            accumulator -= kSimStep;
            ++stepCount;
        }

        if (stepCount >= bomberman::sim::kMaxStepsPerFrame)
        {
            const auto accumulatorMs = std::chrono::duration<double, std::milli>(accumulator).count();
            LOG_SERVER_WARN("Exceeded max server tick steps ({}), accumulator={:.3f}ms", bomberman::sim::kMaxStepsPerFrame, accumulatorMs);
        }
    }



    // Write diagnostics report before tearing down ENet resources.
    state.diag.endSession();
    if (cli.diagnostics.netDiagEnabled)
    {
        std::filesystem::create_directories("logs");
        if (state.diag.writeSessionReport("logs/server_diag.txt"))
        {
            LOG_SERVER_INFO("Diagnostics report written to logs/server_diag.txt");
        }
        else
        {
            LOG_SERVER_ERROR("Failed to write diagnostics report");
        }
    }

    // Clean up.
    enet_host_destroy(server);
    enet_deinitialize();
    LOG_SERVER_INFO("Shutdown complete");
    return EXIT_SUCCESS;
}
