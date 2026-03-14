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
        bomberman::cli::LoggingCliOptions logging;
        uint16_t port = bomberman::net::kDefaultServerPort;
        bool mute = false;
    };

    void printUsage(const char* exeName)
    {
        std::cout
            << "Usage: " << exeName << ' ' << bomberman::cli::kLoggingUsageArgs
            << " [--port <port override>] [--mute]\n"
            << "       Default log config: " << bomberman::log::defaultConfigFilePath() << " (if present)\n";
    }

    bool parseCli(int argc, char** argv, CliOptions& outOptions)
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
                    printUsage(argv[0]);
                    return false;
                }

                continue;
            }

            if (arg == "--help")
            {
                printUsage(argv[0]);
                return false;
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
