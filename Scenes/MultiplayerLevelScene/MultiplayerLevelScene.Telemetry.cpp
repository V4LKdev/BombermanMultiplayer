/** @file MultiplayerLevelScene.Telemetry.cpp
 *  @brief Telemetry, debug HUD, and connection-health logic.
 *  @ingroup multiplayer_level_scene
 */

#include "Scenes/MultiplayerLevelScene/MultiplayerLevelSceneInternal.h"

#include <iomanip>
#include <sstream>

#include "Entities/Text.h"
#include "Game.h"
#include "Net/NetClient.h"
#include "Util/Log.h"

namespace bomberman
{
    using namespace multiplayer_level_scene_internal;

    namespace
    {
        [[nodiscard]]
        std::string formatLossPercent(const uint32_t lossPermille)
        {
            std::ostringstream out;
            out << std::fixed << std::setprecision(1)
                << (static_cast<double>(lossPermille) / 10.0);
            return out.str();
        }
    } // namespace

    // =============================================================================================================
    // ===== Connection Health ======================================================================================
    // =============================================================================================================

    void MultiplayerLevelScene::updateGameplayConnectionHealth(const net::NetClient& netClient)
    {
        if (!gameplayUnlocked_)
        {
            setGameplayConnectionDegraded(false, 0);
            return;
        }

        const uint32_t silenceMs = netClient.gameplaySilenceMs();
        setGameplayConnectionDegraded(silenceMs >= kGameplayDegradedThresholdMs, silenceMs);
    }

    void MultiplayerLevelScene::setGameplayConnectionDegraded(const bool degraded, const uint32_t silenceMs)
    {
        if (gameplayConnectionDegraded_ == degraded)
            return;

        gameplayConnectionDegraded_ = degraded;

        if (gameplayConnectionDegraded_)
        {
            LOG_NET_CONN_WARN("Gameplay updates silent for {}ms - connection degraded, waiting for gameplay updates",
                              silenceMs);

            if (!gameplayStatusText_)
            {
                auto font = game->getAssetManager()->getFont(16);
                gameplayStatusText_ =
                    std::make_shared<Text>(font, game->getRenderer(), "WAITING FOR GAMEPLAY UPDATES");
                gameplayStatusText_->fitToContent();
                gameplayStatusText_->setColor(SDL_Color{0xFF, 0xD1, 0x66, 0xFF});
                gameplayStatusText_->attachToCamera(false);
                gameplayStatusText_->setPosition(
                    game->getWindowWidth() / 2 - gameplayStatusText_->getWidth() / 2,
                    kGameplayStatusOffsetY);
                addObject(gameplayStatusText_);
            }
            return;
        }

        LOG_NET_CONN_INFO("Gameplay updates resumed - connection recovered");
        if (gameplayStatusText_)
        {
            removeObject(gameplayStatusText_);
            gameplayStatusText_.reset();
        }
    }

    // =============================================================================================================
    // ===== Prediction Telemetry ===================================================================================
    // =============================================================================================================

