#include <string>

#include "Game.h"
#include "Net/NetClient.h"

int main(int /*argc*/, char** /*argv*/)
{
    bomberman::net::NetClient client;
    client.handshake("127.0.0.1", 12345, "PlayerName");

    // init game
    bomberman::Game game(std::string("bomberman"), 800, 600);
    // run game loop
    game.run();

    return 0;
}
