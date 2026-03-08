#ifndef BOMBERMAN_SERVERHANDLERS_H
#define BOMBERMAN_SERVERHANDLERS_H

#include <enet/enet.h>

namespace bomberman::server
{
    struct ServerState;

    /** @brief Parses and dispatches a received ENet packet through the server dispatcher. */
    void handleEventReceive(const ENetEvent& event, ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVERHANDLERS_H
