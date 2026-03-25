/**
 * @file ServerInputHandlers.cpp
 * @brief Authoritative server gameplay-input receive handling.
 */

#include "ServerHandlers.h"

#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    namespace
    {
        [[nodiscard]]
        MatchPlayerState* getAcceptedMatchPlayerState(ServerState& state, const PeerSession& session)
        {
            if (!session.playerId.has_value())
            {
                return nullptr;
            }

            auto& matchEntry = state.matchPlayers[session.playerId.value()];
            return matchEntry.has_value() ? &matchEntry.value() : nullptr;
        }

        struct BufferedInputStats
        {
            uint8_t tooLateCount = 0;
            uint8_t tooLateDirectCount = 0;
            uint8_t tooLateBufferedCount = 0;
            uint8_t tooFarAheadCount = 0;
            uint32_t firstTooFarAheadSeq = 0;
            uint32_t lastTooFarAheadSeq = 0;
        };

        [[nodiscard]]
        MatchPlayerState* requireAcceptedMatchPlayer(PacketDispatchContext& ctx)
        {
            auto* session = getPeerSession(ctx.peer);
            if (session == nullptr)
            {
                LOG_NET_INPUT_ERROR("Input peer {} has no live peer session - ignoring", ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            if (!session->playerId.has_value())
            {
                LOG_NET_INPUT_WARN("Input from non-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            auto* matchPlayer = getAcceptedMatchPlayerState(ctx.state, *session);
            if (matchPlayer == nullptr)
            {
                LOG_NET_INPUT_DEBUG("Ignoring gameplay input playerId={} because no active match state is currently bound",
                                    *session->playerId);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            ctx.recordedPlayerId = session->playerId;
            return matchPlayer;
        }

        [[nodiscard]]
        bool parseInputMessage(PacketDispatchContext& ctx, const uint8_t* payload, const std::size_t size, MsgInput& msgInput)
        {
            if (!deserializeMsgInput(payload, size, msgInput))
            {
                LOG_NET_PROTO_WARN("Failed to parse Input payload from peer {}", ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Malformed;
                return false;
            }

            return true;
        }

        [[nodiscard]]
        uint32_t highestSeqInBatch(const MsgInput& msgInput)
        {
            return msgInput.baseInputSeq + static_cast<uint32_t>(msgInput.count) - 1u;
        }

        [[nodiscard]]
        bool handleFullyStaleBatch(PacketDispatchContext& ctx,
                                   const MatchPlayerState& matchPlayer,
                                   const MsgInput& msgInput,
                                   const uint32_t highestSeq)
        {
            if (highestSeq > matchPlayer.lastProcessedInputSeq)
            {
                return false;
            }

            if (ctx.diag)
            {
                ctx.diag->recordInputPacketFullyStale();
                ctx.diag->recordInputEntriesTooLate(msgInput.count);
                ctx.diag->recordInputEntriesTooLateDirect(1);
                ctx.diag->recordInputEntriesTooLateBuffered(msgInput.count - 1u);
            }

            ctx.receiveResult = NetPacketResult::Ok;
            return true;
        }

        [[nodiscard]]
        BufferedInputStats bufferInputBatch(MatchPlayerState& matchPlayer,
                                            const MsgInput& msgInput,
                                            const uint32_t highestSeq,
                                            const uint32_t maxAcceptableSeq)
        {
            BufferedInputStats stats{};

            for (uint8_t i = 0; i < msgInput.count; ++i)
            {
                const uint32_t seq = msgInput.baseInputSeq + i;
                const uint8_t buttons = msgInput.inputs[i];
                const bool isDirectEntry = (seq == highestSeq);

                if (seq <= matchPlayer.lastProcessedInputSeq)
                {
                    ++stats.tooLateCount;
                    if (isDirectEntry)
                    {
                        ++stats.tooLateDirectCount;
                    }
                    else
                    {
                        ++stats.tooLateBufferedCount;
                    }
                    continue;
                }

                if (seq > maxAcceptableSeq)
                {
                    if (stats.tooFarAheadCount == 0)
                    {
                        stats.firstTooFarAheadSeq = seq;
                    }
                    stats.lastTooFarAheadSeq = seq;
                    ++stats.tooFarAheadCount;
                    continue;
                }

                auto& slot = matchPlayer.inputRing[seq % kServerInputBufferSize];
                const bool sameSeqAlreadyBuffered = slot.valid && slot.seq == seq;
                const bool seenDirect = sameSeqAlreadyBuffered ? slot.seenDirect : false;
                const bool seenBuffered = sameSeqAlreadyBuffered ? slot.seenBuffered : false;

                slot.seq = seq;
                slot.buttons = buttons;
                slot.valid = true;
                slot.seenDirect = seenDirect || isDirectEntry;
                slot.seenBuffered = seenBuffered || !isDirectEntry;

                if (seq > matchPlayer.lastReceivedInputSeq)
                {
                    matchPlayer.lastReceivedInputSeq = seq;
                }
            }

            return stats;
        }

        void recordBufferedInputDiagnostics(PacketDispatchContext& ctx, const BufferedInputStats& stats)
        {
            if (!ctx.diag)
            {
                return;
            }

            ctx.diag->recordInputEntriesTooLate(stats.tooLateCount);
            ctx.diag->recordInputEntriesTooLateDirect(stats.tooLateDirectCount);
            ctx.diag->recordInputEntriesTooLateBuffered(stats.tooLateBufferedCount);
            ctx.diag->recordInputEntriesTooFarAhead(stats.tooFarAheadCount);
        }

        void updateTooFarAheadBatchWarning(PacketDispatchContext& ctx,
                                           MatchPlayerState& matchPlayer,
                                           const MsgInput& msgInput,
                                           const uint32_t highestSeq,
                                           const uint32_t maxAcceptableSeq,
                                           const BufferedInputStats& stats)
        {
            if (stats.tooFarAheadCount == 0)
            {
                matchPlayer.consecutiveTooFarAheadBatches = 0;
                return;
            }

            ++matchPlayer.consecutiveTooFarAheadBatches;
            const uint16_t streak = matchPlayer.consecutiveTooFarAheadBatches;
            if (streak >= kRepeatedInputWarnThreshold &&
                ctx.state.serverTick >= matchPlayer.nextTooFarAheadWarnTick)
            {
                LOG_NET_INPUT_WARN(
                    "Repeated too-far-ahead input rejections playerId={} streak={} latestRejectedSeqs=[{}..{}] count={} batch=[{}..{}] maxAcceptable={} lastRecv={} lastProcessed={}",
                    matchPlayer.playerId,
                    streak,
                    stats.firstTooFarAheadSeq,
                    stats.lastTooFarAheadSeq,
                    stats.tooFarAheadCount,
                    msgInput.baseInputSeq,
                    highestSeq,
                    maxAcceptableSeq,
                    matchPlayer.lastReceivedInputSeq,
                    matchPlayer.lastProcessedInputSeq);

                matchPlayer.nextTooFarAheadWarnTick = ctx.state.serverTick + kRepeatedInputWarnCooldownTicks;
            }
        }

        void logAcceptedInputBatch(const MatchPlayerState& matchPlayer, const MsgInput& msgInput, const uint32_t highestSeq)
        {
            if ((highestSeq % kServerInputBatchLogIntervalTicks) != 0)
            {
                return;
            }

            LOG_NET_INPUT_DEBUG("Input playerId={} batch=[{}..{}] lastRecv={} lastProcessed={}",
                                matchPlayer.playerId,
                                msgInput.baseInputSeq,
                                highestSeq,
                                matchPlayer.lastReceivedInputSeq,
                                matchPlayer.lastProcessedInputSeq);
        }
    } // namespace

    void onInput(PacketDispatchContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t size)
    {
        auto* matchPlayer = requireAcceptedMatchPlayer(ctx);
        if (matchPlayer == nullptr)
        {
            return;
        }

        MsgInput msgInput{};
        if (!parseInputMessage(ctx, payload, size, msgInput))
        {
            return;
        }

        const uint32_t highestSeq = highestSeqInBatch(msgInput);
        const uint32_t maxAcceptableSeq = matchPlayer->lastProcessedInputSeq + kMaxBufferedInputLead;

        if (ctx.diag)
        {
            ctx.diag->recordInputPacketReceived();
        }

        if (handleFullyStaleBatch(ctx, *matchPlayer, msgInput, highestSeq))
        {
            return;
        }

        const BufferedInputStats stats = bufferInputBatch(*matchPlayer, msgInput, highestSeq, maxAcceptableSeq);
        recordBufferedInputDiagnostics(ctx, stats);
        updateTooFarAheadBatchWarning(ctx, *matchPlayer, msgInput, highestSeq, maxAcceptableSeq, stats);
        logAcceptedInputBatch(*matchPlayer, msgInput, highestSeq);

        ctx.receiveResult = NetPacketResult::Ok;
    }
} // namespace bomberman::server
