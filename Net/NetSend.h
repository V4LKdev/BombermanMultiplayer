#ifndef BOMBERMAN_NET_NETSEND_H
#define BOMBERMAN_NET_NETSEND_H

#include <array>
#include <cstddef>

#include <enet/enet.h>
#include "NetCommon.h"
#include "Util/Log.h"

/**
 *  @brief NetSend.h - Thin wrappers around ENet packet creation and sending.
 */

namespace bomberman::net
{
    // NOTE: This layer is intentionally transport-focused: queue onto channels, flush explicitly.

    /**
     *  @brief Queues a pre-serialized byte buffer on an explicit channel with explicit ENet flags.
     *
     *  @return true if the packet was queued successfully.
     */
    template<std::size_t N>
    bool queueOnChannel(ENetPeer* peer, EChannel channel, uint32_t flags, const std::array<uint8_t, N>& bytes)
    {
        ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(), flags);
        if (pkt == nullptr)
        {
            LOG_NET_PACKET_ERROR("Failed to allocate packet ({} bytes) for channel {}",
                                 N, channelName(static_cast<uint8_t>(channel)));
            return false;
        }

        if (enet_peer_send(peer, static_cast<uint8_t>(channel), pkt) != 0)
        {
            LOG_NET_PACKET_ERROR("Failed to queue packet on channel {}",
                                 channelName(static_cast<uint8_t>(channel)));
            enet_packet_destroy(pkt);
            return false;
        }

        return true;
    }

    /**
     *  @brief Queues a pre-serialized byte buffer for all connected peers on an explicit channel.
     *
     *  Safe to call with zero connected peers (no-op).
     *
     *  @return Number of peers successfully queued.
     */
    template<std::size_t N>
    int broadcastQueueOnChannel(ENetHost* host, EChannel channel, uint32_t flags, const std::array<uint8_t, N>& bytes)
    {
        if (host == nullptr) return 0;

        int sent = 0;
        for (std::size_t i = 0; i < host->peerCount; ++i)
        {
            ENetPeer* peer = &host->peers[i];
            if (peer->state != ENET_PEER_STATE_CONNECTED)
                continue;

            ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(), flags);
            if (pkt == nullptr)
            {
                LOG_NET_PACKET_ERROR("broadcastQueueOnChannel: failed to allocate packet for peer {} on channel {}",
                                     i, channelName(static_cast<uint8_t>(channel)));
                continue;
            }

            if (enet_peer_send(peer, static_cast<uint8_t>(channel), pkt) != 0)
            {
                LOG_NET_PACKET_ERROR("broadcastQueueOnChannel: failed to queue packet for peer {} on channel {}",
                                     i, channelName(static_cast<uint8_t>(channel)));
                enet_packet_destroy(pkt);
                continue;
            }

            ++sent;
        }

        return sent;
    }

    /**
     * @brief Flushes all queued packets for this host.
     */
    inline void flush(ENetHost* host)
    {
        if (host == nullptr) return;

        enet_host_flush(host);
    }



    template<std::size_t N>
    bool queueReliableControl(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::ControlReliable, ENET_PACKET_FLAG_RELIABLE, bytes);
    }

    template<std::size_t N>
    bool queueReliableGame(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::GameReliable, ENET_PACKET_FLAG_RELIABLE, bytes);
    }

    template<std::size_t N>
    bool queueUnreliableInput(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::InputUnreliable, 0, bytes);
    }

    template<std::size_t N>
    bool queueUnreliableSnapshot(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::SnapshotUnreliable, 0, bytes);
    }

    template<std::size_t N>
    bool queueUnreliableCorrection(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::CorrectionUnreliable, 0, bytes);
    }

    template<std::size_t N>
    int broadcastQueuedReliableControl(ENetHost* host, const std::array<uint8_t, N>& bytes)
    {
        return broadcastQueueOnChannel(host, EChannel::ControlReliable, ENET_PACKET_FLAG_RELIABLE, bytes);
    }

    template<std::size_t N>
    int broadcastQueuedUnreliableSnapshot(ENetHost* host, const std::array<uint8_t, N>& bytes)
    {
        return broadcastQueueOnChannel(host, EChannel::SnapshotUnreliable, 0, bytes);
    }

    template<std::size_t N>
    int broadcastQueuedReliableGame(ENetHost* host, const std::array<uint8_t, N>& bytes)
    {
        return broadcastQueueOnChannel(host, EChannel::GameReliable, ENET_PACKET_FLAG_RELIABLE, bytes);
    }

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETSEND_H
