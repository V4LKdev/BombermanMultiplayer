#ifndef BOMBERMAN_SERVERSESSION_H
#define BOMBERMAN_SERVERSESSION_H

#include <unordered_map>
#include <enet/enet.h>

#include "Net/NetCommon.h"

namespace bomberman::server
{
    constexpr uint16_t kServerTickRate = 60;

    // TODO: Evolve into a better ClientState with input history, ack tracking, etc.

    /** @brief Long-lived server state shared across all dispatch calls. */
    struct ServerState
    {
        ENetHost* host = nullptr;
        uint32_t serverTick = 0;

        std::unordered_map<uint32_t, net::MsgInput> inputs;            ///< Latest input state per connected client
        std::unordered_map<uint32_t, uint16_t> lastBombCommandId;      ///< Last seen bombCommandId per client (for dedup)
    };

    /** @brief Per-dispatch context passed to handlers - bundles shared state with the sending peer. */
    struct ServerContext
    {
        ServerState& state;
        ENetPeer*    peer;
    };

    /** @brief Advances the server simulation by one tick - processes inputs, updates game state, etc. */
    void simulateServerTick(ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSESSION_H
