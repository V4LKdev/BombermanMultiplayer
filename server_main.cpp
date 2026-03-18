/**
 * @file server_main.cpp
 * @brief Dedicated-server process bootstrap, run loop, and shutdown.
 */

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <enet/enet.h>

#include "Net/NetCommon.h"
#include "Server/ServerEvents.h"
#include "Server/ServerState.h"
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

    constexpr int                              kServiceTimeoutMs = 1;
    constexpr SimDuration                      kSimStep = SimDuration{1.0 / static_cast<double>(bomberman::sim::kTickRate)};
    constexpr std::chrono::milliseconds        kMaxFrameClamp{bomberman::sim::kMaxFrameClampMs};

    /// Global flag for graceful shutdown
    volatile std::sig_atomic_t gRunning = 1;
    void onSignal(int /*sig*/) { gRunning = 0; }

    struct CliOptions
    {
        bomberman::cli::LoggingCliOptions logging;
        bomberman::cli::DiagnosticsCliOptions diagnostics;
        uint16_t port = kDefaultServerPort;
        uint32_t seed = 0;
        uint32_t inputLeadTicks = static_cast<uint32_t>(bomberman::sim::kDefaultServerInputLeadTicks);
        uint32_t snapshotIntervalTicks = static_cast<uint32_t>(bomberman::sim::kDefaultServerSnapshotIntervalTicks);
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
            << " [--port <port override>] [--seed <seed override>] [--input-lead-ticks <0-"
            << bomberman::server::kMaxBufferedInputLead
            << ">] [--snapshot-interval-ticks <1-" << bomberman::sim::kTickRate << ">]\n"
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

            if (arg == "--input-lead-ticks")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Missing value for --input-lead-ticks\n";
                    printUsage();
                    return ParseCliResult::Error;
                }

                const std::string_view value = argv[++i];
                uint32_t parsedLeadTicks = 0;
                if (!bomberman::cli::parseUint32(value, parsedLeadTicks) ||
                    parsedLeadTicks > bomberman::server::kMaxBufferedInputLead)
                {
                    std::cerr << "Invalid input lead ticks: " << value
                              << " (expected 0-" << bomberman::server::kMaxBufferedInputLead << ")\n";
                    printUsage();
                    return ParseCliResult::Error;
                }

                outOptions.inputLeadTicks = parsedLeadTicks;
                continue;
            }

            if (arg == "--snapshot-interval-ticks")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "Missing value for --snapshot-interval-ticks\n";
                    printUsage();
                    return ParseCliResult::Error;
                }

                const std::string_view value = argv[++i];
                uint32_t parsedIntervalTicks = 0;
                if (!bomberman::cli::parseUint32(value, parsedIntervalTicks) ||
                    parsedIntervalTicks == 0 ||
                    parsedIntervalTicks > static_cast<uint32_t>(bomberman::sim::kTickRate))
                {
                    std::cerr << "Invalid snapshot interval ticks: " << value
                              << " (expected 1-" << bomberman::sim::kTickRate << ")\n";
                    printUsage();
                    return ParseCliResult::Error;
                }

                outOptions.snapshotIntervalTicks = parsedIntervalTicks;
                continue;
            }

            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage();
            return ParseCliResult::Error;
        }

        return ParseCliResult::Ok;
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

    ENetHost* server = enet_host_create(&address, bomberman::server::kServerPeerSessionCapacity, kChannelCount, 0, 0);
    if (server == nullptr)
    {
        LOG_SERVER_ERROR("Failed to create ENet host on port {}", cli.port);
        enet_deinitialize();
        return EXIT_FAILURE;
    }

    LOG_SERVER_INFO("==== BOMBERMAN DEDICATED SERVER ===================================================================");
    LOG_NET_DIAG_INFO("Server diagnostics {}", cli.diagnostics.netDiagEnabled ? "enabled" : "disabled");
    LOG_SERVER_INFO("Listening on port {} with max {} peers ({} gameplay slots)", cli.port, bomberman::server::kServerPeerSessionCapacity, kMaxPlayers);
    LOG_SERVER_DEBUG("Server input lead={} tick(s)", cli.inputLeadTicks);
    LOG_SERVER_DEBUG("Server snapshot interval={} tick(s)", cli.snapshotIntervalTicks);
    LOG_SERVER_DEBUG("ENet peer liveness ping={}ms timeoutLimit={} timeoutRange=[{}..{}]ms",
                     bomberman::net::kPeerPingIntervalMs,
                     bomberman::net::kPeerTimeoutLimit,
                     bomberman::net::kPeerTimeoutMinimumMs,
                     bomberman::net::kPeerTimeoutMaximumMs);

    // Initialize server state.
    bomberman::server::ServerState state{};
    bomberman::server::initServerState(state,
                                       server,
                                       cli.diagnostics.netDiagEnabled,
                                       cli.seedOverride,
                                       cli.seed,
                                       cli.inputLeadTicks,
                                       cli.snapshotIntervalTicks);

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
        if (!bomberman::server::serviceServerEvents(state, kServiceTimeoutMs))
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
