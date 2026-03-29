/**
 * @file ServerEvents.h
 * @ingroup authoritative_server
 * @brief Dedicated-server ENet event servicing entry point.
 */

#ifndef BOMBERMAN_SERVEREVENTS_H
#define BOMBERMAN_SERVEREVENTS_H

#include <cstdint>

namespace bomberman::server
{
    struct ServerState;

    /** @brief Services pending ENet events for the dedicated server. */
    [[nodiscard]]
    bool serviceServerEvents(ServerState& state, uint32_t serviceTimeoutMs);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVEREVENTS_H
