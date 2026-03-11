#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "Game.h"
#include "Net/NetClient.h"
#include "Util/CliCommon.h"
#include "Util/Log.h"

namespace
{
    struct CliOptions
    {
        spdlog::level::level_enum logLevel = static_cast<spdlog::level::level_enum>(BOMBERMAN_DEFAULT_LOG_LEVEL);
        std::string logFile;
        uint16_t port = bomberman::net::kDefaultServerPort;
        bool mute = false;
    };

    void printUsage(const char* exeName)
    {
        std::cout
            << "Usage: " << exeName << " [--log-level <trace|debug|info|warn|error|critical>] [--log-file <path>] [--port <port override>] [--mute]\n";
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

            if (arg == "--mute")
            {
                outOptions.mute = true;
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
 * Initializes logging, and runs the game loop.
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

    // Initiate async connection to server.
    bomberman::net::NetClient client;

    // Create game instance.
    bomberman::Game game(
        "bomberman",
        800,
        600,
        &client,
        cli.port,
        cli.mute);

    game.run();

    return 0;
}
