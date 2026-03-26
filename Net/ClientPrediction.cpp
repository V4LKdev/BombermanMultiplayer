/**
 * @file ClientPrediction.cpp
 * @brief Implementation of client-side local prediction, replay, and recovery helpers.
 */

#include "ClientPrediction.h"

#include <algorithm>
#include <cstdlib>

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Lifecycle =================================================================================================
    // =================================================================================================================

    void ClientPrediction::reset() noexcept
    {
        phase_ = PredictionPhase::AwaitingBaseline;

        lastRecordedInputSeq_ = 0;
        lastAuthoritativeInputSeq_ = 0;
        recoveryCatchUpSeq_ = 0;
        currentState_ = {};
        stats_ = {};

        clearHistory();
    }

    void ClientPrediction::suspend() noexcept
    {
        phase_ = PredictionPhase::AwaitingBaseline;
        lastRecordedInputSeq_ = 0;
        lastAuthoritativeInputSeq_ = 0;
        recoveryCatchUpSeq_ = 0;
        currentState_ = {};
        clearHistory();
    }

    // =================================================================================================================
    // ===== Local Input Path ==========================================================================================
    // =================================================================================================================

    bool ClientPrediction::applyLocalInput(const uint32_t inputSeq,
                                           const uint8_t buttons,
                                           const sim::TileMap& map) noexcept
    {
        if (inputSeq == 0)
        {
            ++stats_.rejectedLocalInputs;
            return false;
        }

        if (lastRecordedInputSeq_ != 0)
        {
            const uint32_t expectedNextSeq = lastRecordedInputSeq_ + 1u;
            if (inputSeq != expectedNextSeq)
            {
                ++stats_.rejectedLocalInputs;
                return false;
            }
        }

        storeInputHistory(inputSeq, buttons);
        lastRecordedInputSeq_ = inputSeq;

        if (phase_ == PredictionPhase::AwaitingBaseline)
        {
            /* Keep the local input suffix so the first correction can seed
             * prediction and replay it right away. */
            ++stats_.localInputsDeferred;
            return true;
        }

        if (phase_ == PredictionPhase::Recovering)
        {
            /* During recovery, keep recording local intent, but keep presenting
             * authoritative state until replay can resume safely. */
            ++stats_.localInputsDeferred;
            return true;
        }

        // Prediction is active, so simulate the new input immediately.
        currentState_ = simulateNextStateFromInput(currentState_, buttons, map);
        storeStateHistory(inputSeq, currentState_);
        ++stats_.localInputsApplied;
        return true;
    }

    // =================================================================================================================
    // ===== State Lookup ==============================================================================================
    // =================================================================================================================

    bool ClientPrediction::tryGetPredictedStateAtSeq(const uint32_t inputSeq, LocalPlayerState& outState) const noexcept
    {
        const StateHistoryEntry* entry = findStateHistory(inputSeq);
        if (entry == nullptr)
            return false;

        outState = entry->state;
        return true;
    }

    // =================================================================================================================
    // ===== Correction and Replay =====================================================================================
    // =================================================================================================================

    CorrectionReplayResult ClientPrediction::reconcileAndReplay(const MsgCorrection& correction,
                                                                const sim::TileMap& map) noexcept
    {
        /*
         * Correction and replay flow:
         * 1) Reject stale corrections that do not advance authority.
         * 2) Measure correction delta against any retained predicted state at the acked seq.
         * 3) If still awaiting a baseline, seed from the correction and replay the retained suffix after it.
         * 4) If recovering, wait until authority catches through the unresolved suffix, then try replay again.
         * 5) If active, replay from the new correction baseline or fall back into recovery.
         */
        CorrectionReplayResult result{};

        // The prediction core rejects duplicate or older corrections.
        if (phase_ != PredictionPhase::AwaitingBaseline &&
            correction.lastProcessedInputSeq <= lastAuthoritativeInputSeq_)
        {
            result.ignoredStaleCorrection = true;
            result.recoveryStillActive = phase_ == PredictionPhase::Recovering;
            result.recoveryCatchUpSeq = recoveryCatchUpSeq_;
            return result;
        }

        ++stats_.correctionsApplied;
        lastAuthoritativeInputSeq_ = correction.lastProcessedInputSeq;
        uint8_t predictedButtonsAtAck = 0;

        const sim::TilePos authoritativePosQ{
            correction.xQ,
            correction.yQ
        };
        const uint8_t authoritativePlayerFlags = correction.playerFlags;

        measureCorrectionAtAck(correction.lastProcessedInputSeq, authoritativePosQ, predictedButtonsAtAck, result);

        if (phase_ == PredictionPhase::AwaitingBaseline)
        {
            handleAwaitingBaselineCorrection(correction.lastProcessedInputSeq,
                                             authoritativePosQ,
                                             authoritativePlayerFlags,
                                             map,
                                             result);
            return result;
        }

        discardAcknowledgedHistory(correction.lastProcessedInputSeq);

        if (phase_ == PredictionPhase::Recovering)
        {
            handleRecoveringCorrection(correction.lastProcessedInputSeq,
                                       authoritativePosQ,
                                       authoritativePlayerFlags,
                                       map,
                                       result);
            return result;
        }

        const uint8_t authoritativeButtons =
            result.hadRetainedPredictedStateAtAck ? predictedButtonsAtAck : 0;

        handleActiveCorrection(correction.lastProcessedInputSeq,
                               authoritativePosQ,
                               authoritativeButtons,
                               authoritativePlayerFlags,
                               map,
                               result);

        return result;
    }

    // =================================================================================================================
    // ===== Correction Helpers ========================================================================================
    // =================================================================================================================

    void ClientPrediction::setCurrentAuthoritativeState(const sim::TilePos posQ,
                                                        const uint8_t buttons,
                                                        const uint8_t playerFlags) noexcept
    {
        currentState_.posQ = posQ;
        currentState_.buttons = buttons;
        currentState_.playerFlags = playerFlags;
    }

    void ClientPrediction::enterRecoveryFromReplayFailure(const sim::TilePos authoritativePosQ,
                                                          const uint32_t lastProcessedInputSeq,
                                                          CorrectionReplayResult& result,
                                                          const bool wasRecovering) noexcept
    {
        /*
         * The retained suffix is no longer complete enough to rebuild safely.
         * Fall back to authoritative presentation until a later correction catches up.
         */
        setCurrentAuthoritativeState(authoritativePosQ, 0, currentState_.playerFlags);
        invalidateStateHistoryRange(lastProcessedInputSeq + 1u, lastRecordedInputSeq_);

        phase_ = PredictionPhase::Recovering;
        recoveryCatchUpSeq_ = lastRecordedInputSeq_;
        result.recoveryTriggered = true;
        result.recoveryRestarted = wasRecovering;
        result.recoveryStillActive = true;

        result.remainingDeferredInputs = countRetainedInputSuffixAfter(lastProcessedInputSeq);
        result.recoveryCatchUpSeq = recoveryCatchUpSeq_;

        ++stats_.replayTruncations;
        stats_.totalMissingInputHistory += result.missingInputHistory;
        stats_.maxMissingInputHistory = std::max(stats_.maxMissingInputHistory, result.missingInputHistory);

        if (!wasRecovering)
            ++stats_.recoveryActivations;
    }

    bool ClientPrediction::replayFromAuthoritativeBaseline(const uint32_t lastProcessedInputSeq,
                                                           const sim::TilePos authoritativePosQ,
                                                           const uint8_t authoritativeButtons,
                                                           const uint8_t authoritativePlayerFlags,
                                                           const sim::TileMap& map,
                                                           CorrectionReplayResult& result) noexcept
    {
        setCurrentAuthoritativeState(authoritativePosQ, authoritativeButtons, authoritativePlayerFlags);

        if (lastRecordedInputSeq_ <= lastProcessedInputSeq)
        {
            result.recoveryStillActive = false;
            return true;
        }

        invalidateStateHistoryRange(lastProcessedInputSeq + 1u, lastRecordedInputSeq_);
        if (replayRetainedInputSuffixAfter(lastProcessedInputSeq, map, result))
        {
            result.recoveryStillActive = false;
            return true;
        }

        return false;
    }

    void ClientPrediction::handleAwaitingBaselineCorrection(const uint32_t lastProcessedInputSeq,
                                                            const sim::TilePos authoritativePosQ,
                                                            const uint8_t authoritativePlayerFlags,
                                                            const sim::TileMap& map,
                                                            CorrectionReplayResult& result) noexcept
    {
        /*
         * The first correction seeds prediction from a real authoritative baseline,
         * then immediately tries to replay any retained local inputs after it.
         */
        phase_ = PredictionPhase::Active;
        recoveryCatchUpSeq_ = 0;
        stateHistory_ = {};

        discardAcknowledgedHistory(lastProcessedInputSeq);

        if (replayFromAuthoritativeBaseline(lastProcessedInputSeq,
                                            authoritativePosQ,
                                            0,
                                            authoritativePlayerFlags,
                                            map,
                                            result))
            return;

        /* If replay fails here, the retained input suffix is incomplete. */
        enterRecoveryFromReplayFailure(authoritativePosQ, lastProcessedInputSeq, result, false);
    }

    void ClientPrediction::handleRecoveringCorrection(const uint32_t lastProcessedInputSeq,
                                                      const sim::TilePos authoritativePosQ,
                                                      const uint8_t authoritativePlayerFlags,
                                                      const sim::TileMap& map,
                                                      CorrectionReplayResult& result) noexcept
    {
        if (lastProcessedInputSeq < recoveryCatchUpSeq_)
        {
            setCurrentAuthoritativeState(authoritativePosQ, 0, authoritativePlayerFlags);
            result.recoveryStillActive = true;
            result.remainingDeferredInputs = countRetainedInputSuffixAfter(lastProcessedInputSeq);
            result.recoveryCatchUpSeq = recoveryCatchUpSeq_;
            return;
        }

        if (replayFromAuthoritativeBaseline(lastProcessedInputSeq,
                                            authoritativePosQ,
                                            0,
                                            authoritativePlayerFlags,
                                            map,
                                            result))
        {
            phase_ = PredictionPhase::Active;
            recoveryCatchUpSeq_ = 0;
            result.recoveryResolved = true;
            ++stats_.recoveryResolutions;
            return;
        }

        enterRecoveryFromReplayFailure(authoritativePosQ, lastProcessedInputSeq, result, true);
    }

    void ClientPrediction::handleActiveCorrection(const uint32_t lastProcessedInputSeq,
                                                  const sim::TilePos authoritativePosQ,
                                                  const uint8_t authoritativeButtons,
                                                  const uint8_t authoritativePlayerFlags,
                                                  const sim::TileMap& map,
                                                  CorrectionReplayResult& result) noexcept
    {
        if (replayFromAuthoritativeBaseline(lastProcessedInputSeq,
                                            authoritativePosQ,
                                            authoritativeButtons,
                                            authoritativePlayerFlags,
                                            map,
                                            result))
            return;

        enterRecoveryFromReplayFailure(authoritativePosQ, lastProcessedInputSeq, result, false);
    }

    void ClientPrediction::measureCorrectionAtAck(const uint32_t lastProcessedInputSeq,
                                                  const sim::TilePos authoritativePosQ,
                                                  uint8_t& predictedButtonsAtAck,
                                                  CorrectionReplayResult& result) noexcept
    {
        LocalPlayerState predictedAtAck{};
        result.hadRetainedPredictedStateAtAck = tryGetPredictedStateAtSeq(lastProcessedInputSeq, predictedAtAck);

        if (result.hadRetainedPredictedStateAtAck)
        {
            predictedButtonsAtAck = predictedAtAck.buttons;
            ++stats_.correctionsWithRetainedPredictedState;

            result.deltaXQ = static_cast<int32_t>(authoritativePosQ.xQ) - predictedAtAck.posQ.xQ;
            result.deltaYQ = static_cast<int32_t>(authoritativePosQ.yQ) - predictedAtAck.posQ.yQ;
            result.deltaManhattanQ = static_cast<uint32_t>(std::abs(result.deltaXQ) + std::abs(result.deltaYQ));

            result.correctionMatchedRetainedPrediction =
                predictedAtAck.posQ.xQ == authoritativePosQ.xQ &&
                predictedAtAck.posQ.yQ == authoritativePosQ.yQ;

            if (!result.correctionMatchedRetainedPrediction)
                ++stats_.correctionsMismatched;
        }

        stats_.totalCorrectionDeltaQ += result.deltaManhattanQ;
        stats_.maxCorrectionDeltaQ = std::max(stats_.maxCorrectionDeltaQ, result.deltaManhattanQ);
    }

    // =================================================================================================================
    // ===== History Storage ===========================================================================================
    // =================================================================================================================

    void ClientPrediction::clearHistory() noexcept
    {
        inputHistory_ = {};
        stateHistory_ = {};
    }

    void ClientPrediction::storeInputHistory(const uint32_t seq, const uint8_t buttons) noexcept
    {
        auto& slot = inputHistory_[historySlot(seq)];
        slot.seq = seq;
        slot.buttons = buttons;
        slot.valid = true;
    }

    void ClientPrediction::storeStateHistory(const uint32_t seq, const LocalPlayerState& state) noexcept
    {
        auto& slot = stateHistory_[historySlot(seq)];
        slot.seq = seq;
        slot.state = state;
        slot.valid = true;
    }

    void ClientPrediction::invalidateStateHistoryRange(const uint32_t firstSeq, const uint32_t lastSeq) noexcept
    {
        if (firstSeq == 0 || lastSeq < firstSeq)
            return;

        for (auto& slot : stateHistory_)
        {
            if (slot.valid && slot.seq >= firstSeq && slot.seq <= lastSeq)
                slot.valid = false;
        }
    }

    void ClientPrediction::discardAcknowledgedHistory(const uint32_t lastProcessedInputSeq) noexcept
    {
        if (lastProcessedInputSeq == 0)
            return;

        for (auto& inputSlot : inputHistory_)
        {
            if (inputSlot.valid && inputSlot.seq <= lastProcessedInputSeq)
                inputSlot.valid = false;
        }

        for (auto& stateSlot : stateHistory_)
        {
            if (stateSlot.valid && stateSlot.seq <= lastProcessedInputSeq)
                stateSlot.valid = false;
        }
    }

    uint32_t ClientPrediction::countRetainedInputSuffixAfter(const uint32_t inputSeq) const noexcept
    {
        uint32_t count = 0;
        for (const auto& inputSlot : inputHistory_)
        {
            if (inputSlot.valid && inputSlot.seq > inputSeq)
                ++count;
        }
        return count;
    }

    bool ClientPrediction::replayRetainedInputSuffixAfter(const uint32_t lastProcessedInputSeq,
                                                          const sim::TileMap& map,
                                                          CorrectionReplayResult& result) noexcept
    {
        if (lastRecordedInputSeq_ <= lastProcessedInputSeq)
            return true;

        for (uint32_t seq = lastProcessedInputSeq + 1u; seq <= lastRecordedInputSeq_; ++seq)
        {
            const InputHistoryEntry* inputEntry = findInputHistory(seq);
            if (inputEntry == nullptr)
            {
                // Replay needs a contiguous retained suffix. Once one input is
                // missing, the rest of the suffix can no longer be rebuilt safely.
                result.missingInputHistory = lastRecordedInputSeq_ - seq + 1u;
                stats_.totalReplayedInputs += result.replayedInputs;
                stats_.maxReplayedInputs = std::max(stats_.maxReplayedInputs, result.replayedInputs);
                result.remainingDeferredInputs = countRetainedInputSuffixAfter(lastProcessedInputSeq);
                return false;
            }

            currentState_ = simulateNextStateFromInput(currentState_, inputEntry->buttons, map);
            storeStateHistory(seq, currentState_);
            ++result.replayedInputs;
        }

        stats_.totalReplayedInputs += result.replayedInputs;
        stats_.maxReplayedInputs = std::max(stats_.maxReplayedInputs, result.replayedInputs);
        result.remainingDeferredInputs = 0;
        return true;
    }

    // =================================================================================================================
    // ===== Ring Lookups and Simulation ===============================================================================
    // =================================================================================================================

    const ClientPrediction::InputHistoryEntry* ClientPrediction::findInputHistory(const uint32_t seq) const noexcept
    {
        const auto& slot = inputHistory_[historySlot(seq)];
        if (!slot.valid || slot.seq != seq)
            return nullptr;

        return &slot;
    }

    const ClientPrediction::StateHistoryEntry* ClientPrediction::findStateHistory(const uint32_t seq) const noexcept
    {
        const auto& slot = stateHistory_[historySlot(seq)];
        if (!slot.valid || slot.seq != seq)
            return nullptr;

        return &slot;
    }

    LocalPlayerState ClientPrediction::simulateNextStateFromInput(const LocalPlayerState& baseState,
                                                                  const uint8_t buttons,
                                                                  const sim::TileMap& map) const noexcept
    {
        LocalPlayerState nextState = baseState;
        nextState.buttons = buttons;
        const bool speedBoostActive =
            (baseState.playerFlags &
             static_cast<uint8_t>(MsgSnapshot::PlayerEntry::EPlayerFlags::SpeedBoost)) != 0;
        nextState.posQ = sim::stepMovementWithCollision(
            baseState.posQ,
            buttonsToMoveX(buttons),
            buttonsToMoveY(buttons),
            map,
            speedBoostActive ? sim::kSpeedBoostPlayerSpeedQ : sim::kPlayerSpeedQ);
        return nextState;
    }

} // namespace bomberman::net
