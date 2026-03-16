#ifndef BOMBERMAN_NET_NETTRANSPORTCONFIG_H
#define BOMBERMAN_NET_NETTRANSPORTCONFIG_H

#include <enet/enet.h>

#include "NetCommon.h"

namespace bomberman::net
{
    /**
     * @brief Applies the project's default transport-level liveness policy to one ENet peer.
     *
     * This is transport liveness only. It does not replace higher-level gameplay/input idle policy.
     */
    inline void applyDefaultPeerTransportConfig(ENetPeer* peer)
    {
        if (peer == nullptr)
            return;

        enet_peer_ping_interval(peer, kPeerPingIntervalMs);
        enet_peer_timeout(peer,
                          kPeerTimeoutLimit,
                          kPeerTimeoutMinimumMs,
                          kPeerTimeoutMaximumMs);
    }
} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETTRANSPORTCONFIG_H
