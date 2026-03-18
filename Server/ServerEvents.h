/**
 * @file ServerEvents.h
 * @brief Dedicated-server ENet event servicing entry point.
 */

#ifndef BOMBERMAN_SERVEREVENTS_H
#define BOMBERMAN_SERVEREVENTS_H

#include <cstdint>

namespace bomberman::server
{
    struct ServerState;

    /**
     * @brief Services and drains pending ENet events for the dedicated server.
     *
     * Owns connect, receive, and disconnect event semantics for the
     * authoritative server module. Packet destruction remains internal to this
     * helper so the caller can stay focused on process orchestration.
     *
     * @return `true` on success, `false` if ENet reported a service error.
     */
    [[nodiscard]]
    bool serviceServerEvents(ServerState& state, uint32_t serviceTimeoutMs);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVEREVENTS_H
