/**
 * @file MultiplayerLevelScene.Authority.cpp
 * @brief MultiplayerLevelScene authority, prediction, and merge logic.
 */

#include "Scenes/MultiplayerLevelSceneInternal.h"

#include <limits>

#include "Entities/Text.h"
#include "Game.h"
#include "Net/NetClient.h"
#include "Util/Log.h"

namespace bomberman
{
    using namespace multiplayer_level_scene_internal;

    void MultiplayerLevelScene::consumeAuthoritativeNetState(net::NetClient& netClient)
    {
        applyLatestCorrectionIfAvailable(netClient);
        collectPendingGameplayEvents(netClient);

        net::MsgSnapshot snapshot{};
        const bool hasSnapshot = game->tryGetLatestSnapshot(snapshot);

        while (true)
        {
            const uint32_t nextSnapshotTick =
                (hasSnapshot &&
                 snapshot.serverTick > lastAppliedSnapshotTick_ &&
                 snapshot.serverTick > lastAppliedGameplayEventTick_)
                    ? snapshot.serverTick
                    : std::numeric_limits<uint32_t>::max();

            const uint32_t nextGameplayEventTick =
                pendingGameplayEvents_.empty()
                    ? std::numeric_limits<uint32_t>::max()
                    : gameplayEventServerTick(pendingGameplayEvents_.front());

            if (nextSnapshotTick == std::numeric_limits<uint32_t>::max() &&
                nextGameplayEventTick == std::numeric_limits<uint32_t>::max())
            {
                break;
            }

            if (nextGameplayEventTick < nextSnapshotTick)
            {
                applyNextPendingGameplayEvent();
                continue;
            }

            applySnapshot(snapshot);

            if (nextGameplayEventTick == nextSnapshotTick)
            {
                while (!pendingGameplayEvents_.empty() &&
                       gameplayEventServerTick(pendingGameplayEvents_.front()) == nextSnapshotTick)
                {
                    applyNextPendingGameplayEvent();
                }
            }
        }
    }

    void MultiplayerLevelScene::applyLatestCorrectionIfAvailable(const net::NetClient& netClient)
    {
        if (!game->isPredictionEnabled() || !shouldProcessOwnerPrediction())
            return;

        net::MsgCorrection correction{};
        if (netClient.tryGetLatestCorrection(correction) &&
            correction.serverTick > lastAppliedCorrectionTick_)
        {
            applyAuthoritativeCorrection(correction);
        }
    }

    void MultiplayerLevelScene::collectPendingGameplayEvents(net::NetClient& netClient)
    {
        net::NetClient::GameplayEvent gameplayEvent{};
        while (netClient.tryDequeueGameplayEvent(gameplayEvent))
        {
            pendingGameplayEvents_.push_back(gameplayEvent);
        }
    }

    void MultiplayerLevelScene::applyNextPendingGameplayEvent()
    {
        if (pendingGameplayEvents_.empty())
            return;

        const net::NetClient::GameplayEvent gameplayEvent = pendingGameplayEvents_.front();
        pendingGameplayEvents_.pop_front();

        switch (gameplayEvent.type)
        {
            case net::NetClient::GameplayEvent::EType::BombPlaced:
                applyBombPlacedEvent(gameplayEvent.bombPlaced);
                break;
            case net::NetClient::GameplayEvent::EType::ExplosionResolved:
                applyExplosionResolvedEvent(gameplayEvent.explosionResolved);
                break;
        }
    }

    void MultiplayerLevelScene::onNetInputQueued(const uint32_t inputSeq, const uint8_t buttons)
    {
        if (!localPlayerAlive_ || localPlayerInputLocked_)
            return;

        if (!game->isPredictionEnabled())
        {
            updateLocalPresentationFromInputButtons(buttons);
            return;
        }

        if (!localPrediction_.applyLocalInput(inputSeq, buttons, tiles))
            return;

        updateMaxPendingInputDepth();

        if (!localPrediction_.isInitialized())
        {
            // Keep local presentation responsive until owner corrections establish a baseline.
            updateLocalPresentationFromInputButtons(buttons);
            return;
        }

        syncLocalPresentationFromOwnedState(localPrediction_.currentState());
    }

