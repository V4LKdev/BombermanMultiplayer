/**
 * @file ClientPrediction.h
 * @brief Client-side local prediction history, correction replay, and recovery state.
 *
 * @details
 * The client keeps one contiguous local input timeline and a matching history of
 * predicted post-states.
 * The first owner correction provides the baseline that arms prediction.
 * Later corrections compare against retained predicted state at the acked input seq,
 * then rebuild the retained suffix after it.
 * If that suffix can no longer be replayed safely, prediction falls back to authoritative
 * presentation until a later correction allows replay to resume.
 *
 * @par Phases
 * - Awaiting baseline: local inputs are retained but not simulated yet.
 * - Active: the presented local state is predicted.
 * - Recovering: the presented local state is authoritative until replay is safe again.
 *
 * @par Histories
 * - Input history exists so retained local inputs can be replayed after a correction.
 * - State history exists so retained predicted state can be compared at the acked input seq.
 */

#ifndef BOMBERMAN_NET_CLIENTPREDICTION_H
#define BOMBERMAN_NET_CLIENTPREDICTION_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "Net/NetCommon.h"
#include "Sim/Movement.h"

namespace bomberman::net
{
    // ----- Constants and Types -----

    /** @brief Power-of-two ring size used for local prediction history and bitmask slot indexing. */
    constexpr std::size_t kClientPredictionHistorySize = 128;

    static_assert((kClientPredictionHistorySize & (kClientPredictionHistorySize - 1u)) == 0u,
                  "kClientPredictionHistorySize must be a power of two");

    /** @brief Local state currently presented for the owning player. */
    struct LocalPlayerState
    {
        sim::TilePos posQ{};
        uint8_t buttons = 0; ///< Latest local buttons, used for local facing/animation context.
        uint8_t playerFlags = 0; ///< Owner-local replicated flags that affect prediction, such as speed boost.
    };

    // ----- Diagnostics and Telemetry -----

    /**
     * @brief Summary of one correction/replay attempt.
     *
     * This tells the caller whether the correction was ignored,
     * replayed cleanly, or pushed prediction into recovery.
     */
    struct CorrectionReplayResult
    {
        bool ignoredStaleCorrection = false;         ///< Correction was not newer than the last accepted authoritative input seq.
        bool hadRetainedPredictedStateAtAck = false; ///< Predicted state was still retained at the acked input seq.
        bool correctionMatchedRetainedPrediction = false; ///< That retained state matched the authoritative ack state.
        bool recoveryTriggered = false;              ///< Replay failed and presentation fell back to authority.
        bool recoveryRestarted = false;              ///< Recovery was already active and restarted from a newer unresolved suffix.
        bool recoveryResolved = false;               ///< Recovery finished and normal local prediction resumed.
        bool recoveryStillActive = false;            ///< Presented local state is still authoritative after this correction.

        uint32_t replayedInputs = 0;                 ///< Retained local inputs replayed after the acked input seq.
        uint32_t missingInputHistory = 0;            ///< Length of the first missing retained suffix that blocked replay.
        uint32_t remainingDeferredInputs = 0;        ///< Retained local inputs still waiting behind the authoritative cursor.
        uint32_t recoveryCatchUpSeq = 0;             ///< Highest local input seq authority must catch up to before replay may resume.
        int32_t deltaXQ = 0;                         ///< Authoritative minus predicted X delta at the acked input seq, in tile-Q8.
        int32_t deltaYQ = 0;                         ///< Authoritative minus predicted Y delta at the acked input seq, in tile-Q8.
        uint32_t deltaManhattanQ = 0;                ///< Manhattan size of that correction delta, in tile-Q8.
    };

    /**
     * @brief Aggregate prediction stats for diagnostics.
     *
     * These counters are session-local and reflect behavior.
     * Deferred inputs include both the pre-baseline buffer and inputs retained while recovery is active.
     */
    struct PredictionStats
    {
        uint32_t localInputsApplied = 0;          ///< Inputs simulated immediately.
        uint32_t localInputsDeferred = 0;         ///< Inputs retained but not simulated yet.
        uint32_t rejectedLocalInputs = 0;         ///< Inputs rejected for invalid or non-contiguous sequence progression.

        uint32_t correctionsApplied = 0;               ///< Newer authoritative corrections consumed by the helper.
        uint32_t correctionsWithRetainedPredictedState = 0; ///< Corrections whose acked sequence still had retained predicted state.
        uint32_t correctionsMismatched = 0;            ///< Corrections whose authoritative ack state differed from retained prediction.

