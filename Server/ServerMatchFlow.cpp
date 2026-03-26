/**
 * @file ServerMatchFlow.cpp
 * @brief Authoritative in-match result and return-to-lobby helpers.
 */

#include "ServerFlow.h"

#include "ServerFlowInternal.h"

#include "Net/NetSend.h"
#include "ServerBombs.h"
#include "Util/Log.h"

namespace bomberman::server
{
    using namespace flow_internal;

    namespace
    {
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
    } // namespace

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
        clearBombsAndReleaseOwnership(state);

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