    void MultiplayerLevelScene::applySnapshot(const net::MsgSnapshot& snapshot)
    {
        if (!player)
            return;

        if (snapshot.serverTick <= lastAppliedSnapshotTick_)
            return;

        if (snapshot.serverTick <= lastAppliedGameplayEventTick_)
        {
            LOG_NET_SNAPSHOT_DEBUG(
                "Skipping stale gameplay snapshot tick={} lastSnapshotTick={} lastGameplayEventTick={}",
                snapshot.serverTick,
                lastAppliedSnapshotTick_,
                lastAppliedGameplayEventTick_);
            return;
        }

        const net::NetClient* netClient = game->getNetClient();
        if (!netClient)
            return;

        const uint8_t localId = netClient->playerId();
        ensureLocalPresentation(localId);

        applySnapshotToRemotePlayers(snapshot, localId);
        applySnapshotBombs(snapshot);
        lastAppliedSnapshotTick_ = snapshot.serverTick;
    }

    void MultiplayerLevelScene::seedLocalSpawnFromAssignedPlayerId()
    {
        const net::NetClient* netClient = game->getNetClient();
        if (!netClient)
            return;

        const uint8_t playerId = netClient->playerId();
        if (playerId == net::NetClient::kInvalidPlayerId)
            return;

        setLocalPlayerPositionQ(sim::spawnTilePosForPlayerId(playerId));
    }

    void MultiplayerLevelScene::applyAuthoritativeCorrection(const net::MsgCorrection& correction)
    {
        if (correction.serverTick <= lastAppliedCorrectionTick_)
            return;

        if (!localPrediction_.isInitialized())
        {
            LOG_NET_INPUT_DEBUG("Local prediction armed from authoritative state tick={} lastProcessed={} pos=({}, {})",
                                correction.serverTick,
                                correction.lastProcessedInputSeq,
                                correction.xQ,
                                correction.yQ);
        }

        const auto replayResult = localPrediction_.reconcileAndReplay(correction, tiles);
        if (replayResult.ignoredStaleCorrection)
        {
            if (!currentMatchResult_.has_value())
            {
                LOG_NET_INPUT_DEBUG("Ignored stale local correction tick={} lastProcessed={} lastAppliedTick={}",
                                    correction.serverTick,
                                    correction.lastProcessedInputSeq,
                                    lastAppliedCorrectionTick_);
            }

            livePredictionTelemetry_.lastCorrectionServerTick = correction.serverTick;
            lastAppliedCorrectionTick_ = correction.serverTick;
            return;
        }

        logCorrectionReplayOutcome(correction, replayResult);
        syncLocalPresentationFromOwnedState(localPrediction_.currentState());
        storeCorrectionTelemetry(correction, replayResult);
        updateMaxPendingInputDepth();
        lastAppliedCorrectionTick_ = correction.serverTick;
    }

    void MultiplayerLevelScene::logCorrectionReplayOutcome(
        const net::MsgCorrection& correction,
        const net::CorrectionReplayResult& replayResult)
    {
        if (replayResult.missingInputHistory > 0)
        {
            LOG_NET_INPUT_WARN("Prediction replay truncated tick={} lastProcessed={} missingInputs={}",
                               correction.serverTick,
                               correction.lastProcessedInputSeq,
                               replayResult.missingInputHistory);
        }

        if (replayResult.recoveryTriggered)
        {
            if (replayResult.recoveryRestarted)
            {
                LOG_NET_INPUT_WARN(
                    "Prediction recovery retruncated tick={} lastProcessed={} remainingDeferredInputs={} recoveryCatchUpSeq={}",
                    correction.serverTick,
                    correction.lastProcessedInputSeq,
                    replayResult.remainingDeferredInputs,
                    replayResult.recoveryCatchUpSeq);
            }
            else
            {
                LOG_NET_INPUT_WARN(
                    "Prediction recovery active tick={} lastProcessed={} remainingDeferredInputs={} recoveryCatchUpSeq={}",
                    correction.serverTick,
                    correction.lastProcessedInputSeq,
                    replayResult.remainingDeferredInputs,
                    replayResult.recoveryCatchUpSeq);
            }
        }

        if (replayResult.recoveryResolved)
        {
            LOG_NET_INPUT_INFO("Prediction recovery resolved tick={} lastProcessed={}",
                               correction.serverTick,
                               correction.lastProcessedInputSeq);
        }
    }

    void MultiplayerLevelScene::storeCorrectionTelemetry(
        const net::MsgCorrection& correction,
        const net::CorrectionReplayResult& replayResult)
    {
        livePredictionTelemetry_.lastAckedInputSeq = correction.lastProcessedInputSeq;
        livePredictionTelemetry_.lastCorrectionServerTick = correction.serverTick;
        livePredictionTelemetry_.lastCorrectionDeltaQ = replayResult.deltaManhattanQ;
        livePredictionTelemetry_.lastReplayCount = replayResult.replayedInputs;
        livePredictionTelemetry_.lastMissingInputs = replayResult.missingInputHistory;
        livePredictionTelemetry_.lastRemainingDeferredInputs = replayResult.remainingDeferredInputs;
        livePredictionTelemetry_.recoveryActive = replayResult.recoveryStillActive;
        livePredictionTelemetry_.recoveryCatchUpSeq = replayResult.recoveryCatchUpSeq;
    }

