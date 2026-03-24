/**
 * @file ServerHandlers.h
 * @brief Authoritative server receive-path entry point for typed protocol dispatch.
 */

#ifndef BOMBERMAN_SERVERHANDLERS_H
#define BOMBERMAN_SERVERHANDLERS_H

#include <enet/enet.h>

namespace bomberman::server
{
    struct ServerState;

    /**
     * @brief Validates and dispatches one received ENet packet for the dedicated server.
     *
     * Performs header parsing, channel validation, typed handler dispatch, and
     * diagnostics classification. Packet destruction remains the caller's
     * responsibility because ENet event ownership stays with the caller.
     */
    void handleReceiveEvent(const ENetEvent& event, ServerState& state);

    /** @brief Rebuilds and broadcasts the current authoritative lobby state to all accepted peers. */
    void broadcastLobbyState(ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVERHANDLERS_H
