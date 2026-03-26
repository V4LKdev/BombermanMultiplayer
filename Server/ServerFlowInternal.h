/**
 * @file ServerFlowInternal.h
 * @brief Private helpers shared by authoritative server-flow implementation files.
 */

#ifndef BOMBERMAN_SERVER_SERVERFLOWINTERNAL_H
#define BOMBERMAN_SERVER_SERVERFLOWINTERNAL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Net/NetSend.h"
#include "ServerHandlers.h"
#include "ServerState.h"
#include "Sim/SimConfig.h"

namespace bomberman::server::flow_internal
{
    static_assert(net::kMaxPlayers <= 32, "Current match-player mask assumes at most 32 player ids");

    inline constexpr uint32_t kLobbyCountdownTicks = static_cast<uint32_t>(sim::kTickRate) * 3u;
    inline constexpr uint32_t kMatchStartLoadTimeoutTicks = static_cast<uint32_t>(sim::kTickRate) * 5u;
    inline constexpr uint32_t kMatchStartGoDelayTicks = static_cast<uint32_t>(sim::kTickRate);
    inline constexpr uint32_t kMatchStartUnlockDelayTicks = kMatchStartGoDelayTicks;
    inline constexpr uint32_t kEndOfMatchReturnTicks = static_cast<uint32_t>(sim::kTickRate) * 3u;

    [[nodiscard]]
    constexpr uint32_t playerMaskBit(const uint8_t playerId)
    {
        return 1u << playerId;
    }

    template<std::size_t N>
    bool queueReliableControlToPlayer(ServerState& state,
                                      const uint8_t playerId,
                                      const net::EMsgType type,
                                      const std::size_t payloadSize,
                                      const std::array<uint8_t, N>& packet)
    {
        const auto* session = findPeerSessionByPlayerId(state, playerId);
        const bool queued = session != nullptr &&
                            session->peer != nullptr &&
                            net::queueReliableControl(session->peer, packet);

        state.diag.recordPacketSent(type,
                                    playerId,
                                    static_cast<uint8_t>(net::EChannel::ControlReliable),
                                    net::kPacketHeaderSize + payloadSize,
                                    queued ? net::NetPacketResult::Ok : net::NetPacketResult::Dropped);

        return queued;
    }

    [[nodiscard]]
    uint8_t computeCountdownSecondsRemaining(const ServerState& state);

    void cancelStartingMatch(ServerState& state, uint32_t notifyMask, std::string_view reason);
} // namespace bomberman::server::flow_internal

#endif // BOMBERMAN_SERVER_SERVERFLOWINTERNAL_H
