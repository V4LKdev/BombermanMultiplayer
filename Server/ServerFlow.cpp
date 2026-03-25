/**
 * @file ServerFlow.cpp
 * @brief Authoritative lobby-to-match flow helpers.
 */

#include "ServerFlow.h"

#include <bit>

#include "Net/NetSend.h"
#include "ServerHandlers.h"
#include "ServerState.h"
#include "Sim/SimConfig.h"
#include "Sim/TileMapGen.h"
#include "Util/Log.h"

namespace bomberman::server
{
    namespace
    {
        static_assert(net::kMaxPlayers <= 32, "Current match-player mask assumes at most 32 player ids");

        constexpr uint32_t kLobbyCountdownTicks = static_cast<uint32_t>(sim::kTickRate) * 3u;
        constexpr uint32_t kMatchStartLoadTimeoutTicks = static_cast<uint32_t>(sim::kTickRate) * 5u;
        constexpr uint32_t kMatchStartGoDelayTicks = static_cast<uint32_t>(sim::kTickRate);
        constexpr uint32_t kMatchStartUnlockDelayTicks = kMatchStartGoDelayTicks;
        constexpr uint32_t kEndOfMatchReturnTicks = static_cast<uint32_t>(sim::kTickRate) * 3u;

        [[nodiscard]]
        constexpr uint32_t playerMaskBit(const uint8_t playerId)
        {
            return 1u << playerId;
        }

        [[nodiscard]]
        bool hasMinimumParticipants(const uint32_t playerMask)
        {
            return std::popcount(playerMask) >= 2;
        }

        [[nodiscard]]
        uint8_t computeCountdownSecondsRemaining(const ServerState& state)
        {
            if (state.phase != ServerPhase::LobbyCountdown ||
                state.currentLobbyCountdownDeadlineTick == 0 ||
                state.serverTick >= state.currentLobbyCountdownDeadlineTick)
            {
                return 0;
            }

            const uint32_t remainingTicks = state.currentLobbyCountdownDeadlineTick - state.serverTick;
            return static_cast<uint8_t>((remainingTicks + static_cast<uint32_t>(sim::kTickRate) - 1u) /
                                        static_cast<uint32_t>(sim::kTickRate));
        }

        void clearActiveBombs(ServerState& state)
        {
            for (auto& bombEntry : state.bombs)
            {
                bombEntry.reset();
            }
        }

        void clearActiveMatchPlayers(ServerState& state)
        {
            for (auto& matchEntry : state.matchPlayers)
            {
                matchEntry.reset();
            }
        }

