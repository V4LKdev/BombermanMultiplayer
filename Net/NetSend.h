#ifndef BOMBERMAN_NET_NETSEND_H
#define BOMBERMAN_NET_NETSEND_H

#include <array>
#include <cstddef>

#include <enet/enet.h>
#include "NetCommon.h"
#include "Util/Log.h"

/**
 *  @brief NetSend.h - Thin wrappers around ENet packet creation and sending.
 *
 *  Provides `sendReliable()` and `sendUnreliable()` so that the
 *  create → send → error-check → flush boilerplate lives in one place,
 *  shared by both client and server code.
 */

namespace bomberman::net
{
    // TODO: Split "queue" and "flush" responsibilities; avoid implicit flush in generic send helpers.
    // TODO: Add sendOnChannel(...) so callers can choose Control/GameState explicitly.
    // TODO: Add broadcast helpers for server snapshot/event fanout.
    // TODO: Add lightweight telemetry hooks (queued bytes, send failures, flush count).
    // NOTE: Reliable control packets may flush immediately. unreliable streams should be batch-friendly.
    // NOTE: This layer should stay transport-focused.

    /**
     *  @brief Sends a pre-serialized byte buffer reliably on the Control channel.
     *
     *  @return true if the packet was queued successfully.
     */
    template<std::size_t N>
    bool sendReliable(ENetHost* host, ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        if (pkt == nullptr)
        {
            LOG_PROTO_ERROR("Failed to allocate reliable packet ({} bytes)", N);
            return false;
        }

        if (enet_peer_send(peer, static_cast<uint8_t>(EChannel::Control), pkt) != 0)
        {
            LOG_PROTO_ERROR("Failed to queue reliable packet");
            enet_packet_destroy(pkt);
            return false;
        }

        enet_host_flush(host);
        return true;
    }

    /**
     *  @brief Sends a pre-serialized byte buffer unreliably on the GameState channel.
     *
     *  @return true if the packet was queued successfully.
     */
    template<std::size_t N>
    bool sendUnreliable([[maybe_unused]] ENetHost* host, ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(), 0);
        if (pkt == nullptr)
        {
            LOG_PROTO_ERROR("Failed to allocate unreliable packet ({} bytes)", N);
            return false;
        }

        if (enet_peer_send(peer, static_cast<uint8_t>(EChannel::GameState), pkt) != 0)
        {
            LOG_PROTO_ERROR("Failed to queue unreliable packet");
            enet_packet_destroy(pkt);
            return false;
        }

        return true;
    }

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETSEND_H

