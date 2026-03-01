#include <iostream>
#include <string>

#include "Game.h"
#include "Net/NetClient.h"

int main(int /*argc*/, char** /*argv*/)
{
    // 1. Construct NetClient
    bomberman::net::NetClient client;

    // 2. Attempt Connection
    if (!client.connect("127.0.0.1", 12345, "PlayerName"))
    {
        std::cerr << "[client] Failed to connect to the server." << std::endl;
    }

    // 3. Start Game loop
    bomberman::Game game(std::string("bomberman"), 800, 600);
    game.run();

    return 0;
}