    void MultiplayerLevelScene::logLivePredictionTelemetry(const unsigned int delta)
    {
        const auto& stats = localPrediction_.stats();
        if (game->isPredictionEnabled())
        {
            updateMaxPendingInputDepth();
        }

        if (auto* netClient = game->getNetClient(); netClient != nullptr)
        {
            netClient->updateLivePredictionStats(
                game->isPredictionEnabled() &&
                    shouldProcessOwnerPrediction() &&
                    localPrediction_.isInitialized() &&
                    !livePredictionTelemetry_.recoveryActive,
                livePredictionTelemetry_.recoveryActive,
                stats.correctionsApplied,
                stats.correctionsMismatched,
                livePredictionTelemetry_.lastCorrectionDeltaQ,
                livePredictionTelemetry_.maxPendingInputDepth);
        }

        if (!game->isPredictionEnabled() || !shouldProcessOwnerPrediction())
            return;

        if (stats.localInputsApplied == 0 &&
            stats.localInputsDeferred == 0 &&
            stats.rejectedLocalInputs == 0 &&
            stats.correctionsApplied == 0)
        {
            return;
        }

        livePredictionLogAccumulatorMs_ += delta;
        if (livePredictionLogAccumulatorMs_ < kLivePredictionLogIntervalMs)
            return;

        livePredictionLogAccumulatorMs_ = 0;

        auto* logger = bomberman::log::netInput();
        if (logger == nullptr || !logger->should_log(spdlog::level::debug))
            return;

        const uint32_t pendingDepth = pendingInputDepth();
        logger->log(
            spdlog::level::debug,
            "Prediction live ackSeq={} corrTick={} pendingDepth={} pendingAgeMs={} lastDeltaQ={} lastReplay={} "
            "lastMissingInputs={} remainingDeferredInputs={} recoveryActive={} recoveryCatchUpSeq={} maxPendingDepth={}",
            livePredictionTelemetry_.lastAckedInputSeq,
            livePredictionTelemetry_.lastCorrectionServerTick,
            pendingDepth,
            pendingDepth * kSimulationTickMs,
            livePredictionTelemetry_.lastCorrectionDeltaQ,
            livePredictionTelemetry_.lastReplayCount,
            livePredictionTelemetry_.lastMissingInputs,
            livePredictionTelemetry_.lastRemainingDeferredInputs,
            livePredictionTelemetry_.recoveryActive ? "yes" : "no",
            livePredictionTelemetry_.recoveryCatchUpSeq,
            livePredictionTelemetry_.maxPendingInputDepth);
    }

    void MultiplayerLevelScene::updateMaxPendingInputDepth()
    {
        livePredictionTelemetry_.maxPendingInputDepth =
            std::max(livePredictionTelemetry_.maxPendingInputDepth, pendingInputDepth());
    }

    uint32_t MultiplayerLevelScene::pendingInputDepth() const
    {
        const uint32_t lastRecorded = localPrediction_.lastRecordedInputSeq();
        if (lastRecorded <= livePredictionTelemetry_.lastAckedInputSeq)
            return 0;

        return lastRecorded - livePredictionTelemetry_.lastAckedInputSeq;
    }

    void MultiplayerLevelScene::logPredictionSummary() const
    {
        if (!game->isPredictionEnabled())
            return;

        const auto& stats = localPrediction_.stats();
        if (stats.localInputsApplied == 0 &&
            stats.localInputsDeferred == 0 &&
            stats.rejectedLocalInputs == 0 &&
            stats.correctionsApplied == 0)
        {
            return;
        }

        auto* logger = bomberman::log::netInput();
        if (logger == nullptr || !logger->should_log(spdlog::level::debug))
            return;

        const double avgCorrectionDeltaQ =
            (stats.correctionsWithRetainedPredictedState > 0)
                ? static_cast<double>(stats.totalCorrectionDeltaQ) /
                    static_cast<double>(stats.correctionsWithRetainedPredictedState)
                : 0.0;

        logger->log(
            spdlog::level::debug,
            "Prediction summary localInputs={} deferredInputs={} rejectedInputs={} corrections={} mismatches={} "
            "avgDeltaQ={:.2f} maxDeltaQ={} replayedInputs={} maxReplay={} truncations={} recoveries={} "
            "recoveryResolutions={} maxMissingInputs={} maxPendingDepth={}",
            stats.localInputsApplied,
            stats.localInputsDeferred,
            stats.rejectedLocalInputs,
            stats.correctionsApplied,
            stats.correctionsMismatched,
            avgCorrectionDeltaQ,
            stats.maxCorrectionDeltaQ,
            stats.totalReplayedInputs,
            stats.maxReplayedInputs,
            stats.replayTruncations,
            stats.recoveryActivations,
            stats.recoveryResolutions,
            stats.maxMissingInputHistory,
            livePredictionTelemetry_.maxPendingInputDepth);
    }

    // =============================================================================================================
    // ===== Debug HUD ==============================================================================================
    // =============================================================================================================