        void clearAllLobbyReadyFlags(ServerState& state)
        {
            for (auto& slotEntry : state.playerSlots)
            {
                if (slotEntry.has_value())
                {
                    slotEntry->ready = false;
                }
            }
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
        uint32_t collectAcceptedReadyParticipants(const ServerState& state, bool& allReady)
        {
            uint32_t playerMask = 0;
            allReady = true;

            for (const auto& sessionEntry : state.peerSessions)
            {
                if (!sessionEntry.has_value() ||
                    !sessionEntry->playerId.has_value() ||
                    sessionEntry->peer == nullptr)
                {
                    continue;
                }

                const uint8_t playerId = sessionEntry->playerId.value();
                const auto& slotEntry = state.playerSlots[playerId];
                if (!slotEntry.has_value())
                {
                    allReady = false;
                    return 0;
                }

                playerMask |= playerMaskBit(playerId);
                if (!slotEntry->ready)
                {
                    allReady = false;
                }
            }

            return playerMask;
        }

        [[nodiscard]]
        std::string_view winnerDisplayName(const ServerState& state, const uint8_t playerId)
        {
            const auto& slotEntry = state.playerSlots[playerId];
            if (!slotEntry.has_value() || slotEntry->playerName.empty())
            {
                return "Player";
            }

            return slotEntry->playerName;
        }

        void sendMatchCancelledToParticipants(ServerState& state, uint32_t matchId, uint32_t playerMask)
        {
            if (matchId == 0 || playerMask == 0)
            {
                return;
            }

            net::MsgMatchCancelled cancelled{};
            cancelled.matchId = matchId;

            for (uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
            {
                if ((playerMask & playerMaskBit(playerId)) == 0)
                {
                    continue;
                }

                if (!queueReliableControlToPlayer(state,
                                                  playerId,
                                                  net::EMsgType::MatchCancelled,
                                                  net::kMsgMatchCancelledSize,
                                                  net::makeMatchCancelledPacket(cancelled)))
                {
                    LOG_NET_CONN_WARN("Failed to queue MatchCancelled to playerId={} matchId={}",
                                      playerId,
                                      matchId);
                }
            }

            net::flush(state.host);
        }

        void sendMatchResultToParticipants(ServerState& state)
        {
            if (state.currentMatchId == 0 || state.currentMatchPlayerMask == 0)
            {
                return;
            }

            net::MsgMatchResult matchResult{};
            matchResult.matchId = state.currentMatchId;

            if (state.roundEndedInDraw)
            {
                matchResult.result = net::MsgMatchResult::EResult::Draw;
            }
            else if (state.roundWinnerPlayerId.has_value())
            {
                matchResult.result = net::MsgMatchResult::EResult::Win;
                matchResult.winnerPlayerId = state.roundWinnerPlayerId.value();
                net::setMatchResultWinnerName(matchResult, winnerDisplayName(state, matchResult.winnerPlayerId));
            }

            for (uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
            {
                if ((state.currentMatchPlayerMask & playerMaskBit(playerId)) == 0)
                {
                    continue;
                }

                if (!queueReliableControlToPlayer(state,
                                                  playerId,
                                                  net::EMsgType::MatchResult,
                                                  net::kMsgMatchResultSize,
                                                  net::makeMatchResultPacket(matchResult)))
                {
                    LOG_NET_CONN_WARN("Failed to queue MatchResult to playerId={} matchId={}",
                                      playerId,
                                      matchResult.matchId);
                }
            }

            net::flush(state.host);
        }

        void cancelLobbyCountdown(ServerState& state, const std::string_view reason)
        {
            state.phase = ServerPhase::Lobby;
            state.currentLobbyCountdownPlayerMask = 0;
            state.currentLobbyCountdownDeadlineTick = 0;
            state.currentLobbyCountdownLastBroadcastSecond = 0;
            broadcastLobbyState(state);
            LOG_SERVER_INFO("Lobby countdown cancelled ({})", reason);
        }

        void startLobbyCountdown(ServerState& state, const uint32_t participantMask)
        {
            state.phase = ServerPhase::LobbyCountdown;
            state.currentLobbyCountdownPlayerMask = participantMask;
            state.currentLobbyCountdownDeadlineTick = state.serverTick + kLobbyCountdownTicks;
            state.currentLobbyCountdownLastBroadcastSecond = computeCountdownSecondsRemaining(state);
            broadcastLobbyState(state);
            LOG_SERVER_INFO("Lobby countdown started players={} seconds={}",
                            std::popcount(participantMask),
                            static_cast<unsigned int>(state.currentLobbyCountdownLastBroadcastSecond));
        }

        void cancelStartingMatch(ServerState& state, const uint32_t notifyMask, const std::string_view reason)
        {
            const uint32_t cancelledMatchId = state.currentMatchId;
            sendMatchCancelledToParticipants(state, cancelledMatchId, notifyMask);
            resetRoundRuntimeToLobby(state);
            broadcastLobbyState(state);
            LOG_SERVER_WARN("Match bootstrap cancelled matchId={} ({})", cancelledMatchId, reason);
        }

        void maybeCommitMatchStart(ServerState& state)
        {
            if (state.phase != ServerPhase::StartingMatch ||
                state.currentMatchId == 0 ||
                state.currentMatchPlayerMask == 0 ||
                state.currentMatchLoadedMask != state.currentMatchPlayerMask)
            {
                return;
            }

            net::MsgMatchStart matchStart{};
            matchStart.matchId = state.currentMatchId;
            matchStart.goShowServerTick = state.serverTick + kMatchStartGoDelayTicks;
            matchStart.unlockServerTick = state.serverTick + kMatchStartUnlockDelayTicks;
            bool matchStartSendFailed = false;

            for (uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
            {
                if ((state.currentMatchPlayerMask & playerMaskBit(playerId)) == 0)
                {
                    continue;
                }

                if (!queueReliableControlToPlayer(state,
                                                  playerId,
                                                  net::EMsgType::MatchStart,
                                                  net::kMsgMatchStartSize,
                                                  net::makeMatchStartPacket(matchStart)))
                {
                    matchStartSendFailed = true;
                    LOG_NET_CONN_WARN("Failed to queue MatchStart to playerId={} matchId={}",
                                      playerId,
                                      state.currentMatchId);
                }
            }

            if (matchStartSendFailed)
            {
                cancelStartingMatch(state,
                                    state.currentMatchPlayerMask,
                                    "failed to queue MatchStart to all participants");
                return;
            }

            state.phase = ServerPhase::InMatch;
            state.currentMatchStartDeadlineTick = 0;
            state.currentMatchGoShowTick = matchStart.goShowServerTick;
            state.currentMatchUnlockTick = matchStart.unlockServerTick;
            net::flush(state.host);
            LOG_SERVER_INFO("Match start committed matchId={} players={} goTick={} unlockTick={}",
                            state.currentMatchId,
                            std::popcount(state.currentMatchPlayerMask),
                            state.currentMatchGoShowTick,
                            state.currentMatchUnlockTick);
        }
    } // namespace

    void resetRoundRuntimeToLobby(ServerState& state)
    {
        state.phase = ServerPhase::Lobby;
        state.currentMatchId = 0;
        state.currentLobbyCountdownPlayerMask = 0;
        state.currentLobbyCountdownDeadlineTick = 0;
        state.currentLobbyCountdownLastBroadcastSecond = 0;
        state.currentMatchPlayerMask = 0;
        state.currentMatchLoadedMask = 0;
        state.currentMatchStartDeadlineTick = 0;
        state.currentMatchGoShowTick = 0;
        state.currentMatchUnlockTick = 0;
        state.currentEndOfMatchReturnTick = 0;
        state.roundWinnerPlayerId.reset();
        state.roundEndedInDraw = false;

        clearAllLobbyReadyFlags(state);
        clearActiveBombs(state);
        clearActiveMatchPlayers(state);
        rollNextRoundMapSeed(state);
    }

    void handleLobbyReadyStateChanged(ServerState& state)
    {
        bool allReady = false;
        const uint32_t participantMask = collectAcceptedReadyParticipants(state, allReady);

        if (state.phase == ServerPhase::LobbyCountdown)
        {
            if (!allReady ||
                participantMask != state.currentLobbyCountdownPlayerMask ||
                !hasMinimumParticipants(participantMask))
            {
                cancelLobbyCountdown(state, "a participant unreadied before countdown completion");
                return;
            }

            broadcastLobbyState(state);
            return;
        }

        if (state.phase != ServerPhase::Lobby)
        {
            return;
        }

        if (allReady && hasMinimumParticipants(participantMask))
        {
            startLobbyCountdown(state, participantMask);
            return;
        }

        broadcastLobbyState(state);
    }

    bool beginMatchBootstrap(ServerState& state)
    {
        if (state.phase != ServerPhase::LobbyCountdown || state.currentLobbyCountdownPlayerMask == 0)
        {
            return false;
        }

        bool allReady = false;
        const uint32_t participantMask = collectAcceptedReadyParticipants(state, allReady);
        if (!allReady ||
            participantMask != state.currentLobbyCountdownPlayerMask ||
            !hasMinimumParticipants(participantMask))
        {
            cancelLobbyCountdown(state, "countdown commit conditions were no longer valid");
            return false;
        }

        state.phase = ServerPhase::StartingMatch;
        state.currentMatchId = state.nextMatchId++;
        state.currentLobbyCountdownPlayerMask = 0;
        state.currentLobbyCountdownDeadlineTick = 0;
        state.currentLobbyCountdownLastBroadcastSecond = 0;
        state.currentMatchPlayerMask = participantMask;
        state.currentMatchLoadedMask = 0;
        state.currentMatchStartDeadlineTick = state.serverTick + kMatchStartLoadTimeoutTicks;
        state.currentMatchGoShowTick = 0;
        state.currentMatchUnlockTick = 0;
        state.currentEndOfMatchReturnTick = 0;
        state.roundWinnerPlayerId.reset();
        state.roundEndedInDraw = false;

        clearAllLobbyReadyFlags(state);
        clearActiveBombs(state);
        clearActiveMatchPlayers(state);
        sim::generateTileMap(state.mapSeed, state.tiles);

        net::MsgLevelInfo levelInfo{};
        levelInfo.matchId = state.currentMatchId;
        levelInfo.mapSeed = state.mapSeed;

        uint32_t bootstrappedMask = 0;
        bool bootstrapSendFailed = false;

        for (uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
        {
            if ((participantMask & playerMaskBit(playerId)) == 0)
            {
                continue;
            }

            createMatchPlayerState(state, playerId);
            if (queueReliableControlToPlayer(state,
                                             playerId,
                                             net::EMsgType::LevelInfo,
                                             net::kMsgLevelInfoSize,
                                             net::makeLevelInfoPacket(levelInfo)))
            {
                bootstrappedMask |= playerMaskBit(playerId);
                continue;
            }

            bootstrapSendFailed = true;
            destroyMatchPlayerState(state, playerId);
            LOG_NET_CONN_WARN("Failed to queue LevelInfo to playerId={} matchId={}",
                              playerId,
                              state.currentMatchId);
        }

        if (bootstrapSendFailed)
        {
            cancelStartingMatch(state, bootstrappedMask, "failed to queue LevelInfo to all participants");
            return false;
        }

        state.currentMatchPlayerMask = bootstrappedMask;
        net::flush(state.host);
        LOG_SERVER_INFO("Starting match bootstrap matchId={} players={} seed={}",
                        state.currentMatchId,
                        std::popcount(state.currentMatchPlayerMask),
                        state.mapSeed);
        return true;
    }

    void advanceServerFlow(ServerState& state)
    {
        if (state.phase == ServerPhase::LobbyCountdown &&
            state.currentLobbyCountdownDeadlineTick != 0)
        {
            if (state.serverTick >= state.currentLobbyCountdownDeadlineTick)
            {
                beginMatchBootstrap(state);
                return;
            }

            const uint8_t remainingSeconds = computeCountdownSecondsRemaining(state);
            if (remainingSeconds != state.currentLobbyCountdownLastBroadcastSecond)
            {
                state.currentLobbyCountdownLastBroadcastSecond = remainingSeconds;
                broadcastLobbyState(state);
            }

            return;
        }

        if (state.phase == ServerPhase::StartingMatch &&
            state.currentMatchId != 0 &&
            state.currentMatchStartDeadlineTick != 0 &&
            state.serverTick >= state.currentMatchStartDeadlineTick)
        {
            cancelStartingMatch(state,
                                state.currentMatchPlayerMask,
                                "timed out waiting for match-loaded acknowledgements");
            return;
        }

        if (state.phase == ServerPhase::InMatch &&
            state.currentMatchUnlockTick != 0 &&
            state.serverTick >= state.currentMatchUnlockTick)
        {
            for (auto& matchEntry : state.matchPlayers)
            {
                if (matchEntry.has_value())
                {
                    matchEntry->inputLocked = false;
                }
            }

            LOG_SERVER_INFO("Gameplay unlocked matchId={} tick={}", state.currentMatchId, state.serverTick);
            state.currentMatchUnlockTick = 0;
            return;
        }

        if (state.phase != ServerPhase::EndOfMatch ||
            state.currentMatchId == 0 ||
            state.currentEndOfMatchReturnTick == 0 ||
            state.serverTick < state.currentEndOfMatchReturnTick)
        {
            return;
        }

        resetRoundRuntimeToLobby(state);
        broadcastLobbyState(state);
        LOG_SERVER_INFO("Returned to lobby after end-of-match cooldown");
    }

    void markPlayerLoadedForCurrentMatch(ServerState& state, const uint8_t playerId)
    {
        if (state.phase != ServerPhase::StartingMatch ||
            state.currentMatchId == 0 ||
            (state.currentMatchPlayerMask & playerMaskBit(playerId)) == 0)
        {
            return;
        }

        const uint32_t playerBit = playerMaskBit(playerId);
        if ((state.currentMatchLoadedMask & playerBit) != 0)
        {
            return;
        }

        state.currentMatchLoadedMask |= playerBit;
        LOG_NET_CONN_DEBUG("Match loaded acknowledged playerId={} matchId={} loaded={}/{}",
                           playerId,
                           state.currentMatchId,
                           std::popcount(state.currentMatchLoadedMask),
                           std::popcount(state.currentMatchPlayerMask));
        maybeCommitMatchStart(state);
    }

    void handleAcceptedPlayerReleased(ServerState& state, const uint8_t playerId)
    {
        const uint32_t releasedBit = playerMaskBit(playerId);
        state.currentLobbyCountdownPlayerMask &= ~releasedBit;
        state.currentMatchPlayerMask &= ~releasedBit;
        state.currentMatchLoadedMask &= ~releasedBit;

        if (state.phase == ServerPhase::LobbyCountdown)
        {
            cancelLobbyCountdown(state, "a participant disconnected before countdown completion");
            return;
        }

        if (state.phase == ServerPhase::StartingMatch)
        {
            cancelStartingMatch(state,
                                state.currentMatchPlayerMask,
                                "a participant disconnected before match start");
            return;
        }

        if (state.phase != ServerPhase::Lobby && state.currentMatchPlayerMask == 0)
        {
            resetRoundRuntimeToLobby(state);
            LOG_SERVER_INFO("Round state reset to lobby after the last player disconnected");
        }
    }

    void beginEndOfMatch(ServerState& state,
                         const std::optional<uint8_t> winnerPlayerId,
                         const bool draw,
                         const uint8_t activePlayerCount,
                         const uint8_t alivePlayerCount)
    {
        if (state.phase != ServerPhase::InMatch)
        {
            return;
        }

        state.phase = ServerPhase::EndOfMatch;
        state.roundWinnerPlayerId = draw ? std::nullopt : winnerPlayerId;
        state.roundEndedInDraw = draw;
        state.currentMatchGoShowTick = 0;
        state.currentMatchUnlockTick = 0;
        state.currentEndOfMatchReturnTick = state.serverTick + kEndOfMatchReturnTicks;
        state.diag.recordRoundEnded(state.roundWinnerPlayerId, state.roundEndedInDraw, state.serverTick);

        if (state.roundWinnerPlayerId.has_value())
        {
            auto& winnerSlotEntry = state.playerSlots[state.roundWinnerPlayerId.value()];
            if (winnerSlotEntry.has_value())
            {
                ++winnerSlotEntry->wins;
            }
        }

        for (auto& matchEntry : state.matchPlayers)
        {
            if (matchEntry.has_value())
            {
                matchEntry->inputLocked = true;
            }
        }

        sendMatchResultToParticipants(state);

        if (state.roundEndedInDraw)
        {
            LOG_SERVER_INFO("Round ended tick={} result=draw activePlayers={} alivePlayers={}",
                            state.serverTick,
                            static_cast<int>(activePlayerCount),
                            static_cast<int>(alivePlayerCount));
            return;
        }

        LOG_SERVER_INFO("Round ended tick={} winnerPlayerId={} activePlayers={} alivePlayers={}",
                        state.serverTick,
                        static_cast<int>(state.roundWinnerPlayerId.value()),
                        static_cast<int>(activePlayerCount),
                        static_cast<int>(alivePlayerCount));
    }
} // namespace bomberman::server
