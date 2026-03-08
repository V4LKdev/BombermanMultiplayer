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
 *  Provides `sendReliable()`, `sendUnreliable()`, `broadcastReliable()`, and
 *  `broadcastUnreliable()` so that the create → send → error-check → flush
 *  boilerplate lives in one place, shared by both client and server code.
 *
 *  Broadcast helpers iterate all connected peers and flush exactly once after
 *  the loop, avoiding the per-peer flush overhead of calling sendReliable in
 *  a loop.
 */

namespace bomberman::net
{
    // TODO: Split "queue" and "flush" responsibilities; avoid implicit flush in generic send helpers.
    // TODO: Add sendOnChannel(...) so callers can choose Control/GameState explicitly.
    // TODO: Add lightweight telemetry hooks (queued bytes, send failures, flush count).
    // NOTE: Reliable control packets flush immediately. Unreliable streams are batch-friendly.
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

    /**
     *  @brief Broadcasts a pre-serialized byte buffer reliably to all connected peers.
     *
     *  Queues one packet per connected peer then flushes once.
     *  Safe to call with zero connected peers (no-op).
     *
     *  @return Number of peers successfully queued.
     */
    template<std::size_t N>
    int broadcastReliable(ENetHost* host, const std::array<uint8_t, N>& bytes)
    {
        int sent = 0;
        for (std::size_t i = 0; i < host->peerCount; ++i)
        {
            ENetPeer* peer = &host->peers[i];
            if (peer->state != ENET_PEER_STATE_CONNECTED)
                continue;

            ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
            if (pkt == nullptr)
            {
                LOG_PROTO_ERROR("broadcastReliable: failed to allocate packet for peer {}", i);
                continue;
            }

            if (enet_peer_send(peer, static_cast<uint8_t>(EChannel::Control), pkt) != 0)
            {
                LOG_PROTO_ERROR("broadcastReliable: failed to queue packet for peer {}", i);
                enet_packet_destroy(pkt);
                continue;
            }

            ++sent;
        }

        if (sent > 0)
            enet_host_flush(host);

        return sent;
    }

    /**
     *  @brief Broadcasts a pre-serialized byte buffer unreliably to all connected peers.
     *
     *  Queues one packet per connected peer then flushes once.
     *  Safe to call with zero connected peers (no-op).
     *
     *  @return Number of peers successfully queued.
     */
    template<std::size_t N>
    int broadcastUnreliable(ENetHost* host, const std::array<uint8_t, N>& bytes)
    {
        int sent = 0;
        for (std::size_t i = 0; i < host->peerCount; ++i)
        {
            ENetPeer* peer = &host->peers[i];
            if (peer->state != ENET_PEER_STATE_CONNECTED)
                continue;

            ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(), 0);
            if (pkt == nullptr)
            {
                LOG_PROTO_ERROR("broadcastUnreliable: failed to allocate packet for peer {}", i);
                continue;
            }

            if (enet_peer_send(peer, static_cast<uint8_t>(EChannel::GameState), pkt) != 0)
            {
                LOG_PROTO_ERROR("broadcastUnreliable: failed to queue packet for peer {}", i);
                enet_packet_destroy(pkt);
                continue;
            }

            ++sent;
        }

        if (sent > 0)
            enet_host_flush(host);

        return sent;
    }

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETSEND_H

