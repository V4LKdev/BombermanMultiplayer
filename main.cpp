#include "Game.h"
#include "Net/NetClient.h"
#include "Util/Log.h"

/**
 * @brief Entry point for the Bomberman client application.
 *
 * Initializes logging, attempts a server connection, and runs the game loop.
 * If the connection fails, the game continues in offline mode.
 */
int main(int /*argc*/, char** /*argv*/)
{
    // Initialize project-wide named loggers.
    bomberman::log::init();

    // TODO: Read host, port, and player name from CLI args or config file. Also refactor into non-blocking statemachine
    bomberman::net::NetClient client;
    const auto connectResult = client.connect("127.0.0.1", 12345, "PlayerName");

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
