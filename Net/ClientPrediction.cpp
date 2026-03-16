#include "ClientPrediction.h"

#include <algorithm>
#include <cstdlib>

namespace bomberman::net
{
    void ClientPrediction::reset() noexcept
    {
        initialized_ = false;
        lastRecordedInputSeq_ = 0;
        lastAppliedStateSeq_ = 0;
        recoveryCatchUpSeq_ = 0;
        recoveryActive_ = false;
        currentState_ = {};
        stats_ = {};
        clearHistory();
    }

    void ClientPrediction::initialize(const sim::TilePos startPosQ, const uint32_t lastProcessedInputSeq) noexcept
    {
        initialized_ = true;
        lastRecordedInputSeq_ = lastProcessedInputSeq;
        lastAppliedStateSeq_ = lastProcessedInputSeq;
        recoveryCatchUpSeq_ = 0;
        recoveryActive_ = false;
        currentState_ = {};
        currentState_.posQ = startPosQ;
        currentState_.buttons = 0;
        clearHistory();
    }

    bool ClientPrediction::applyLocalInput(const uint32_t inputSeq,
                                           const uint8_t buttons,
                                           const sim::TileMap& map) noexcept
    {
        if (!initialized_ || inputSeq == 0)
        {
            ++stats_.rejectedLocalInputs;
            return false;
        }

        const uint32_t expectedNextSeq = lastRecordedInputSeq_ + 1u;
        if (inputSeq != expectedNextSeq)
        {
            ++stats_.rejectedLocalInputs;
            return false;
        }

        storeInputHistory(inputSeq, buttons);
        lastRecordedInputSeq_ = inputSeq;

        if (recoveryActive_)
        {
            ++stats_.localInputsDeferred;
            return true;
        }

        currentState_ = simulateStateFromInput(currentState_, buttons, map);
        storeStateHistory(inputSeq, currentState_);
        lastAppliedStateSeq_ = inputSeq;
        ++stats_.localInputsApplied;
        return true;
    }

    bool ClientPrediction::tryGetPredictedState(const uint32_t inputSeq, PredictedPlayerState& outState) const noexcept
    {
        const StateHistoryEntry* entry = findStateHistory(inputSeq);
        if (entry == nullptr)
            return false;

        outState = entry->state;
        return true;
    }

    bool ClientPrediction::hasPendingInputsAfter(const uint32_t inputSeq) const noexcept
    {
        return initialized_ && lastRecordedInputSeq_ > inputSeq;
    }

