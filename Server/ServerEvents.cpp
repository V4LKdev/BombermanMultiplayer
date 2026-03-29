/**
 * @file ServerEvents.cpp
 * @ingroup authoritative_server
 * @brief Dedicated-server ENet connect, receive, and disconnect event servicing.
 */

#include "ServerEvents.h"

#include <optional>

#include "Net/NetTransportConfig.h"
#include "ServerHandlers.h"
#include "ServerState.h"
#include "Util/Log.h"

namespace bomberman::server
{
    namespace
    {
        void recordServerDiagLifecycle(ServerState& state,
                                       const net::NetPeerLifecycleType type,
                                       const std::optional<uint8_t> playerId = std::nullopt,
                                       const uint32_t transportPeerId = 0)
        {
            state.diag.recordPeerLifecycle(type, playerId.value_or(0xFF), transportPeerId);
        }

        void handleConnectEvent(ServerState& state, ENetPeer& peer)
        {
            net::applyDefaultPeerTransportConfig(&peer);
            if (bindPeerSession(state, peer) == nullptr)
            {
                LOG_SERVER_ERROR("Failed to bind live peer session for enetId={} (capacity={})",
                                 peer.incomingPeerID,
                                 kServerPeerSessionCapacity);
                enet_peer_reset(&peer);
                return;
            }

            LOG_SERVER_DEBUG("Peer connected (id={})", peer.incomingPeerID);
            recordServerDiagLifecycle(state,
                                      net::NetPeerLifecycleType::TransportConnected,
                                      std::nullopt,
                                      peer.incomingPeerID);
        }

        void handleReceiveNetworkEvent(ServerState& state, ENetEvent& event)
        {
            handleReceiveEvent(event, state);
            enet_packet_destroy(event.packet);
        }

        void handleDisconnectEvent(ServerState& state, ENetPeer& peer)
        {
            if (const auto releasedPlayerId = releasePeerSession(state, peer); releasedPlayerId.has_value())
            {
                const uint8_t playerId = releasedPlayerId.value();
                if (const auto& reclaimEntry = state.disconnectedPlayerReclaims[playerId];
                    reclaimEntry.has_value() && !reclaimEntry->playerName.empty())
                {
                    LOG_SERVER_INFO("Peer disconnected (playerId={}, name=\"{}\")",
                                    playerId,
                                    reclaimEntry->playerName);
                }
                else
                {
                    LOG_SERVER_INFO("Peer disconnected (playerId={})", playerId);
                }

                recordServerDiagLifecycle(state,
                                          net::NetPeerLifecycleType::PeerDisconnected,
                                          playerId,
                                          peer.incomingPeerID);
                return;
            }

            LOG_SERVER_DEBUG("Peer disconnected before handshake completed (enetId={})", peer.incomingPeerID);
            recordServerDiagLifecycle(state,
                                      net::NetPeerLifecycleType::TransportDisconnectedBeforeHandshake,
                                      std::nullopt,
                                      peer.incomingPeerID);
            peer.data = nullptr;
        }

        void handleServerEvent(ServerState& state, ENetEvent& event)
        {
            switch (event.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                    handleConnectEvent(state, *event.peer);
                    break;

                case ENET_EVENT_TYPE_RECEIVE:
                    handleReceiveNetworkEvent(state, event);
                    break;

                case ENET_EVENT_TYPE_DISCONNECT:
                    handleDisconnectEvent(state, *event.peer);
                    break;

                case ENET_EVENT_TYPE_NONE:
                    break;
            }
        }
    } // namespace

    bool serviceServerEvents(ServerState& state, const uint32_t serviceTimeoutMs)
    {
        if (state.host == nullptr)
        {
            return false;
        }

        ENetEvent event{};
        int result = enet_host_service(state.host, &event, serviceTimeoutMs);

        while (result > 0)
        {
            handleServerEvent(state, event);
            result = enet_host_service(state.host, &event, 0);
        }

        return result >= 0;
    }

} // namespace bomberman::server
