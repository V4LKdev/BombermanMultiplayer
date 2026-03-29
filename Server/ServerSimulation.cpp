/**
 * @file ServerSimulation.cpp
 * @ingroup authoritative_server
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

namespace bomberman::server
{
    namespace
    {
        [[nodiscard]]
        net::MsgCorrection buildCorrection(const ServerState& state, const MatchPlayerState& matchPlayer)
        {
            net::MsgCorrection corr{};
            corr.matchId = state.currentMatchId;
            corr.serverTick = state.serverTick;
            corr.lastProcessedInputSeq = matchPlayer.lastProcessedInputSeq;
            corr.xQ = static_cast<int16_t>(std::clamp(matchPlayer.pos.xQ, INT16_MIN, INT16_MAX));
            corr.yQ = static_cast<int16_t>(std::clamp(matchPlayer.pos.yQ, INT16_MIN, INT16_MAX));
            corr.playerFlags = buildReplicatedPlayerFlags(matchPlayer, state.serverTick);
            return corr;
        }

        /** @brief Resolves which input bitmask to simulate on the current tick. */
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
                // The first input may arrive after the nominal unlock+lead deadline; realign to keep the configured
                // lead buffer rather than walking forward on stale buttons.
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

            if (matchPlayer.consecutiveInputGaps >= net::kRepeatedInputWarnThreshold
                && state.serverTick >= matchPlayer.nextGapWarnTick)
            {
                LOG_NET_INPUT_WARN(
                    "Repeated input gaps playerId={} streak={} expectedSeq={} slotValid={} slotSeq={} using lastAppliedButtons=0x{:02x} lastRecv={} lastProcessed={}",
                    matchPlayer.playerId, matchPlayer.consecutiveInputGaps,
                    nextSeq,
                    entry.valid ? 1 : 0, entry.seq,
                    matchPlayer.lastAppliedButtons,
                    matchPlayer.lastReceivedInputSeq, matchPlayer.lastProcessedInputSeq);

                matchPlayer.nextGapWarnTick = state.serverTick + net::kRepeatedInputWarnCooldownTicks;
            }
        }

        void queueMatchPlayerCorrection(ServerState& state, const MatchPlayerState& matchPlayer)
        {
            bool correctionQueued = false;
            if (const auto* session = findPeerSessionByPlayerId(state, matchPlayer.playerId);
                session != nullptr && session->peer != nullptr)
            {
                const auto correction = buildCorrection(state, matchPlayer);
                const auto correctionBytes = net::makeCorrectionPacket(correction);
                correctionQueued = net::queueUnreliableCorrection(session->peer, correctionBytes);
            }

            state.diag.recordPacketSent(net::EMsgType::Correction,
                                        matchPlayer.playerId,
                                        static_cast<uint8_t>(net::EChannel::CorrectionUnreliable),
                                        net::kPacketHeaderSize + net::kMsgCorrectionSize,
                                        correctionQueued ? net::NetPacketResult::Ok : net::NetPacketResult::Dropped);
        }

        void samplePeerTransport(ServerState& state, const MatchPlayerState& matchPlayer)
        {
            const auto* session = findPeerSessionByPlayerId(state, matchPlayer.playerId);
            if (state.serverTick % net::kPeerTransportSampleTicks != 0 || session == nullptr || session->peer == nullptr)
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

        void finishMatchPlayerTick(ServerState& state, const MatchPlayerState& matchPlayer)
        {
            queueMatchPlayerCorrection(state, matchPlayer);
            samplePeerTransport(state, matchPlayer);
        }

        void sampleMatchPlayerInputContinuity(ServerState& state, const MatchPlayerState& matchPlayer)
        {
            state.diag.samplePeerInputContinuity(matchPlayer.playerId,
                                                 matchPlayer.lastReceivedInputSeq,
                                                 matchPlayer.lastProcessedInputSeq);
        }

        void clearMatchPlayerButtons(MatchPlayerState& matchPlayer)
        {
            matchPlayer.appliedButtons = 0;
            matchPlayer.lastAppliedButtons = 0;
            matchPlayer.previousTickButtons = 0;
        }

        void simulateAcceptedMatchPlayer(ServerState& state, MatchPlayerState& matchPlayer)
        {
            refreshMatchPlayerPowerupLoadout(state, matchPlayer);

            if (!matchPlayer.alive || matchPlayer.inputLocked)
            {
                clearMatchPlayerButtons(matchPlayer);
                sampleMatchPlayerInputContinuity(state, matchPlayer);
                finishMatchPlayerTick(state, matchPlayer);
                return;
            }

            resolveMatchPlayerInputForTick(state, matchPlayer);
            sampleMatchPlayerInputContinuity(state, matchPlayer);

            tryPlaceBomb(state, matchPlayer);

            const int8_t moveX = net::buttonsToMoveX(matchPlayer.appliedButtons);
            const int8_t moveY = net::buttonsToMoveY(matchPlayer.appliedButtons);
            matchPlayer.pos = sim::stepMovementWithCollision(
                matchPlayer.pos,
                moveX,
                moveY,
                state.tiles,
                hasSpeedBoost(matchPlayer, state.serverTick) ? sim::kSpeedBoostPlayerSpeedQ : sim::kPlayerSpeedQ);
            matchPlayer.previousTickButtons = matchPlayer.appliedButtons;
            finishMatchPlayerTick(state, matchPlayer);
        }

        void broadcastSnapshotIfDue(ServerState& state)
        {
            if (!shouldBroadcastSnapshot(state))
            {
                return;
            }

            const auto snapshot = buildSnapshot(state);
            const auto snapshotBytes = net::makeSnapshotPacket(snapshot);
            if (state.serverTick % net::kServerSnapshotLogIntervalTicks == 0)
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

                const bool queued = net::queueUnreliableSnapshot(session->peer, snapshotBytes);
                state.diag.recordPacketSent(net::EMsgType::Snapshot,
                                            slot->playerId,
                                            static_cast<uint8_t>(net::EChannel::SnapshotUnreliable),
                                            net::kPacketHeaderSize + net::kMsgSnapshotSize,
                                            queued ? net::NetPacketResult::Ok : net::NetPacketResult::Dropped);
            }

            net::flush(state.host);
        }

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
