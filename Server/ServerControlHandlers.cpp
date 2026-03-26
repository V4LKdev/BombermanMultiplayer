/**
 * @file ServerControlHandlers.cpp
 * @brief Authoritative server control-message handling and lobby-state broadcast.
 */

#include "ServerHandlers.h"

#include <optional>
#include <string_view>

#include "Net/NetSend.h"
#include "ServerFlow.h"
#include "Sim/SimConfig.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    namespace
    {
        constexpr std::string_view rejectReasonName(const MsgReject::EReason reason)
        {
            using enum MsgReject::EReason;

            switch (reason)
            {
                case VersionMismatch: return "version mismatch";
                case ServerFull:      return "server full";
                case Banned:          return "banned";
                case GameInProgress:  return "game in progress";
                case Other:           return "other";
                default:              return "unknown";
            }
        }

        void recordPeerLifecycle(NetDiagnostics* diag,
                                 const NetPeerLifecycleType type,
                                 const uint8_t peerId,
                                 const uint32_t transportPeerId,
                                 const std::string_view note = {})
        {
            if (!diag)
            {
                return;
            }

            diag->recordPeerLifecycle(type, peerId, transportPeerId, note);
        }

        void recordControlPacketSent(const PacketDispatchContext& ctx,
                                     const EMsgType type,
                                     const uint8_t peerId,
                                     const std::size_t payloadSize,
                                     const NetPacketResult result = NetPacketResult::Ok)
        {
            if (!ctx.diag)
            {
                return;
            }

            ctx.diag->recordPacketSent(type,
                                       peerId,
                                       static_cast<uint8_t>(EChannel::ControlReliable),
                                       kPacketHeaderSize + payloadSize,
                                       result);
        }

        void sendReject(const PacketDispatchContext& ctx, const MsgReject::EReason reason)
        {
            MsgReject reject{};
            reject.reason = reason;
            reject.expectedProtocolVersion = reason == MsgReject::EReason::VersionMismatch ? kProtocolVersion : 0;
            if (ctx.diag)
            {
                ctx.diag->recordRejectReason(reason);
            }

            recordPeerLifecycle(ctx.diag,
                                NetPeerLifecycleType::PeerRejected,
                                ctx.recordedPlayerId.value_or(0xFF),
                                ctx.peer->incomingPeerID,
                                rejectReasonName(reason));

            if (const bool sent = queueReliableControl(ctx.peer, makeRejectPacket(reject)); sent)
            {
                flush(ctx.state.host);
                LOG_NET_CONN_DEBUG("Sent Reject (reason={}) to peer {}",
                                   static_cast<int>(reason),
                                   ctx.peer->incomingPeerID);
                recordControlPacketSent(ctx, EMsgType::Reject, ctx.recordedPlayerId.value_or(0xFF), kMsgRejectSize);
            }
            else
            {
                recordControlPacketSent(ctx,
                                        EMsgType::Reject,
                                        ctx.recordedPlayerId.value_or(0xFF),
                                        kMsgRejectSize,
                                        NetPacketResult::Dropped);
            }

            enet_peer_disconnect_later(ctx.peer, 0);
        }

        [[nodiscard]]
        bool hasAcceptedPlayer(const ENetPeer* peer)
        {
            const auto* session = getPeerSession(peer);
            return session != nullptr && session->playerId.has_value();
        }

        [[nodiscard]]
        bool roundRequiresAdditionalBootstrap(const ServerState& state)
        {
            return state.phase == ServerPhase::StartingMatch ||
                   state.phase == ServerPhase::InMatch ||
                   state.phase == ServerPhase::EndOfMatch;
        }

        [[nodiscard]]
        std::optional<uint8_t> acceptedPlayerId(const ENetPeer* peer)
        {
            const auto* session = getPeerSession(peer);
            return (session != nullptr) ? session->playerId : std::nullopt;
        }

        [[nodiscard]]
        std::optional<uint8_t> reserveHelloPlayerId(PacketDispatchContext& ctx)
        {
            if (ctx.state.playerIdPoolSize == 0)
            {
                LOG_NET_CONN_WARN("Server full ({}/{}) - rejecting peer {}",
                                  static_cast<int>(kMaxPlayers),
                                  static_cast<int>(kMaxPlayers),
                                  ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Rejected;
                sendReject(ctx, MsgReject::EReason::ServerFull);
                return std::nullopt;
            }

            const auto reservedPlayerId = acquirePlayerId(ctx.state);
            if (!reservedPlayerId.has_value())
            {
                ctx.receiveResult = NetPacketResult::Rejected;
                sendReject(ctx, MsgReject::EReason::ServerFull);
                return std::nullopt;
            }

            ctx.recordedPlayerId = reservedPlayerId;
            return reservedPlayerId;
        }

        [[nodiscard]]
        bool queueWelcomeForHello(PacketDispatchContext& ctx, const uint8_t playerId)
        {
            MsgWelcome welcome{};
            welcome.protocolVersion = kProtocolVersion;
            welcome.playerId = playerId;
            welcome.serverTickRate = sim::kTickRate;

            if (!queueReliableControl(ctx.peer, makeWelcomePacket(welcome)))
            {
                LOG_NET_CONN_ERROR("Failed to send Welcome to peer {} - rejecting", ctx.peer->incomingPeerID);
                recordControlPacketSent(ctx,
                                        EMsgType::Welcome,
                                        playerId,
                                        kMsgWelcomeSize,
                                        NetPacketResult::Dropped);
                releasePlayerId(ctx.state, playerId);
                ctx.receiveResult = NetPacketResult::Rejected;
                sendReject(ctx, MsgReject::EReason::Other);
                return false;
            }

            LOG_NET_CONN_DEBUG("Queued Welcome to playerId={}", playerId);
            recordControlPacketSent(ctx, EMsgType::Welcome, playerId, kMsgWelcomeSize);
            return true;
        }

        [[nodiscard]]
        MsgLobbyState buildLobbyState(const ServerState& state)
        {
            MsgLobbyState lobbyState{};
            lobbyState.phase = state.phase == ServerPhase::LobbyCountdown
                ? MsgLobbyState::EPhase::Countdown
                : MsgLobbyState::EPhase::Idle;
            lobbyState.countdownSecondsRemaining = 0;
            if (state.phase == ServerPhase::LobbyCountdown &&
                state.currentLobbyCountdownDeadlineTick > state.serverTick)
            {
                const uint32_t remainingTicks = state.currentLobbyCountdownDeadlineTick - state.serverTick;
                lobbyState.countdownSecondsRemaining =
                    static_cast<uint8_t>((remainingTicks + static_cast<uint32_t>(sim::kTickRate) - 1u) /
                                         static_cast<uint32_t>(sim::kTickRate));
            }

            for (uint8_t playerId = 0; playerId < kMaxPlayers; ++playerId)
            {
                const auto* session = findPeerSessionByPlayerId(state, playerId);
                if (session == nullptr)
                {
                    continue;
                }

                const auto& slotEntry = state.playerSlots[playerId];
                if (!slotEntry.has_value())
                {
                    continue;
                }

                auto& seat = lobbyState.seats[playerId];
                const auto& slot = slotEntry.value();

                uint8_t flags = static_cast<uint8_t>(MsgLobbyState::SeatEntry::ESeatFlags::Occupied);
                if (slot.ready)
                {
                    flags |= static_cast<uint8_t>(MsgLobbyState::SeatEntry::ESeatFlags::Ready);
                }

                seat.flags = static_cast<MsgLobbyState::SeatEntry::ESeatFlags>(flags);
                seat.wins = slot.wins;
                setLobbySeatName(seat, slot.playerName);
            }

            return lobbyState;
        }

        void queueLobbyStateToPeer(ServerState& state, ENetPeer& peer, const uint8_t playerId, const MsgLobbyState& lobbyState)
        {
            const bool queued = queueReliableControl(&peer, makeLobbyStatePacket(lobbyState));
            state.diag.recordPacketSent(EMsgType::LobbyState,
                                        playerId,
                                        static_cast<uint8_t>(EChannel::ControlReliable),
                                        kPacketHeaderSize + kMsgLobbyStateSize,
                                        queued ? NetPacketResult::Ok : NetPacketResult::Dropped);

            if (!queued)
            {
                LOG_NET_CONN_WARN("Failed to queue LobbyState to playerId={} peer={}", playerId, peer.incomingPeerID);
            }
        }

        [[nodiscard]]
        bool finalizeAcceptedHello(PacketDispatchContext& ctx, const uint8_t playerId, const std::string_view playerName)
        {
            auto* session = getPeerSession(ctx.peer);
            if (session == nullptr)
            {
                LOG_NET_CONN_ERROR("Peer {} lost its live session before Hello accept finalization", ctx.peer->incomingPeerID);
                releasePlayerId(ctx.state, playerId);
                ctx.receiveResult = NetPacketResult::Rejected;
                enet_peer_reset(ctx.peer);
                return false;
            }

            acceptPeerSession(ctx.state, *session, playerId, playerName);

            recordPeerLifecycle(ctx.diag,
                                NetPeerLifecycleType::PlayerAccepted,
                                playerId,
                                static_cast<uint32_t>(ctx.peer->incomingPeerID));
            ctx.receiveResult = NetPacketResult::Ok;
            return true;
        }

        [[nodiscard]]
        PlayerSlot* requireAcceptedPlayerSlot(PacketDispatchContext& ctx)
        {
            auto* session = getPeerSession(ctx.peer);
            if (session == nullptr)
            {
                LOG_NET_CONN_ERROR("Control peer {} has no live peer session - ignoring", ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            if (!session->playerId.has_value())
            {
                LOG_NET_CONN_WARN("Control packet from non-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            const uint8_t playerId = session->playerId.value();
            auto& slotEntry = ctx.state.playerSlots[playerId];
            if (!slotEntry.has_value())
            {
                LOG_NET_CONN_ERROR("Control packet playerId={} has no active player metadata - ignoring", playerId);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            ctx.recordedPlayerId = playerId;
            return &slotEntry.value();
        }
    } // namespace

    void onHello(PacketDispatchContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t payloadSize)
    {
        if (hasAcceptedPlayer(ctx.peer))
        {
            LOG_NET_CONN_DEBUG("Duplicate Hello from already-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
            ctx.receiveResult = NetPacketResult::Rejected;
            ctx.recordedPlayerId = acceptedPlayerId(ctx.peer);
            return;
        }

        MsgHello msgHello{};
        if (!deserializeMsgHello(payload, payloadSize, msgHello))
        {
            LOG_NET_PROTO_WARN("Failed to parse Hello payload from peer {}", ctx.peer->incomingPeerID);
            ctx.receiveResult = NetPacketResult::Malformed;
            return;
        }

        if (msgHello.protocolVersion != kProtocolVersion)
        {
            LOG_NET_PROTO_ERROR("Protocol mismatch: peer {} sent version {}, expected {}",
                                ctx.peer->incomingPeerID,
                                msgHello.protocolVersion,
                                kProtocolVersion);
            ctx.receiveResult = NetPacketResult::Rejected;
            sendReject(ctx, MsgReject::EReason::VersionMismatch);
            return;
        }

        const std::string_view playerName(msgHello.name, boundedStrLen(msgHello.name, kPlayerNameMax));
        LOG_NET_CONN_DEBUG("Hello from \"{}\" (peer {})", playerName, ctx.peer->incomingPeerID);

        if (roundRequiresAdditionalBootstrap(ctx.state))
        {
            LOG_NET_CONN_DEBUG("Rejecting Hello from peer {} because the current round is not admitting new players (phase={})",
                               ctx.peer->incomingPeerID,
                               static_cast<int>(ctx.state.phase));
            ctx.receiveResult = NetPacketResult::Rejected;
            sendReject(ctx, MsgReject::EReason::GameInProgress);
            return;
        }

        const std::optional<uint8_t> reservedPlayerId = reserveHelloPlayerId(ctx);
        if (!reservedPlayerId.has_value())
        {
            return;
        }

        const uint8_t playerId = reservedPlayerId.value();
        if (!queueWelcomeForHello(ctx, playerId))
        {
            return;
        }

        if (!finalizeAcceptedHello(ctx, playerId, playerName))
        {
            return;
        }

        handleAcceptedPlayerJoined(ctx.state);
        LOG_NET_CONN_INFO("Accepted playerId={} into the lobby", playerId);
    }

    void onLobbyReady(PacketDispatchContext& ctx,
                      const PacketHeader& /*header*/,
                      const uint8_t* payload,
                      const std::size_t payloadSize)
    {
        if (ctx.state.phase != ServerPhase::Lobby && ctx.state.phase != ServerPhase::LobbyCountdown)
        {
            LOG_NET_CONN_DEBUG("LobbyReady received outside lobby phase from peer {} - ignoring",
                               ctx.peer->incomingPeerID);
            ctx.receiveResult = NetPacketResult::Rejected;
            return;
        }

        auto* slot = requireAcceptedPlayerSlot(ctx);
        if (slot == nullptr)
        {
            return;
        }

        MsgLobbyReady msgReady{};
        if (!deserializeMsgLobbyReady(payload, payloadSize, msgReady))
        {
            LOG_NET_PROTO_WARN("Failed to parse LobbyReady payload from peer {}", ctx.peer->incomingPeerID);
            ctx.receiveResult = NetPacketResult::Malformed;
            return;
        }

        const bool desiredReady = msgReady.ready != 0;
        if (slot->ready == desiredReady)
        {
            ctx.receiveResult = NetPacketResult::Ok;
            return;
        }

        slot->ready = desiredReady;
        LOG_NET_CONN_DEBUG("Lobby ready updated playerId={} name=\"{}\" ready={}",
                           slot->playerId,
                           slot->playerName,
                           desiredReady);
        handleLobbyReadyStateChanged(ctx.state);
        ctx.receiveResult = NetPacketResult::Ok;
    }

    void onMatchLoaded(PacketDispatchContext& ctx,
                       const PacketHeader& /*header*/,
                       const uint8_t* payload,
                       const std::size_t payloadSize)
    {
        if (ctx.state.phase != ServerPhase::StartingMatch)
        {
            LOG_NET_CONN_WARN("MatchLoaded received outside starting-match phase from peer {} - ignoring",
                              ctx.peer->incomingPeerID);
            ctx.receiveResult = NetPacketResult::Rejected;
            return;
        }

        auto* slot = requireAcceptedPlayerSlot(ctx);
        if (slot == nullptr)
        {
            return;
        }

        MsgMatchLoaded matchLoaded{};
        if (!deserializeMsgMatchLoaded(payload, payloadSize, matchLoaded))
        {
            LOG_NET_PROTO_WARN("Failed to parse MatchLoaded payload from peer {}", ctx.peer->incomingPeerID);
            ctx.receiveResult = NetPacketResult::Malformed;
            return;
        }

        const uint32_t participantBit = 1u << slot->playerId;
        if (matchLoaded.matchId != ctx.state.currentMatchId || (ctx.state.currentMatchPlayerMask & participantBit) == 0)
        {
            LOG_NET_CONN_WARN("MatchLoaded rejected playerId={} matchId={} currentMatchId={} participantMask=0x{:08x}",
                              slot->playerId,
                              matchLoaded.matchId,
                              ctx.state.currentMatchId,
                              ctx.state.currentMatchPlayerMask);
            ctx.receiveResult = NetPacketResult::Rejected;
            return;
        }

        markPlayerLoadedForCurrentMatch(ctx.state, slot->playerId);
        ctx.receiveResult = NetPacketResult::Ok;
    }

    void broadcastLobbyState(ServerState& state)
    {
        if (state.host == nullptr)
        {
            return;
        }

        const MsgLobbyState lobbyState = buildLobbyState(state);

        for (const auto& sessionEntry : state.peerSessions)
        {
            if (!sessionEntry.has_value() || !sessionEntry->playerId.has_value() || sessionEntry->peer == nullptr)
            {
                continue;
            }

            queueLobbyStateToPeer(state, *sessionEntry->peer, sessionEntry->playerId.value(), lobbyState);
        }

        flush(state.host);
    }
} // namespace bomberman::server