    void MultiplayerLevelScene::ensureDebugHudPresentations()
    {
        if (debugHudNetText_)
            return;

        auto font = game->getAssetManager()->getFont(kDebugHudPointSize);
        debugHudNetText_ = std::make_shared<Text>(font, game->getRenderer(), "");
        debugHudNetText_->attachToCamera(false);
        addObject(debugHudNetText_);

#if defined(BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS) && BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS
        debugHudPredictionText_ = std::make_shared<Text>(font, game->getRenderer(), "");
        debugHudPredictionText_->attachToCamera(false);
        addObject(debugHudPredictionText_);

        debugHudSimulationText_ = std::make_shared<Text>(font, game->getRenderer(), "");
        debugHudSimulationText_->attachToCamera(false);
        addObject(debugHudSimulationText_);
#endif

        debugHudRefreshAccumulatorMs_ = kDebugHudRefreshIntervalMs;
    }

    void MultiplayerLevelScene::updateDebugHud(const unsigned int delta)
    {
        const auto* netClient = game ? game->getNetClient() : nullptr;
        if (netClient == nullptr)
            return;

        ensureDebugHudPresentations();
        debugHudRefreshAccumulatorMs_ += delta;
        if (debugHudRefreshAccumulatorMs_ < kDebugHudRefreshIntervalMs)
            return;

        debugHudRefreshAccumulatorMs_ = 0;

        const auto& live = netClient->liveStats();
        const uint8_t playerId = netClient->playerId();
        const std::string playerLabel =
            playerId == net::NetClient::kInvalidPlayerId
                ? std::string("?")
                : std::to_string(playerId);

        std::ostringstream netLine;
#if defined(BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS) && BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS
        netLine << "[NET] RTT " << live.rttMs << "ms +/-" << live.rttVarianceMs
                << " loss " << formatLossPercent(live.lossPermille) << "% player=" << playerLabel
                << " proto=" << net::kProtocolVersion;
#else
        netLine << "Ping " << live.rttMs << "ms   Loss " << formatLossPercent(live.lossPermille) << "%";
#endif
        debugHudNetText_->setText(netLine.str());
        debugHudNetText_->fitToContent();
        debugHudNetText_->setPosition(kDebugHudOffsetX, kDebugHudOffsetY);

#if defined(BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS) && BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS
        std::ostringstream predLine;
        predLine << "[PRED] "
                 << (live.predictionActive ? "active" : "inactive")
                 << " corr=" << live.correctionCount
                 << " mismatch=" << live.mismatchCount
                 << " recover=" << (live.recoveryActive ? 1 : 0)
                 << " last_d=" << live.lastCorrectionDeltaQ << "q"
                 << " max_pending=" << live.maxPendingInputDepth;
        debugHudPredictionText_->setText(predLine.str());
        debugHudPredictionText_->fitToContent();
        debugHudPredictionText_->setPosition(
            kDebugHudOffsetX,
            kDebugHudOffsetY + debugHudNetText_->getHeight() + kDebugHudLineGapPx);

        std::ostringstream simLine;
        simLine << "[SIM] snap_tick=" << live.lastSnapshotTick
                << " corr_tick=" << live.lastCorrectionTick
                << " snap_age=" << live.snapshotAgeMs << "ms"
                << " silence=" << live.gameplaySilenceMs << "ms";
        debugHudSimulationText_->setText(simLine.str());
        debugHudSimulationText_->fitToContent();
        debugHudSimulationText_->setPosition(
            kDebugHudOffsetX,
            kDebugHudOffsetY +
                debugHudNetText_->getHeight() +
                kDebugHudLineGapPx +
                debugHudPredictionText_->getHeight() +
                kDebugHudLineGapPx);
#endif
    }

    void MultiplayerLevelScene::removeDebugHudPresentations()
    {
        if (debugHudNetText_)
        {
            removeObject(debugHudNetText_);
            debugHudNetText_.reset();
        }

        if (debugHudPredictionText_)
        {
            removeObject(debugHudPredictionText_);
            debugHudPredictionText_.reset();
        }

        if (debugHudSimulationText_)
        {
            removeObject(debugHudSimulationText_);
            debugHudSimulationText_.reset();
        }
    }
} // namespace bomberman