    CorrectionReplayResult ClientPrediction::applyCorrectionAndReplay(const MsgCorrection& correction,
                                                                      const sim::TileMap& map) noexcept
    {
        CorrectionReplayResult result{};
        const bool wasRecovering = recoveryActive_;
        ++stats_.correctionsApplied;

        PredictedPlayerState predictedAtAck{};
        result.hadPredictedStateAtAck =
            tryGetPredictedState(correction.lastProcessedInputSeq, predictedAtAck);

        const sim::TilePos authoritativePosQ{
            correction.xQ,
            correction.yQ
        };

        if (result.hadPredictedStateAtAck)
        {
            ++stats_.correctionsWithPredictedState;
            result.deltaXQ = static_cast<int32_t>(authoritativePosQ.xQ) - static_cast<int32_t>(predictedAtAck.posQ.xQ);
            result.deltaYQ = static_cast<int32_t>(authoritativePosQ.yQ) - static_cast<int32_t>(predictedAtAck.posQ.yQ);
            result.deltaManhattanQ =
                static_cast<uint32_t>(std::abs(result.deltaXQ) + std::abs(result.deltaYQ));
            result.correctionMatchedPrediction =
                predictedAtAck.posQ.xQ == authoritativePosQ.xQ &&
                predictedAtAck.posQ.yQ == authoritativePosQ.yQ;
            if (!result.correctionMatchedPrediction)
                ++stats_.correctionsMismatched;
        }

        stats_.totalCorrectionDeltaQ += result.deltaManhattanQ;
        stats_.maxCorrectionDeltaQ = std::max(stats_.maxCorrectionDeltaQ, result.deltaManhattanQ);

        if (!initialized_)
        {
            initialize(authoritativePosQ, correction.lastProcessedInputSeq);
            result.recoveryStillActive = false;
            return result;
        }

        discardAcknowledgedHistory(correction.lastProcessedInputSeq);

        if (recoveryActive_)
        {
            setAppliedAuthoritativeState(authoritativePosQ, correction.lastProcessedInputSeq, 0);

            if (correction.lastProcessedInputSeq < recoveryCatchUpSeq_)
            {
                result.recoveryStillActive = true;
                result.remainingDeferredInputs = countRetainedInputsAfter(correction.lastProcessedInputSeq);
                result.catchUpSeq = recoveryCatchUpSeq_;
                return result;
            }

            invalidateStateHistoryRange(correction.lastProcessedInputSeq + 1u, lastRecordedInputSeq_);
            if (replayRetainedInputsAfter(correction.lastProcessedInputSeq, map, result))
            {
                recoveryActive_ = false;
                recoveryCatchUpSeq_ = 0;
                result.recoveryResolved = true;
                result.recoveryStillActive = false;
                ++stats_.recoveryResolutions;
                return result;
            }

            setAppliedAuthoritativeState(authoritativePosQ, correction.lastProcessedInputSeq, 0);
            invalidateStateHistoryRange(correction.lastProcessedInputSeq + 1u, lastRecordedInputSeq_);
            recoveryActive_ = true;
            recoveryCatchUpSeq_ = lastRecordedInputSeq_;
            result.recoveryTriggered = true;
            result.recoveryRetruncated = true;
            result.recoveryStillActive = true;
            result.remainingDeferredInputs = countRetainedInputsAfter(correction.lastProcessedInputSeq);
            result.catchUpSeq = recoveryCatchUpSeq_;
            ++stats_.replayTruncations;
            stats_.totalMissingInputHistory += result.missingInputHistory;
            stats_.maxMissingInputHistory = std::max(stats_.maxMissingInputHistory, result.missingInputHistory);
            return result;
        }

        const uint8_t authoritativeButtons =
            result.hadPredictedStateAtAck ? predictedAtAck.buttons : 0;
        setAppliedAuthoritativeState(authoritativePosQ, correction.lastProcessedInputSeq, authoritativeButtons);

        if (lastRecordedInputSeq_ <= correction.lastProcessedInputSeq)
        {
            result.recoveryStillActive = false;
            return result;
        }

        invalidateStateHistoryRange(correction.lastProcessedInputSeq + 1u, lastRecordedInputSeq_);
        if (replayRetainedInputsAfter(correction.lastProcessedInputSeq, map, result))
        {
            result.recoveryStillActive = false;
            return result;
        }

        setAppliedAuthoritativeState(authoritativePosQ, correction.lastProcessedInputSeq, 0);
        invalidateStateHistoryRange(correction.lastProcessedInputSeq + 1u, lastRecordedInputSeq_);
        recoveryActive_ = true;
        recoveryCatchUpSeq_ = lastRecordedInputSeq_;
        result.recoveryTriggered = true;
        result.recoveryStillActive = true;
        result.remainingDeferredInputs = countRetainedInputsAfter(correction.lastProcessedInputSeq);
        result.catchUpSeq = recoveryCatchUpSeq_;
        ++stats_.replayTruncations;
        stats_.totalMissingInputHistory += result.missingInputHistory;
        stats_.maxMissingInputHistory = std::max(stats_.maxMissingInputHistory, result.missingInputHistory);
        if (!wasRecovering)
            ++stats_.recoveryActivations;
        return result;
    }

    void ClientPrediction::setAppliedAuthoritativeState(const sim::TilePos posQ,
                                                        const uint32_t seq,
                                                        const uint8_t buttons) noexcept
    {
        currentState_.posQ = posQ;
        currentState_.buttons = buttons;
        lastAppliedStateSeq_ = seq;
    }

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

    void ClientPrediction::storeStateHistory(const uint32_t seq, const PredictedPlayerState& state) noexcept
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

    uint32_t ClientPrediction::countRetainedInputsAfter(const uint32_t inputSeq) const noexcept
    {
        uint32_t count = 0;
        for (const auto& inputSlot : inputHistory_)
        {
            if (inputSlot.valid && inputSlot.seq > inputSeq)
                ++count;
        }
        return count;
    }

    bool ClientPrediction::replayRetainedInputsAfter(const uint32_t lastProcessedInputSeq,
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
                result.missingInputHistory = lastRecordedInputSeq_ - seq + 1u;
                stats_.totalReplayedInputs += result.replayedInputs;
                stats_.maxReplayedInputs = std::max(stats_.maxReplayedInputs, result.replayedInputs);
                result.remainingDeferredInputs = countRetainedInputsAfter(lastProcessedInputSeq);
                return false;
            }

            currentState_ = simulateStateFromInput(currentState_, inputEntry->buttons, map);
            storeStateHistory(seq, currentState_);
            lastAppliedStateSeq_ = seq;
            ++result.replayedInputs;
        }

        stats_.totalReplayedInputs += result.replayedInputs;
        stats_.maxReplayedInputs = std::max(stats_.maxReplayedInputs, result.replayedInputs);
        result.remainingDeferredInputs = 0;
        return true;
    }

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

    PredictedPlayerState ClientPrediction::simulateStateFromInput(const PredictedPlayerState& baseState,
                                                                  const uint8_t buttons,
                                                                  const sim::TileMap& map) const noexcept
    {
        PredictedPlayerState nextState = baseState;
        nextState.buttons = buttons;
        nextState.posQ = sim::stepMovementWithCollision(
            baseState.posQ,
            buttonsToMoveX(buttons),
            buttonsToMoveY(buttons),
            map);
        return nextState;
    }

} // namespace bomberman::net
