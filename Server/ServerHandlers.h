/**
 * @file ServerHandlers.h
 * @ingroup authoritative_server
 * @brief Authoritative server receive-path entry point and shared handler declarations.
 */

#ifndef BOMBERMAN_SERVERHANDLERS_H
#define BOMBERMAN_SERVERHANDLERS_H

#include "ServerState.h"

namespace bomberman::server
{
    /** @brief Returns the accepted player id bound to a live peer, if any. */
    [[nodiscard]]
    std::optional<uint8_t> acceptedPlayerId(const ENetPeer* peer);

    /** @brief Handles one validated Hello packet. */
    void onHello(PacketDispatchContext& ctx,
                 const net::PacketHeader& header,
                 const uint8_t* payload,
                 std::size_t payloadSize);

    /** @brief Handles one lobby ready-toggle request from an accepted seat. */
    void onLobbyReady(PacketDispatchContext& ctx,
                      const net::PacketHeader& header,
                      const uint8_t* payload,
                      std::size_t payloadSize);

    /** @brief Handles one match-loaded acknowledgement during `StartingMatch`. */
    void onMatchLoaded(PacketDispatchContext& ctx,
                       const net::PacketHeader& header,
                       const uint8_t* payload,
                       std::size_t payloadSize);

    /** @brief Handles one validated gameplay input batch. */
    void onInput(PacketDispatchContext& ctx,
                 const net::PacketHeader& header,
                 const uint8_t* payload,
                 std::size_t payloadSize);

    /** @brief Rebuilds and broadcasts the current lobby state. */
    void broadcastLobbyState(ServerState& state);

    /** @brief Parses, validates, and dispatches one received ENet packet. */
    void handleReceiveEvent(const ENetEvent& event, ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVERHANDLERS_H
