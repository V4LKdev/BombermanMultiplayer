#ifndef BOMBERMAN_NET_CLIENTPREDICTION_H
#define BOMBERMAN_NET_CLIENTPREDICTION_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "Net/NetCommon.h"
#include "Sim/Movement.h"

namespace bomberman::net
{
    /** @brief Power-of-two ring size used for local prediction history. */
    constexpr std::size_t kClientPredictionHistorySize = 128;

    static_assert((kClientPredictionHistorySize & (kClientPredictionHistorySize - 1u)) == 0u,
                  "kClientPredictionHistorySize must be a power of two");

    /**
     * @brief Predicted local-player state immediately after a specific input sequence.
     *
     * The sequence key itself lives in the owning history entry. This state stores
     * only the simulation result and the buttons that produced it.
     */
    struct PredictedPlayerState
    {
        sim::TilePos posQ{};
        uint8_t buttons = 0;
    };

    /** @brief Summary returned after applying an authoritative correction and replaying pending inputs. */
    struct CorrectionReplayResult
    {
        bool hadPredictedStateAtAck = false;
        bool correctionMatchedPrediction = false;
        bool recoveryTriggered = false;
        bool recoveryRetruncated = false;
        bool recoveryResolved = false;
        bool recoveryStillActive = false;
        uint32_t replayedInputs = 0;
        uint32_t missingInputHistory = 0;
        uint32_t remainingDeferredInputs = 0;
        uint32_t catchUpSeq = 0;
        int32_t deltaXQ = 0;
        int32_t deltaYQ = 0;
        uint32_t deltaManhattanQ = 0;
    };

    /** @brief Aggregate prediction stats used for manual testing and diagnostics. */
    struct PredictionStats
    {
        uint32_t localInputsApplied = 0;
        uint32_t localInputsDeferred = 0;
        uint32_t rejectedLocalInputs = 0;

        uint32_t correctionsApplied = 0;
        uint32_t correctionsWithPredictedState = 0;
        uint32_t correctionsMismatched = 0;

        uint32_t totalCorrectionDeltaQ = 0;
        uint32_t maxCorrectionDeltaQ = 0;

        uint32_t totalReplayedInputs = 0;
        uint32_t maxReplayedInputs = 0;

        uint32_t replayTruncations = 0;
        uint32_t totalMissingInputHistory = 0;
        uint32_t maxMissingInputHistory = 0;
        uint32_t recoveryActivations = 0;
        uint32_t recoveryResolutions = 0;
    };

    /**
     * @brief Client-side local prediction helper for input history and replay.
     *
     * This class is intentionally transport-agnostic after construction: callers provide
     * input sequences, button bitmasks, and authoritative corrections. It owns only the
     * client-local prediction history required for later reconciliation.
     */
    class ClientPrediction
    {
      public:
        ClientPrediction() = default;

        /** @brief Clears all prediction state and history. */
        void reset() noexcept;

        /**
         * @brief Seeds prediction from an authoritative position and processed-input cursor.
         *
         * Clears all prior history and starts a fresh local prediction timeline.
         */
        void initialize(sim::TilePos startPosQ, uint32_t lastProcessedInputSeq = 0) noexcept;

        /** @brief Returns true once prediction has been seeded from an authoritative state. */
        [[nodiscard]]
        bool isInitialized() const noexcept { return initialized_; }

        /** @brief Returns the current local-player state (predicted normally, authoritative during recovery). */
        [[nodiscard]]
        const PredictedPlayerState& currentState() const noexcept { return currentState_; }

        /** @brief Returns aggregate prediction diagnostics for the current local session. */
        [[nodiscard]]
        const PredictionStats& stats() const noexcept { return stats_; }

        /** @brief Returns the highest local input sequence recorded in history. */
        [[nodiscard]]
        uint32_t lastRecordedInputSeq() const noexcept { return lastRecordedInputSeq_; }

