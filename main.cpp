#include <charconv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

#include "Game.h"
#include "Net/NetClient.h"
#include "Util/Log.h"

namespace
{
    struct CliOptions
    {
        spdlog::level::level_enum logLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        std::string logFile;
        uint16_t port = 12345; // Default port
    };

    void printUsage(const char* exeName)
    {
        std::cout
            << "Usage: " << exeName << " [--log-level <trace|debug|info|warn|error|critical>] [--log-file <path>] [--port <port override>]\n";
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


/**
 * @brief Entry point for the Bomberman client application.
 *
 * Initializes logging, attempts a server connection, and runs the game loop.
 * If the connection fails, the game continues in offline mode.
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

    // TODO: Read host, port, and player name from CLI args or config file. Also refactor into non-blocking statemachine
    bomberman::net::NetClient client;
    const auto connectResult = client.connect("127.0.0.1", cli.port, "PlayerName");

    if (connectResult != bomberman::net::EConnectState::Connected)
    {
        LOG_CLIENT_WARN("Could not connect to server ({}) -> running in offline mode",
                        bomberman::net::connectStateName(connectResult));
    }

    // Pass network client only when connected, otherwise run offline.
    bomberman::Game game(
        "bomberman",
        800,
        600,
        client.isConnected() ? &client : nullptr);

    game.run();

    return 0;
}
