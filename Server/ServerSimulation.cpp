/**
 * @file ServerSimulation.cpp
 * @brief Authoritative fixed-tick simulation, corrections, and snapshot broadcast.
 */

#include "ServerState.h"

#include <algorithm>
#include <climits>

#include "Net/NetSend.h"
#include "ServerBombs.h"
#include "ServerFlow.h"
#include "ServerPowerups.h"
#include "ServerSnapshot.h"
#include "Sim/SimConfig.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    // =================================================================================================================
    // ===== Internal Helpers ==========================================================================================
    // =================================================================================================================

    namespace
    {
        /** @brief Builds the owner correction that corresponds to the current authoritative tick. */
        [[nodiscard]]
        MsgCorrection buildCorrection(const ServerState& state, const MatchPlayerState& matchPlayer)
        {
            MsgCorrection corr{};
            corr.matchId = state.currentMatchId;
            corr.serverTick = state.serverTick;
            corr.lastProcessedInputSeq = matchPlayer.lastProcessedInputSeq;
            corr.xQ = static_cast<int16_t>(std::clamp(matchPlayer.pos.xQ, INT16_MIN, INT16_MAX));
            corr.yQ = static_cast<int16_t>(std::clamp(matchPlayer.pos.yQ, INT16_MIN, INT16_MAX));
            corr.playerFlags = buildReplicatedPlayerFlags(matchPlayer, state.serverTick);
            return corr;
        }

        /**
         * @brief Resolves which input bitmask the server should simulate for this match player on the current tick.
         *
         * Arms the fixed-delay consume timeline on first input, consumes the
         * exact next sequence when it reaches its deadline, or permanently gaps
         * forward using the last authoritative buttons when the deadline is
         * missed.
         */
        void resolveMatchPlayerInputForTick(ServerState& state, MatchPlayerState& matchPlayer)
        {
            matchPlayer.appliedButtons = 0;

            if (matchPlayer.lastReceivedInputSeq == 0)
            {
                // No input ever received from this match player - idle.
                return;
            }

            if (!matchPlayer.inputTimelineStarted)
            {
                matchPlayer.inputTimelineStarted = true;
                matchPlayer.nextConsumeServerTick = state.serverTick + state.inputLeadTicks;
            }

            if (state.serverTick < matchPlayer.nextConsumeServerTick)
                return;

            const uint32_t nextSeq = matchPlayer.lastProcessedInputSeq + 1u;
            const std::size_t seqSlot = nextSeq % kServerInputBufferSize;
            auto& entry = matchPlayer.inputRing[seqSlot];
            if (entry.valid && entry.seq == nextSeq)
            {
                // Exact match at the scheduled deadline - consume this command.
                matchPlayer.appliedButtons = entry.buttons;
                matchPlayer.lastAppliedButtons = matchPlayer.appliedButtons;
                matchPlayer.lastProcessedInputSeq = nextSeq;
                matchPlayer.consecutiveInputGaps = 0;

                if (entry.seenBuffered && !entry.seenDirect)
                {
                    state.diag.recordBufferedDeadlineRecovery(matchPlayer.playerId, nextSeq, state.serverTick);
                }
                else if (entry.seenDirect)
                {
                    state.diag.recordDirectDeadlineConsume(matchPlayer.playerId, nextSeq);
                }

                entry.valid = false;
                entry.seenDirect = false;
                entry.seenBuffered = false;
                ++matchPlayer.nextConsumeServerTick;
                return;
            }

            // The consume deadline was reached and the exact sequence is still missing.
            // Fall back to the previous buttons and advance permanently.
            matchPlayer.appliedButtons = matchPlayer.lastAppliedButtons;
            matchPlayer.lastProcessedInputSeq = nextSeq;
            ++matchPlayer.consecutiveInputGaps;
            ++matchPlayer.nextConsumeServerTick;

            state.diag.recordSimulationGap(matchPlayer.playerId,
                                           nextSeq,
                                           matchPlayer.lastAppliedButtons,
                                           state.serverTick);

            if (matchPlayer.consecutiveInputGaps >= kRepeatedInputWarnThreshold
                && state.serverTick >= matchPlayer.nextGapWarnTick)
            {
                LOG_NET_INPUT_WARN(
                    "Repeated input gaps playerId={} streak={} expectedSeq={} slotValid={} slotSeq={} using lastAppliedButtons=0x{:02x} lastRecv={} lastProcessed={}",
                    matchPlayer.playerId, matchPlayer.consecutiveInputGaps,
                    nextSeq,
                    entry.valid ? 1 : 0, entry.seq,
                    matchPlayer.lastAppliedButtons,
                    matchPlayer.lastReceivedInputSeq, matchPlayer.lastProcessedInputSeq);

                matchPlayer.nextGapWarnTick = state.serverTick + kRepeatedInputWarnCooldownTicks;
            }
        }

        /** @brief Queues the owning match player's authoritative correction for the current server tick. */
        void queueMatchPlayerCorrection(ServerState& state, const MatchPlayerState& matchPlayer)
        {
            bool correctionQueued = false;
            if (const auto* session = findPeerSessionByPlayerId(state, matchPlayer.playerId);
                session != nullptr && session->peer != nullptr)
            {
                const auto correction = buildCorrection(state, matchPlayer);
                const auto correctionBytes = makeCorrectionPacket(correction);
                correctionQueued = queueUnreliableCorrection(session->peer, correctionBytes);
            }

            state.diag.recordPacketSent(EMsgType::Correction,
                                        matchPlayer.playerId,
                                        static_cast<uint8_t>(EChannel::CorrectionUnreliable),
                                        kPacketHeaderSize + kMsgCorrectionSize,
                                        correctionQueued ? NetPacketResult::Ok : NetPacketResult::Dropped);
        }

        /** @brief Samples ENet transport health for one accepted match player when the sampling cadence is due. */
        void samplePeerTransport(ServerState& state, const MatchPlayerState& matchPlayer)
        {
            const auto* session = findPeerSessionByPlayerId(state, matchPlayer.playerId);
            if (state.serverTick % kPeerTransportSampleTicks != 0 || session == nullptr || session->peer == nullptr)
            {
                return;
            }

            const auto queuedReliable = session->peer->reliableDataInTransit;
            const uint32_t totalWaiting = static_cast<uint32_t>(session->peer->totalWaitingData);
            const uint32_t queuedUnreliable = totalWaiting > queuedReliable
                ? totalWaiting - queuedReliable
                : 0;
            const uint32_t packetLossPermille = static_cast<uint32_t>(
                static_cast<uint64_t>(session->peer->packetLoss) * 1000u / ENET_PEER_PACKET_LOSS_SCALE);

            state.diag.samplePeerTransport(matchPlayer.playerId,
                                  session->peer->roundTripTime,
                                  session->peer->roundTripTimeVariance,
                                  packetLossPermille,
                                  queuedReliable,
                                  queuedUnreliable);
        }

        /** @brief Advances one accepted match player through authoritative input, movement, correction, and diagnostics. */
        void simulateAcceptedMatchPlayer(ServerState& state, MatchPlayerState& matchPlayer)
        {
            refreshMatchPlayerPowerupLoadout(state, matchPlayer);

            if (!matchPlayer.alive || matchPlayer.inputLocked)
            {
                matchPlayer.appliedButtons = 0;
                matchPlayer.lastAppliedButtons = 0;
                matchPlayer.previousTickButtons = 0;
                state.diag.samplePeerInputContinuity(matchPlayer.playerId,
                                                     matchPlayer.lastReceivedInputSeq,
                                                     matchPlayer.lastProcessedInputSeq);
                queueMatchPlayerCorrection(state, matchPlayer);
                samplePeerTransport(state, matchPlayer);
                return;
            }

            resolveMatchPlayerInputForTick(state, matchPlayer);
            state.diag.samplePeerInputContinuity(matchPlayer.playerId,
                                                 matchPlayer.lastReceivedInputSeq,
                                                 matchPlayer.lastProcessedInputSeq);

            tryPlaceBomb(state, matchPlayer);

            const int8_t moveX = buttonsToMoveX(matchPlayer.appliedButtons);
            const int8_t moveY = buttonsToMoveY(matchPlayer.appliedButtons);
            matchPlayer.pos = sim::stepMovementWithCollision(
                matchPlayer.pos,
                moveX,
                moveY,
                state.tiles,
                hasSpeedBoost(matchPlayer, state.serverTick) ? sim::kSpeedBoostPlayerSpeedQ : sim::kPlayerSpeedQ);
            matchPlayer.previousTickButtons = matchPlayer.appliedButtons;

            queueMatchPlayerCorrection(state, matchPlayer);
            samplePeerTransport(state, matchPlayer);
        }

        /** @brief Broadcasts the current authoritative snapshot when this tick is configured to do so. */
        void broadcastSnapshotIfDue(ServerState& state)
        {
            if (!shouldBroadcastSnapshot(state))
            {
                return;
            }

            const auto snapshot = buildSnapshot(state);
            const auto snapshotBytes = makeSnapshotPacket(snapshot);
            if (state.serverTick % kServerSnapshotLogIntervalTicks == 0)
            {
                LOG_NET_SNAPSHOT_DEBUG("Snapshot tick={} playerCount={}", snapshot.serverTick, snapshot.playerCount);
            }

            for (const auto& slot : state.matchPlayers)
            {
                if (!slot.has_value())
                {
                    continue;
                }

                const auto* session = findPeerSessionByPlayerId(state, slot->playerId);
                if (session == nullptr || session->peer == nullptr)
                    continue;

                const bool queued = queueUnreliableSnapshot(session->peer, snapshotBytes);
                state.diag.recordPacketSent(EMsgType::Snapshot,
                                            slot->playerId,
                                            static_cast<uint8_t>(EChannel::SnapshotUnreliable),
                                            kPacketHeaderSize + kMsgSnapshotSize,
                                            queued ? NetPacketResult::Ok : NetPacketResult::Dropped);
            }

            flush(state.host);
        }

        /** @brief Evaluates whether the current authoritative round has reached an end state. */
        void evaluateRoundEnd(ServerState& state)
        {
            if (state.phase != ServerPhase::InMatch)
                return;

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

            if (activePlayerCount == 0 || alivePlayerCount > 1)
                return;

            beginEndOfMatch(state,
                            (alivePlayerCount == 1) ? survivingPlayerId : std::nullopt,
                            alivePlayerCount == 0,
                            activePlayerCount,
                            alivePlayerCount);
        }
    } // namespace

    // =================================================================================================================
    // ===== Fixed-Tick Simulation =====================================================================================
    // =================================================================================================================

    void simulateServerTick(ServerState& state)
    {
        ++state.serverTick;
        state.diag.advanceTick();
        advanceServerFlow(state);

        if (state.phase != ServerPhase::InMatch && state.phase != ServerPhase::EndOfMatch)
            return;

        bool anyMatchPlayer = false;

        for (auto& slot : state.matchPlayers)
        {
            if (!slot.has_value())
                continue;

            anyMatchPlayer = true;
            auto& matchPlayer = slot.value();
            simulateAcceptedMatchPlayer(state, matchPlayer);
        }

        if (state.phase == ServerPhase::InMatch)
        {
            resolveExplodingBombs(state);
            collectRevealedPowerups(state);
            evaluateRoundEnd(state);
        }

        if (!anyMatchPlayer)
            return;

        broadcastSnapshotIfDue(state);
    }

} // namespace bomberman::server
