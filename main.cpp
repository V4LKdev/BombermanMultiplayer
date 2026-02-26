#include <iostream>
#include <ostream>
#include <string>

#include "Game.h"
#include "Net/NetClient.h"

int main(int /*argc*/, char** /*argv*/)
{
    bomberman::net::NetClient client;
    if (!client.connect("127.0.0.1", 12345, "PlayerName"))
    {
        std::cerr << "[client] Failed to connect to the server." << std::endl;
    }

    // init game
    bomberman::Game game(std::string("bomberman"), 800, 600);
    // run game loop
    game.run();

    client.disconnect(); // Put in destructor later?

    return 0;
}