        uint32_t totalCorrectionDeltaQ = 0;       ///< Sum of correction Manhattan deltas over corrections with retained predicted state.
        uint32_t maxCorrectionDeltaQ = 0;         ///< Largest correction Manhattan delta seen so far.

        uint32_t totalReplayedInputs = 0;         ///< Sum of replayed retained inputs across all correction applications.
        uint32_t maxReplayedInputs = 0;           ///< Largest replay suffix rebuilt from one correction.

        uint32_t replayTruncations = 0;           ///< Replay attempts that fell back to recovery because retained input history was incomplete.
        uint32_t totalMissingInputHistory = 0;    ///< Sum of missing retained suffix sizes that caused replay truncation.
        uint32_t maxMissingInputHistory = 0;      ///< Largest missing retained suffix observed during replay.
        uint32_t recoveryActivations = 0;         ///< Transitions from active prediction into authoritative recovery mode.
        uint32_t recoveryResolutions = 0;         ///< Times recovery replay succeeded and prediction resumed.
    };

    // ----- Client Prediction Helper -----

    /** @brief Client-side local input prediction and reconciliation. */
    class ClientPrediction
    {
      private:
        enum class PredictionPhase : uint8_t
        {
            AwaitingBaseline,
            Active,
            Recovering
        };

      public:
        ClientPrediction() = default;

        // ----- Public API -----

        /** @brief Clears all prediction state and history. */
        void reset() noexcept;
        /** @brief Suspends prediction, drops retained local history, and waits for a later correction baseline to re-arm. */
        void suspend() noexcept;

        /** @brief Returns true once the first authoritative baseline has been accepted. */
        [[nodiscard]]
        bool isInitialized() const noexcept { return phase_ != PredictionPhase::AwaitingBaseline; }

        /** @brief Returns the local state currently being presented for the owning player. */
        [[nodiscard]]
        const LocalPlayerState& currentState() const noexcept { return currentState_; }

        /** @brief Returns aggregate prediction diagnostics for the current session. */
        [[nodiscard]]
        const PredictionStats& stats() const noexcept { return stats_; }

        /** @brief Returns the highest contiguous local input seq seen so far. */
        [[nodiscard]]
        uint32_t lastRecordedInputSeq() const noexcept { return lastRecordedInputSeq_; }

        /** @brief Records one new local input and simulates it immediately only while prediction is active. */
        bool applyLocalInput(uint32_t inputSeq, uint8_t buttons, const sim::TileMap& map) noexcept;

        /** @brief Applies an authoritative correction and replays retained local inputs after it. */
        [[nodiscard]]
        CorrectionReplayResult reconcileAndReplay(const MsgCorrection& correction, const sim::TileMap& map) noexcept;

      private:
        // ----- History entries -----

        struct InputHistoryEntry
        {
            uint32_t seq = 0;     ///< Absolute local input sequence stored in this ring slot.
            uint8_t buttons = 0;  ///< Button bitmask recorded for `seq`.
            bool valid = false;   ///< True only while this slot still represents `seq`.
        };

        struct StateHistoryEntry
        {
            uint32_t seq = 0;          ///< Absolute input sequence whose post-state is stored here.
            LocalPlayerState state{};  ///< Retained local-player post-state immediately after applying `seq`.
            bool valid = false;        ///< True only while this slot still represents `seq`.
        };

        [[nodiscard]]
        static constexpr std::size_t historySlot(uint32_t seq) noexcept
        {
            return seq & (kClientPredictionHistorySize - 1u);
        }

        // ----- Correction helpers -----

