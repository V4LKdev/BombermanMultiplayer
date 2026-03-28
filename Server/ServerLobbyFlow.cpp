/**
 * @file ServerLobbyFlow.cpp
 * @brief Authoritative lobby, countdown, bootstrap, and match-start helpers.
 */

#include "ServerFlow.h"

#include <bit>

#include "ServerFlowInternal.h"

#include "Net/NetSend.h"
#include "ServerBombs.h"
#include "ServerHandlers.h"
#include "ServerPowerups.h"
#include "Sim/TileMapGen.h"
#include "Util/Log.h"

namespace bomberman::server::flow_internal
{
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

    void sendMatchCancelledToParticipants(ServerState& state, const uint32_t matchId, const uint32_t playerMask)
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

    void cancelLobbyCountdown(ServerState& state, const std::string_view reason)
    {
        state.phase = ServerPhase::Lobby;
        state.currentLobbyCountdownPlayerMask = 0;
        state.currentLobbyCountdownDeadlineTick = 0;
        state.currentLobbyCountdownLastBroadcastSecond = 0;
        broadcastLobbyState(state);
        refreshServerFlowDiagnostics(state);
        LOG_SERVER_INFO("Lobby countdown cancelled ({})", reason);
    }

    void startLobbyCountdown(ServerState& state, const uint32_t participantMask)
    {
        state.phase = ServerPhase::LobbyCountdown;
        state.currentLobbyCountdownPlayerMask = participantMask;
        state.currentLobbyCountdownDeadlineTick = state.serverTick + kLobbyCountdownTicks;
        state.currentLobbyCountdownLastBroadcastSecond = computeCountdownSecondsRemaining(state);
        broadcastLobbyState(state);
        refreshServerFlowDiagnostics(state);
        LOG_SERVER_INFO("Lobby countdown started players={} seconds={}",
                        std::popcount(participantMask),
                        static_cast<unsigned int>(state.currentLobbyCountdownLastBroadcastSecond));
    }

    void resetLobbyParticipantsToUnready(ServerState& state, const std::string_view reason)
    {
        const bool countdownWasActive = state.phase == ServerPhase::LobbyCountdown;

        state.phase = ServerPhase::Lobby;
        state.currentLobbyCountdownPlayerMask = 0;
        state.currentLobbyCountdownDeadlineTick = 0;
        state.currentLobbyCountdownLastBroadcastSecond = 0;
        clearAllLobbyReadyFlags(state);
        broadcastLobbyState(state);
        refreshServerFlowDiagnostics(state);

        if (countdownWasActive)
        {
            LOG_SERVER_INFO("Lobby countdown cancelled ({}); ready state reset", reason);
            return;
        }

        LOG_SERVER_INFO("Lobby participant set changed ({}); ready state reset", reason);
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
        for (auto& matchEntry : state.matchPlayers)
        {
            if (!matchEntry.has_value())
                continue;

            matchEntry->inputTimelineStarted = true;
            // The first authoritative consume deadline is offset from gameplay unlock by the configured input lead.
            matchEntry->nextConsumeServerTick = matchStart.unlockServerTick + state.inputLeadTicks;
        }

        net::flush(state.host);
        refreshServerFlowDiagnostics(state);
        LOG_SERVER_INFO("Match start committed matchId={} players={} goTick={} unlockTick={}",
                        state.currentMatchId,
                        std::popcount(state.currentMatchPlayerMask),
                        state.currentMatchGoShowTick,
                        state.currentMatchUnlockTick);
    }
} // namespace bomberman::server::flow_internal

namespace bomberman::server
{
    using namespace flow_internal;

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
        refreshServerFlowDiagnostics(state);

        clearAllLobbyReadyFlags(state);
        clearBombsAndReleaseOwnership(state);
        clearRoundPowerups(state);
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

    void handleAcceptedPlayerJoined(ServerState& state)
    {
        if (state.phase != ServerPhase::Lobby && state.phase != ServerPhase::LobbyCountdown)
        {
            return;
        }

        resetLobbyParticipantsToUnready(state, "a participant joined");
        refreshServerFlowDiagnostics(state);
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
        clearBombsAndReleaseOwnership(state);
        clearRoundPowerups(state);
        clearActiveMatchPlayers(state);
        sim::generateTileMap(state.mapSeed, state.tiles);
        placeRoundPowerups(state);

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
        refreshServerFlowDiagnostics(state);
        LOG_SERVER_INFO("Starting match bootstrap matchId={} players={} seed={}",
                        state.currentMatchId,
                        std::popcount(state.currentMatchPlayerMask),
                        state.mapSeed);
        return true;
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

        if (state.phase == ServerPhase::Lobby || state.phase == ServerPhase::LobbyCountdown)
        {
            resetLobbyParticipantsToUnready(state, "a participant disconnected");
            refreshServerFlowDiagnostics(state);
            return;
        }

        if (state.phase == ServerPhase::StartingMatch)
        {
            cancelStartingMatch(state,
                                state.currentMatchPlayerMask,
                                "a participant disconnected before match start");
            refreshServerFlowDiagnostics(state);
            return;
        }

        if (state.phase != ServerPhase::Lobby && state.currentMatchPlayerMask == 0)
        {
            resetRoundRuntimeToLobby(state);
            refreshServerFlowDiagnostics(state);
            LOG_SERVER_INFO("Round state reset to lobby after the last player disconnected");
            return;
        }

        if (state.phase == ServerPhase::InMatch)
        {
            uint8_t activePlayerCount = 0;
            uint8_t alivePlayerCount = 0;
            std::optional<uint8_t> survivingPlayerId{};

            for (const auto& matchEntry : state.matchPlayers)
            {
                if (!matchEntry.has_value())
                    continue;

                ++activePlayerCount;
                if (!matchEntry->alive)
                    continue;

                ++alivePlayerCount;
                survivingPlayerId = matchEntry->playerId;
            }

            // Mid-match reconnect is unsupported, so once a disconnect leaves the remaining round state decided,
            // end the round immediately instead of letting one more simulation tick mutate gameplay world state.
            if (activePlayerCount > 0 && alivePlayerCount <= 1)
            {
                beginEndOfMatch(state,
                                (alivePlayerCount == 1) ? survivingPlayerId : std::nullopt,
                                alivePlayerCount == 0,
                                activePlayerCount,
                                alivePlayerCount);
            }
        }
    }
} // namespace bomberman::server
