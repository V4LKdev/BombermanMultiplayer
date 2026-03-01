#include <iostream>
#include <string>
#include <thread>
#include <chrono>

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
    else
    {
        // Temporary input smoke test before gameplay integration.
        const uint16_t tickRate = client.serverTickRate() > 0 ? client.serverTickRate() : 60;
        const auto tickInterval = std::chrono::milliseconds(1000 / tickRate);
        const uint32_t testTicks = tickRate; // ~1 second of traffic

        for (uint32_t tick = 1; tick <= testTicks; ++tick)
        {
            bomberman::net::MsgInput input{};
            input.moveX = (tick % 2 == 0) ? 1 : -1;
            input.moveY = 0;
            input.actionFlags = 0;

            client.sendInput(input, tick);
            client.pump(0);
            std::this_thread::sleep_for(tickInterval);
        }
    }

    // 3. Start Game loop
    bomberman::Game game(std::string("bomberman"), 800, 600);
    game.run();

    return 0;
}
