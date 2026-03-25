/**
 * @file ServerHandlers.h
 * @brief Authoritative server receive-path entry point and shared handler declarations.
 */

#ifndef BOMBERMAN_SERVERHANDLERS_H
#define BOMBERMAN_SERVERHANDLERS_H

#include "ServerState.h"

namespace bomberman::server
{
    /** @brief Handles one validated Hello packet on the authoritative server receive path. */
    void onHello(PacketDispatchContext& ctx,
                 const net::PacketHeader& header,
                 const uint8_t* payload,
                 std::size_t payloadSize);

    /** @brief Handles one authoritative lobby ready-toggle request from an accepted seat. */
    void onLobbyReady(PacketDispatchContext& ctx,
                      const net::PacketHeader& header,
                      const uint8_t* payload,
                      std::size_t payloadSize);

    /** @brief Handles one authoritative match-loaded acknowledgement during `StartingMatch`. */
    void onMatchLoaded(PacketDispatchContext& ctx,
                       const net::PacketHeader& header,
                       const uint8_t* payload,
                       std::size_t payloadSize);

    /** @brief Handles one validated gameplay input batch from an accepted in-match player. */
    void onInput(PacketDispatchContext& ctx,
                 const net::PacketHeader& header,
                 const uint8_t* payload,
                 std::size_t payloadSize);

    /** @brief Rebuilds and broadcasts the current authoritative lobby state to all accepted peers. */
    void broadcastLobbyState(ServerState& state);

    /**
     * @brief Validates and dispatches one received ENet packet for the dedicated server.
     *
     * Performs header parsing, channel validation, typed handler dispatch, and
     * diagnostics classification. Packet destruction remains the caller's
     * responsibility because ENet event ownership stays with the caller.
     */
    void handleReceiveEvent(const ENetEvent& event, ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVERHANDLERS_H
