#ifndef BOMBERMAN_NET_NETSEND_H
#define BOMBERMAN_NET_NETSEND_H

#include <array>
#include <cstddef>

#include <enet/enet.h>

#include "NetCommon.h"
#include "Util/Log.h"

/**
 * @file NetSend.h
 * @brief Thin wrappers around ENet packet creation, queueing, and flushing.
 */

namespace bomberman::net
{
    // ----- Queue Helpers -----

    // This layer is transport-focused: queue onto channels and flush explicitly.

    /**
     * @brief Queues a pre-serialized byte buffer on an explicit channel.
     *
     * @warning `peer` must be a valid ENet peer owned by the active host.
     * This helper does not validate protocol/message pairing.
     *
     * @return `true` if the packet was queued successfully.
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

    /** @brief Flushes all currently queued packets for this host. */
    inline void flush(ENetHost* host)
    {
        if (host == nullptr) return;

        enet_host_flush(host);
    }

    // ----- Per-Peer Channel Helpers -----

    /** @brief Queues a reliable control packet. */
    template<std::size_t N>
    bool queueReliableControl(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::ControlReliable, ENET_PACKET_FLAG_RELIABLE, bytes);
    }

    /** @brief Queues a reliable gameplay packet. */
    template<std::size_t N>
    bool queueReliableGame(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::GameplayReliable, ENET_PACKET_FLAG_RELIABLE, bytes);
    }

    /** @brief Queues an unreliable input packet. */
    template<std::size_t N>
    bool queueUnreliableInput(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::InputUnreliable, 0, bytes);
    }

    /** @brief Queues an unreliable snapshot packet. */
    template<std::size_t N>
    bool queueUnreliableSnapshot(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::SnapshotUnreliable, 0, bytes);
    }

    /** @brief Queues an unreliable owner-correction packet. */
    template<std::size_t N>
    bool queueUnreliableCorrection(ENetPeer* peer, const std::array<uint8_t, N>& bytes)
    {
        return queueOnChannel(peer, EChannel::CorrectionUnreliable, 0, bytes);
    }

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETSEND_H
