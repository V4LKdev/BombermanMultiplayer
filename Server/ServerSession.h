#ifndef BOMBERMAN_SERVERSESSION_H
#define BOMBERMAN_SERVERSESSION_H

#include <unordered_map>
#include <enet/enet.h>

#include "Const.h"
#include "Net/NetCommon.h"
#include "Sim/Movement.h"

namespace bomberman::server
{

    /** @brief Authoritative per-client state on the server. */
    struct ClientState
    {
        net::MsgInput input{};          ///< Latest received input.
        uint16_t lastBombCommandId = 0; ///< Last seen bombCommandId.
        sim::TilePos pos{};             ///< Authoritative position in tile-Q8.
    };

    /** @brief Long-lived server state shared across all dispatch calls. */
    struct ServerState
    {
        ENetHost* host = nullptr;
        uint32_t serverTick = 0;
        uint32_t nextStateSequence = 0; ///< Monotonically increasing sequence number for state packets.

        std::unordered_map<uint32_t, ClientState> clients; ///< Per-client authoritative state.

        uint32_t mapSeed = 0;
        sim::TileMap tiles;
    };

    /** @brief Initialises a ServerState to a clean pre-game state. */
    void initServerState(ServerState& state, ENetHost* host, bool overrideMapSeed = false, uint32_t mapSeed = 0);

    /** @brief Per-dispatch context passed to handlers. */
    struct ServerContext
    {
        ServerState& state;
        ENetPeer*    peer;
    };

    /** @brief Advances the server simulation by one tick. */
    void simulateServerTick(ServerState& state);

    /** @brief Builds a `MsgState` snapshot from the current `ServerState` for broadcasting to clients. */
    net::MsgState buildStateSnapshot(const ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSESSION_H
