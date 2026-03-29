/** @file MultiplayerLevelScene.Authority.cpp
 *  @brief Authoritative state merge and local prediction logic.
 *  @ingroup multiplayer_level_scene
 */

#include "Scenes/MultiplayerLevelScene/MultiplayerLevelSceneInternal.h"

#include <limits>

#include "Entities/Text.h"
#include "Game.h"
#include "Net/Client/NetClient.h"
#include "Util/Log.h"

namespace bomberman
{
    using namespace multiplayer_level_scene_internal;

    // =============================================================================================================
    // ===== Authority Merge ========================================================================================
    // =============================================================================================================

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
                if (!shouldApplyGameplayEvent(gameplayEvent.explosionResolved.serverTick, "ExplosionResolved"))
                    break;
                applyExplosionResolvedTiles(gameplayEvent.explosionResolved);
                applyExplosionResolvedEvent(gameplayEvent.explosionResolved);
                break;
        }
    }

    // =============================================================================================================
    // ===== Local Input and Prediction =============================================================================
    // =============================================================================================================

    void MultiplayerLevelScene::onNetInputQueued(const uint32_t inputSeq, const uint8_t buttons)
    {
        const bool bombHeldNow = (buttons & net::kInputBomb) != 0;
        const bool bombPressedNow = bombHeldNow && !localBombHeldOnLastQueuedInput_;
        localBombHeldOnLastQueuedInput_ = bombHeldNow;

        if (!allowsLocalGameplayInput())
        {
            return;
        }

        if (bombPressedNow)
        {
            if (canSpawnLocalBombSparkPresentation())
            {
                spawnLocalBombSparkPresentation();
            }
        }

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
            updateLocalPresentationFromInputButtons(buttons);
            return;
        }

        syncLocalPresentationFromOwnedState(localPrediction_.currentState());
    }

    // =============================================================================================================
    // ===== Snapshot and Correction Application ====================================================================
    // =============================================================================================================

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
        applySnapshotPowerups(snapshot);
        lastAppliedSnapshotTick_ = snapshot.serverTick;
    }

    bool MultiplayerLevelScene::shouldApplyGameplayEvent(const uint32_t gameplayEventTick,
                                                         const char* const gameplayEventName) const
    {
        if (gameplayEventTick <= lastAppliedGameplayEventTick_)
        {
            LOG_NET_SNAPSHOT_DEBUG(
                "Dropping stale {} tick={} because a newer gameplay event already applied lastGameplayEventTick={}",
                gameplayEventName,
                gameplayEventTick,
                lastAppliedGameplayEventTick_);
            return false;
        }

        return true;
    }

    void MultiplayerLevelScene::applyExplosionResolvedTiles(const net::MsgExplosionResolved& explosion)
    {
        for (uint8_t i = 0; i < explosion.destroyedBrickCount; ++i)
        {
            const auto& brickCell = explosion.destroyedBricks[i];
            if (brickCell.row < tileArrayHeight &&
                brickCell.col < tileArrayWidth &&
                tiles[brickCell.row][brickCell.col] == Tile::Brick)
            {
                tiles[brickCell.row][brickCell.col] = Tile::Grass;
            }
        }
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

        // Correction flags refresh owner-local effect/loadout state only;
        // death/input-lock presentation still comes from snapshot/event authority.
        localPlayerEffectFlags_ = correction.playerFlags;
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

    bool MultiplayerLevelScene::shouldProcessOwnerPrediction() const
    {
        return allowsLocalGameplayInput();
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

    bool MultiplayerLevelScene::allowsLocalGameplayInput() const
    {
        // Gameplay is locked during the pre-match lobby, after a match result until returning to menu, and on local death.
        return matchStarted_ &&
               gameplayUnlocked_ &&
               localPlayerAlive_ &&
               !localPlayerInputLocked_ &&
               !currentMatchResult_.has_value() &&
               !returningToMenu_;
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
        if (!alive)
        {
            localBombHeldOnLastQueuedInput_ = false;
        }

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
        localPlayerInputLocked_ = locked;
        if (locked)
        {
            localBombHeldOnLastQueuedInput_ = false;
        }

        if (!player)
            return;

        if (localPlayerInputLocked_)
        {
            player->setMovementDirection(MovementDirection::None);
        }
    }
} // namespace bomberman