        /** @brief Returns the sequence currently represented by `currentState_`. */
        [[nodiscard]]
        uint32_t lastAppliedStateSeq() const noexcept { return lastAppliedStateSeq_; }

        /** @brief Returns true while local prediction is suspended waiting for authority to catch up. */
        [[nodiscard]]
        bool isRecoveryActive() const noexcept { return recoveryActive_; }

        /**
         * @brief Applies one new local input to prediction and stores both input/state history.
         *
         * Returns false if prediction is uninitialized or if the input sequence is not the next
         * contiguous local sequence. While recovery is active, the input is recorded but not
         * simulated into `currentState_`.
         */
        bool applyLocalInput(uint32_t inputSeq, uint8_t buttons, const sim::TileMap& map) noexcept;

        /** @brief Returns the predicted post-state for a specific input sequence, if still retained. */
        [[nodiscard]]
        bool tryGetPredictedState(uint32_t inputSeq, PredictedPlayerState& outState) const noexcept;

        /** @brief Returns true if local prediction still has any pending inputs after the given ack sequence. */
        [[nodiscard]]
        bool hasPendingInputsAfter(uint32_t inputSeq) const noexcept;

        /**
         * @brief Resets to an authoritative correction and replays retained pending local inputs.
         *
         * The helper invalidates stale predicted-state history after the correction point and
         * rebuilds it from retained input history. If replay cannot continue due to missing
         * history, the helper enters recovery mode and suspends local prediction until authority
         * catches through the unknown suffix.
         */
        [[nodiscard]]
        CorrectionReplayResult applyCorrectionAndReplay(const MsgCorrection& correction, const sim::TileMap& map) noexcept;

      private:
        struct InputHistoryEntry
        {
            uint32_t seq = 0;
            uint8_t buttons = 0;
            bool valid = false;
        };

        struct StateHistoryEntry
        {
            uint32_t seq = 0;
            PredictedPlayerState state{};
            bool valid = false;
        };

        [[nodiscard]]
        static constexpr std::size_t historySlot(uint32_t seq) noexcept
        {
            return seq & (kClientPredictionHistorySize - 1u);
        }

        void setAppliedAuthoritativeState(sim::TilePos posQ, uint32_t seq, uint8_t buttons = 0) noexcept;
        void clearHistory() noexcept;
        void storeInputHistory(uint32_t seq, uint8_t buttons) noexcept;
        void storeStateHistory(uint32_t seq, const PredictedPlayerState& state) noexcept;
        void invalidateStateHistoryRange(uint32_t firstSeq, uint32_t lastSeq) noexcept;
        void discardAcknowledgedHistory(uint32_t lastProcessedInputSeq) noexcept;
        [[nodiscard]]
        uint32_t countRetainedInputsAfter(uint32_t inputSeq) const noexcept;
        [[nodiscard]]
        bool replayRetainedInputsAfter(uint32_t lastProcessedInputSeq,
                                       const sim::TileMap& map,
                                       CorrectionReplayResult& result) noexcept;

        [[nodiscard]]
        const InputHistoryEntry* findInputHistory(uint32_t seq) const noexcept;

        [[nodiscard]]
        const StateHistoryEntry* findStateHistory(uint32_t seq) const noexcept;

        [[nodiscard]]
        PredictedPlayerState simulateStateFromInput(const PredictedPlayerState& baseState,
                                                    uint8_t buttons,
                                                    const sim::TileMap& map) const noexcept;

        std::array<InputHistoryEntry, kClientPredictionHistorySize> inputHistory_{};
        std::array<StateHistoryEntry, kClientPredictionHistorySize> stateHistory_{};

        PredictedPlayerState currentState_{};
        PredictionStats stats_{};
        uint32_t lastRecordedInputSeq_ = 0;
        uint32_t lastAppliedStateSeq_ = 0;
        uint32_t recoveryCatchUpSeq_ = 0;
        bool recoveryActive_ = false;
        bool initialized_ = false;
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_CLIENTPREDICTION_H