        /** @brief Replaces the presented local state with the correction baseline. */
        void setCurrentAuthoritativeState(sim::TilePos posQ,
                                          uint8_t buttons = 0,
                                          uint8_t playerFlags = 0) noexcept;
        /** @brief Switches prediction into authoritative recovery mode after replay failed. */
        void enterRecoveryFromReplayFailure(sim::TilePos authoritativePosQ,
                                            uint32_t lastProcessedInputSeq,
                                            uint8_t authoritativePlayerFlags,
                                            CorrectionReplayResult& result,
                                            bool wasRecovering) noexcept;
        /** @brief Applies a correction baseline and replays the retained suffix after it. */
        [[nodiscard]]
        bool replayFromAuthoritativeBaseline(uint32_t lastProcessedInputSeq,
                                             sim::TilePos authoritativePosQ,
                                             uint8_t authoritativeButtons,
                                             uint8_t authoritativePlayerFlags,
                                             const sim::TileMap& map,
                                             CorrectionReplayResult& result) noexcept;
        /** @brief Handles the first authoritative correction that seeds prediction. */
        void handleAwaitingBaselineCorrection(uint32_t lastProcessedInputSeq,
                                              sim::TilePos authoritativePosQ,
                                              uint8_t authoritativePlayerFlags,
                                              const sim::TileMap& map,
                                              CorrectionReplayResult& result) noexcept;
        /** @brief Handles a correction while recovery is waiting for authority to catch up. */
        void handleRecoveringCorrection(uint32_t lastProcessedInputSeq,
                                        sim::TilePos authoritativePosQ,
                                        uint8_t authoritativePlayerFlags,
                                        const sim::TileMap& map,
                                        CorrectionReplayResult& result) noexcept;
        /** @brief Handles a correction during normal active prediction. */
        void handleActiveCorrection(uint32_t lastProcessedInputSeq,
                                    sim::TilePos authoritativePosQ,
                                    uint8_t authoritativeButtons,
                                    uint8_t authoritativePlayerFlags,
                                    const sim::TileMap& map,
                                    CorrectionReplayResult& result) noexcept;
        /** @brief Measures correction delta against retained predicted state at the acked seq. */
        void measureCorrectionAtAck(uint32_t lastProcessedInputSeq,
                                    sim::TilePos authoritativePosQ,
                                    uint8_t& predictedButtonsAtAck,
                                    CorrectionReplayResult& result) noexcept;
        /** @brief Returns the retained predicted post-state for `seq`, if still available. */
        [[nodiscard]]
        bool tryGetPredictedStateAtSeq(uint32_t inputSeq, LocalPlayerState& outState) const noexcept;

        // ----- History storage -----

        /** @brief Clears both retained input history and retained predicted post-state history. */
        void clearHistory() noexcept;
        /** @brief Stores one contiguous local input in the prediction input ring. */
        void storeInputHistory(uint32_t seq, uint8_t buttons) noexcept;
        /** @brief Stores one predicted post-state keyed by its input sequence. */
        void storeStateHistory(uint32_t seq, const LocalPlayerState& state) noexcept;
        /** @brief Invalidates retained predicted post-states in the inclusive sequence range. */
        void invalidateStateHistoryRange(uint32_t firstSeq, uint32_t lastSeq) noexcept;
        /** @brief Discards retained history at or before the latest authoritative ack. */
        void discardAcknowledgedHistory(uint32_t lastProcessedInputSeq) noexcept;
        /** @brief Counts retained local inputs strictly newer than `inputSeq`. */
        [[nodiscard]]
        uint32_t countRetainedInputSuffixAfter(uint32_t inputSeq) const noexcept;
        /** @brief Replays the retained contiguous local input suffix after `lastProcessedInputSeq`. */
        [[nodiscard]]
        bool replayRetainedInputSuffixAfter(uint32_t lastProcessedInputSeq,
                                            const sim::TileMap& map,
                                            CorrectionReplayResult& result) noexcept;

        // ----- Ring lookups and simulation -----

        /** @brief Returns the retained local input entry for `seq`, or `nullptr` if unavailable. */
        [[nodiscard]]
        const InputHistoryEntry* findInputHistory(uint32_t seq) const noexcept;

        /** @brief Returns the retained predicted post-state entry for `seq`, or `nullptr` if unavailable. */
        [[nodiscard]]
        const StateHistoryEntry* findStateHistory(uint32_t seq) const noexcept;

        /** @brief Runs one local movement step from `baseState` using the shared movement sim. */
        [[nodiscard]]
        LocalPlayerState simulateNextStateFromInput(const LocalPlayerState& baseState,
                                                    uint8_t buttons,
                                                    const sim::TileMap& map) const noexcept;

        // ----- State -----

        std::array<InputHistoryEntry, kClientPredictionHistorySize> inputHistory_{};
        std::array<StateHistoryEntry, kClientPredictionHistorySize> stateHistory_{};

        LocalPlayerState currentState_{};  ///< Local state currently being presented for the owning player.
        PredictionStats stats_{};
        uint32_t lastRecordedInputSeq_ = 0; ///< Highest contiguous local input sequence retained so far.
        uint32_t lastAuthoritativeInputSeq_ = 0; ///< Highest authoritative input sequence accepted from corrections.
        uint32_t recoveryCatchUpSeq_ = 0;   ///< Highest locally retained seq authority must reach before replay may resume safely.
        PredictionPhase phase_ = PredictionPhase::AwaitingBaseline; ///< Current prediction phase.
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_CLIENTPREDICTION_H
