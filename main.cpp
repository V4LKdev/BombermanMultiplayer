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
    enum class ParseCliResult : uint8_t
    {
        Ok,
        Help,
        Error
    };

    struct CliOptions
    {
        bomberman::cli::LoggingCliOptions logging;
        uint16_t port = bomberman::net::kDefaultServerPort;
        bool mute = false;
    };

    void printUsage()
    {
        std::cout
            << "Usage: " << bomberman::cli::kLoggingUsageArgs
            << " [--port <port override>] [--mute]\n"
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

            if (arg == "--mute")
            {
                outOptions.mute = true;
                continue;
            }

            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage();
            return ParseCliResult::Error;
        }

        return ParseCliResult::Ok;
    }
} // namespace


/**
 * @brief Entry point for the Bomberman client application.
 *
 * Initializes logging, and runs the game loop.
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
