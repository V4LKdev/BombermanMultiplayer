/**
 * @file ServerSimulation.cpp
 * @brief Authoritative fixed-tick simulation, corrections, and snapshot broadcast.
 */

#include "ServerState.h"

#include <algorithm>
#include <climits>
#include <optional>

#include "Net/NetSend.h"
#include "ServerSnapshot.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    // =================================================================================================================
    // ===== Internal Helpers ==========================================================================================
    // =================================================================================================================

    namespace
    {
        /** @brief Returns true when the current authoritative buttons contain a new bomb-press edge. */
        [[nodiscard]]
        bool hasBombPlacementEdge(const MatchPlayerState& matchPlayer)
        {
            const bool bombHeldNow = (matchPlayer.appliedButtons & kInputBomb) != 0;
            const bool bombHeldLastTick = (matchPlayer.previousTickButtons & kInputBomb) != 0;
            return bombHeldNow && !bombHeldLastTick;
        }

        /** @brief Converts an authoritative player center position into the occupied tile cell. */
        [[nodiscard]]
        std::optional<BombCell> bombCellFromPlayerPosition(const sim::TilePos& pos)
        {
            const int32_t col = pos.xQ / 256;
            const int32_t row = pos.yQ / 256;
            if (col < 0 || row < 0 ||
                col >= static_cast<int32_t>(tileArrayWidth) ||
                row >= static_cast<int32_t>(tileArrayHeight))
            {
                return std::nullopt;
            }

            return BombCell{
                static_cast<uint8_t>(col),
                static_cast<uint8_t>(row)
            };
        }

        /** @brief Returns true when no active bomb currently occupies the given tile cell. */
        [[nodiscard]]
        bool isBombCellUnoccupied(const ServerState& state, const BombCell& cell)
        {
            for (const auto& bombEntry : state.bombs)
            {
                if (!bombEntry.has_value())
                    continue;

                if (bombEntry->cell.col == cell.col && bombEntry->cell.row == cell.row)
                    return false;
            }

            return true;
        }

        /** @brief Returns a free authoritative bomb slot, or `std::nullopt` when capacity is exhausted. */
        [[nodiscard]]
        std::optional<std::size_t> findFreeBombSlot(const ServerState& state)
        {
            for (std::size_t i = 0; i < state.bombs.size(); ++i)
            {
                if (!state.bombs[i].has_value())
                    return i;
            }

            return std::nullopt;
        }

        /** @brief Attempts to place one authoritative bomb for this player on the current server tick. */
        void tryPlaceBomb(ServerState& state, MatchPlayerState& matchPlayer)
        {
            if (!hasBombPlacementEdge(matchPlayer))
                return;

            if (!matchPlayer.alive)
            {
                LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because the player is dead",
                                    matchPlayer.playerId,
                                    state.serverTick);
                return;
            }

            if (matchPlayer.activeBombCount >= matchPlayer.maxBombs)
            {
                LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because activeBombCount={} maxBombs={}",
                                    matchPlayer.playerId,
                                    state.serverTick,
                                    matchPlayer.activeBombCount,
                                    matchPlayer.maxBombs);
                return;
            }

            const auto cell = bombCellFromPlayerPosition(matchPlayer.pos);
            if (!cell.has_value())
            {
                LOG_NET_INPUT_WARN("Rejected bomb placement playerId={} tick={} because the authoritative position is out of bounds pos=({}, {})",
                                   matchPlayer.playerId,
                                   state.serverTick,
                                   matchPlayer.pos.xQ,
                                   matchPlayer.pos.yQ);
                return;
            }

            const Tile tile = state.tiles[cell->row][cell->col];
            if (tile == Tile::Stone || tile == Tile::Brick)
            {
                LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because cell=({}, {}) is solid tile={}",
                                    matchPlayer.playerId,
                                    state.serverTick,
                                    static_cast<int>(cell->col),
                                    static_cast<int>(cell->row),
                                    static_cast<int>(tile));
                return;
            }

            if (!isBombCellUnoccupied(state, *cell))
            {
                LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because cell=({}, {}) is already occupied",
                                    matchPlayer.playerId,
                                    state.serverTick,
                                    static_cast<int>(cell->col),
                                    static_cast<int>(cell->row));
                return;
            }

            const auto freeSlot = findFreeBombSlot(state);
            if (!freeSlot.has_value())
            {
                LOG_NET_INPUT_WARN("Rejected bomb placement playerId={} tick={} because authoritative bomb capacity {} is exhausted",
                                   matchPlayer.playerId,
                                   state.serverTick,
                                   state.bombs.size());
                return;
            }

            auto& bombEntry = state.bombs[freeSlot.value()];
            bombEntry.emplace();
            auto& bomb = bombEntry.value();
            bomb.ownerId = matchPlayer.playerId;
            bomb.cell = *cell;
            bomb.placedTick = state.serverTick;
            bomb.explodeTick = state.serverTick + sim::kDefaultBombFuseTicks;
            bomb.radius = matchPlayer.bombRange;

            ++matchPlayer.activeBombCount;
            state.diag.recordBombPlaced(matchPlayer.playerId,
                                        bomb.cell.col,
                                        bomb.cell.row,
                                        bomb.radius,
                                        state.serverTick);

            LOG_NET_INPUT_INFO("Bomb placed playerId={} tick={} cell=({}, {}) radius={} activeBombs={}/{} explodeTick={}",
                               matchPlayer.playerId,
                               state.serverTick,
                               static_cast<int>(bomb.cell.col),
                               static_cast<int>(bomb.cell.row),
                               static_cast<int>(bomb.radius),
                               static_cast<int>(matchPlayer.activeBombCount),
                               static_cast<int>(matchPlayer.maxBombs),
                               bomb.explodeTick);
        }

        /** @brief Builds the owner correction that corresponds to the current authoritative tick. */
        [[nodiscard]]
        MsgCorrection buildCorrection(const ServerState& state, const MatchPlayerState& matchPlayer)
        {
            MsgCorrection corr{};
            corr.serverTick = state.serverTick;
            corr.lastProcessedInputSeq = matchPlayer.lastProcessedInputSeq;
            corr.xQ = static_cast<int16_t>(std::clamp(matchPlayer.pos.xQ, INT16_MIN, INT16_MAX));
            corr.yQ = static_cast<int16_t>(std::clamp(matchPlayer.pos.yQ, INT16_MIN, INT16_MAX));
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
                    state.diag.recordBufferedInputRecovery(matchPlayer.playerId, nextSeq, state.serverTick);

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
            resolveMatchPlayerInputForTick(state, matchPlayer);
            state.diag.samplePeerInputContinuity(matchPlayer.playerId,
                                                 matchPlayer.lastReceivedInputSeq,
                                                 matchPlayer.lastProcessedInputSeq);

            tryPlaceBomb(state, matchPlayer);

            const int8_t moveX = buttonsToMoveX(matchPlayer.appliedButtons);
            const int8_t moveY = buttonsToMoveY(matchPlayer.appliedButtons);
            matchPlayer.pos = sim::stepMovementWithCollision(matchPlayer.pos, moveX, moveY, state.tiles);
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
    } // namespace

    // =================================================================================================================
    // ===== Fixed-Tick Simulation =====================================================================================
    // =================================================================================================================

    void simulateServerTick(ServerState& state)
    {
        ++state.serverTick;
        state.diag.advanceTick();

        bool anyMatchPlayer = false;

        for (auto& slot : state.matchPlayers)
        {
            if (!slot.has_value())
                continue;

            anyMatchPlayer = true;
            auto& matchPlayer = slot.value();
            simulateAcceptedMatchPlayer(state, matchPlayer);
        }

        // TODO: Only movement is simulated on the server right now

        if (!anyMatchPlayer)
            return;

        broadcastSnapshotIfDue(state);
    }

} // namespace bomberman::server