    void MultiplayerLevelScene::logLivePredictionTelemetry(const unsigned int delta)
    {
        if (!game->isPredictionEnabled() || !shouldProcessOwnerPrediction())
            return;

        const auto& stats = localPrediction_.stats();
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
        updateMaxPendingInputDepth();

#ifndef NDEBUG
        const uint32_t pendingDepth = pendingInputDepth();
        LOG_NET_INPUT_DEBUG(
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
#endif
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

#ifndef NDEBUG
        const double avgCorrectionDeltaQ =
            (stats.correctionsWithRetainedPredictedState > 0)
                ? static_cast<double>(stats.totalCorrectionDeltaQ) /
                    static_cast<double>(stats.correctionsWithRetainedPredictedState)
                : 0.0;

        LOG_NET_INPUT_DEBUG(
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
#endif
    }

    bool MultiplayerLevelScene::shouldProcessOwnerPrediction() const
    {
        return matchStarted_ &&
               gameplayUnlocked_ &&
               localPlayerAlive_ &&
               !localPlayerInputLocked_ &&
               !currentMatchResult_.has_value() &&
               !returningToMenu_;
    }

    bool MultiplayerLevelScene::shouldOwnLocalStateFromPrediction() const
    {
        return game->isPredictionEnabled() &&
               shouldProcessOwnerPrediction() &&
               localPrediction_.isInitialized();
    }

    void MultiplayerLevelScene::applyBootstrapLocalSnapshot(const net::MsgSnapshot::PlayerEntry& entry)
    {
        playerPos_.xQ = entry.xQ;
        playerPos_.yQ = entry.yQ;
        syncPlayerSpriteToSimPosition();
    }

    void MultiplayerLevelScene::updateLocalPresentationFromInputButtons(const uint8_t buttons)
    {
        if (!player)
            return;

        const bool isMoving = (net::buttonsToMoveX(buttons) != 0) || (net::buttonsToMoveY(buttons) != 0);
        if (isMoving)
        {
            localFacingDirection_ = inferDirectionFromButtons(buttons);
            player->setMovementDirection(localFacingDirection_);
        }
        else
        {
            player->setMovementDirection(MovementDirection::None);
        }
    }

    void MultiplayerLevelScene::syncLocalPresentationFromOwnedState(const net::LocalPlayerState& localState)
    {
        if (!player || !localPlayerAlive_)
            return;

        playerPos_ = localState.posQ;
        syncPlayerSpriteToSimPosition();

        const bool isMoving = (net::buttonsToMoveX(localState.buttons) != 0) ||
                              (net::buttonsToMoveY(localState.buttons) != 0);
        if (isMoving)
        {
            localFacingDirection_ = inferDirectionFromButtons(localState.buttons);
            player->setMovementDirection(localFacingDirection_);
        }
        else
        {
            player->setMovementDirection(MovementDirection::None);
        }
    }

    void MultiplayerLevelScene::updateSceneObjects(const unsigned int delta)
    {
        Scene::update(delta);
    }

    uint32_t MultiplayerLevelScene::currentAuthoritativeGameplayTick(const net::NetClient& netClient) const
    {
        return std::max(netClient.lastSnapshotTick(), netClient.lastCorrectionTick());
    }

    void MultiplayerLevelScene::applyAuthoritativeLocalSnapshot(const net::MsgSnapshot::PlayerEntry& entry)
    {
        if (shouldOwnLocalStateFromPrediction())
        {
            return;
        }

        applyBootstrapLocalSnapshot(entry);
    }

    void MultiplayerLevelScene::setLocalPlayerAlivePresentation(const bool alive)
    {
        localPlayerAlive_ = alive;

        if (!player)
            return;

        player->setVisible(alive);
        if (localPlayerTag_)
        {
            localPlayerTag_->setVisible(alive);
        }

        if (!alive)
        {
            player->setMovementDirection(MovementDirection::None);
        }
    }

    void MultiplayerLevelScene::setLocalPlayerInputLock(const bool locked)
    {
        const bool wasLocked = localPlayerInputLocked_;
        localPlayerInputLocked_ = locked;

        if (!player)
            return;

        if (localPlayerInputLocked_)
        {
            if (!wasLocked && game->isPredictionEnabled())
            {
                localPrediction_.suspend();
            }
            player->setMovementDirection(MovementDirection::None);
        }
    }
} // namespace bomberman
