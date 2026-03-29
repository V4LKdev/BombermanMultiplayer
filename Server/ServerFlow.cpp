/**
 * @file ServerFlow.cpp
 * @ingroup authoritative_server
 * @brief Authoritative server-flow phase orchestration.
 */

#include "ServerFlow.h"

#include "ServerFlowInternal.h"

#include "ServerHandlers.h"
#include "Util/Log.h"

namespace bomberman::server
{
    using namespace flow_internal;

    namespace
    {
        void unlockMatchInputs(ServerState& state)
        {
            for (auto& matchEntry : state.matchPlayers)
            {
                if (matchEntry.has_value())
                {
                    matchEntry->inputLocked = false;
                }
            }
        }
    } // namespace

    void refreshServerFlowDiagnostics(ServerState& state)
    {
        state.diag.recordServerFlowState(coarseServerFlowState(state.phase),
                                         isServerIdleForDiagnostics(state),
                                         state.serverTick,
                                         state.currentMatchId);
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
            unlockMatchInputs(state);
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
} // namespace bomberman::server
