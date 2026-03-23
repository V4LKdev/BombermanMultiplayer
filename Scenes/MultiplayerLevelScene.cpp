/**
 * @file MultiplayerLevelScene.cpp
 * @brief Multiplayer gameplay scene implementation.
 */

#include "Scenes/MultiplayerLevelScene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_set>

#include <SDL.h>

#include "Entities/Player.h"
#include "Entities/Text.h"
#include "Game.h"
#include "Net/NetClient.h"
#include "Sim/SimConfig.h"
#include "Util/Log.h"

namespace bomberman
{
    // =================================================================================================================
    // ===== Internal Helpers ==========================================================================================
    // =================================================================================================================

    namespace
    {
        struct PlayerColor
        {
            uint8_t r;
            uint8_t g;
            uint8_t b;
        };

        constexpr PlayerColor kPlayerColors[] = {
            {0xFF, 0x22, 0x22},
            {0x22, 0xFF, 0x22},
            {0x22, 0x88, 0xFF},
            {0xFF, 0xFF, 0x22},
        };

        constexpr std::size_t kColorCount = std::size(kPlayerColors);
        constexpr int kMovementDeltaThresholdQ = 2;
        constexpr int kNameTagOffsetPx = 6;
        constexpr int kNameTagMinPointSize = 12;
        constexpr int kNameTagMaxPointSize = 20;
        constexpr uint32_t kGameplayDegradedThresholdMs = 2000;
        constexpr int kGameplayStatusOffsetY = 12;
        constexpr uint32_t kLivePredictionLogIntervalMs = 1000;
        constexpr uint32_t kSimulationTickMs = 1000u / static_cast<uint32_t>(sim::kTickRate);

        bool snapshotEntryIsAlive(const net::MsgSnapshot::PlayerEntry& entry)
        {
            const auto flags = static_cast<uint8_t>(entry.flags);
            return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::Alive)) != 0;
        }

        PlayerColor colorForPlayerId(const uint8_t playerId)
        {
            return kPlayerColors[static_cast<std::size_t>(playerId) % kColorCount];
        }

        MovementDirection inferDirectionFromDelta(const int dxQ,
                                                  const int dyQ,
                                                  const MovementDirection fallback)
        {
            const int absDx = std::abs(dxQ);
            const int absDy = std::abs(dyQ);

            if(absDx < kMovementDeltaThresholdQ && absDy < kMovementDeltaThresholdQ)
                return fallback;

            if(absDx >= absDy)
                return dxQ >= 0 ? MovementDirection::Right : MovementDirection::Left;

            return dyQ >= 0 ? MovementDirection::Down : MovementDirection::Up;
        }

        int computeTagPointSize(const int scaledTileSize)
        {
            return std::clamp(scaledTileSize / 3, kNameTagMinPointSize, kNameTagMaxPointSize);
        }

        MovementDirection inferDirectionFromButtons(const uint8_t buttons)
        {
            const int8_t moveX = net::buttonsToMoveX(buttons);
            const int8_t moveY = net::buttonsToMoveY(buttons);

            if(moveX == 0 && moveY == 0)
                return MovementDirection::None;

            if(moveX != 0)
                return moveX > 0 ? MovementDirection::Right : MovementDirection::Left;

            return moveY > 0 ? MovementDirection::Down : MovementDirection::Up;
        }
    } // namespace


    MultiplayerLevelScene::MultiplayerLevelScene(Game* game, const unsigned int stage,
                                                 const unsigned int prevScore,
                                                 std::optional<uint32_t> mapSeed)
        : LevelScene(game, stage, prevScore, mapSeed)
    {
        initializeLevelWorld(mapSeed);
    }

    // =================================================================================================================
    // ===== Scene Hooks ===============================================================================================
    // =================================================================================================================

    void MultiplayerLevelScene::updateLevel(const unsigned int delta)
    {
        net::NetClient* netClient = requireConnectedNetClient();
        if(netClient == nullptr)
            return;

        /*
         * Runtime order:
         * 1) consume any newer owner correction first so local prediction owns local presentation.
         * 2) consume the newest snapshot for remote presentation and local bootstrap fallback.
         * 3) tick scene objects after state application, then finish tag/camera/diagnostic updates.
         */
        updateGameplayConnectionHealth(*netClient);
        applyLatestCorrectionIfAvailable(*netClient);
        applyLatestSnapshotIfAvailable();
        finalizeFrameUpdate(delta);
    }

    net::NetClient* MultiplayerLevelScene::requireConnectedNetClient()
    {
        net::NetClient* netClient = game->getNetClient();
        if(netClient == nullptr || netClient->connectState() != net::EConnectState::Connected)
        {
            const auto stateName =
                netClient ? net::connectStateName(netClient->connectState()) : std::string_view("NoClient");
            returnToMenu(false, stateName);
            return nullptr;
        }

        return netClient;
    }

    void MultiplayerLevelScene::applyLatestCorrectionIfAvailable(const net::NetClient& netClient)
    {
        if(!game->isPredictionEnabled())
            return;

        net::MsgCorrection correction{};
        if(netClient.tryGetLatestCorrection(correction) &&
           correction.serverTick > lastAppliedCorrectionTick_)
        {
            applyAuthoritativeCorrection(correction);
        }
    }

    void MultiplayerLevelScene::applyLatestSnapshotIfAvailable()
    {
        net::MsgSnapshot snapshot{};
        if(game->tryGetLatestSnapshot(snapshot))
        {
            applySnapshot(snapshot);
        }
    }

    void MultiplayerLevelScene::finalizeFrameUpdate(const unsigned int delta)
    {
        updateSceneObjects(delta);
        updateRemotePlayerPresentations(delta);
        updateLocalPlayerTagPosition();
        updateCamera();
        logLivePredictionTelemetry(delta);
    }

    void MultiplayerLevelScene::onExit()
    {
        logPredictionSummary();
        removeAllRemotePlayers();
        localPrediction_.reset();
        lastAppliedSnapshotTick_ = 0;
        lastAppliedCorrectionTick_ = 0;
        localPlayerId_.reset();
        livePredictionTelemetry_ = {};
        localFacingDirection_ = MovementDirection::Right;
        livePredictionLogAccumulatorMs_ = 0;
        gameplayConnectionDegraded_ = false;
        returningToMenu_ = false;

        if(localPlayerTag_)
        {
            removeObject(localPlayerTag_);
            localPlayerTag_.reset();
        }

        if(gameplayStatusText_)
        {
            removeObject(gameplayStatusText_);
            gameplayStatusText_.reset();
        }

        LevelScene::onExit();
    }

    void MultiplayerLevelScene::onKeyPressed(const SDL_Scancode scancode)
    {
        if(scancode != SDL_SCANCODE_ESCAPE)
            return;

        returnToMenu(true, "LocalLeave");
    }

    // =================================================================================================================
    // ===== Local Input, Snapshot, and Correction Flow ================================================================
    // =================================================================================================================

    void MultiplayerLevelScene::onNetInputQueued(const uint32_t inputSeq, const uint8_t buttons)
    {
        if(!game->isPredictionEnabled())
        {
            updateLocalPresentationFromInputButtons(buttons);
            return;
        }

        if(!localPrediction_.applyLocalInput(inputSeq, buttons, tiles))
            return;

        updateMaxPendingInputDepth();

        if(!localPrediction_.isInitialized())
        {
            /*
             * Before the first owner correction arrives, keep local presentation responsive
             * without inventing local position ahead of the prediction baseline.
             */
            updateLocalPresentationFromInputButtons(buttons);
            return;
        }

        syncLocalPresentationFromOwnedState(localPrediction_.currentState());
    }

    void MultiplayerLevelScene::applySnapshot(const net::MsgSnapshot& snapshot)
    {
        if(!player)
            return;

        if(snapshot.serverTick <= lastAppliedSnapshotTick_)
            return;

        const net::NetClient* netClient = game->getNetClient();
        if(!netClient)
            return;

        const uint8_t localId = netClient->playerId();
        ensureLocalPresentation(localId);

        /*
         * Snapshot player entries remain the authoritative membership source.
         * Local position only comes from snapshots while prediction is disabled or unarmed.
         */
        applySnapshotToRemotePlayers(snapshot, localId);
        lastAppliedSnapshotTick_ = snapshot.serverTick;
    }

    void MultiplayerLevelScene::applyAuthoritativeCorrection(const net::MsgCorrection& correction)
    {
        if(correction.serverTick <= lastAppliedCorrectionTick_)
            return;

        if(!localPrediction_.isInitialized())
        {
            LOG_NET_DIAG_INFO("Local prediction armed from authoritative state tick={} lastProcessed={} pos=({}, {})",
                              correction.serverTick,
                              correction.lastProcessedInputSeq,
                              correction.xQ,
                              correction.yQ);
        }

        const auto replayResult = localPrediction_.reconcileAndReplay(correction, tiles);
        if(replayResult.ignoredStaleCorrection)
        {
            LOG_NET_DIAG_DEBUG("Ignored stale local correction tick={} lastProcessed={} lastAppliedTick={}",
                               correction.serverTick,
                               correction.lastProcessedInputSeq,
                               lastAppliedCorrectionTick_);
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
        if(replayResult.missingInputHistory > 0)
        {
            LOG_NET_INPUT_WARN("Prediction replay truncated tick={} lastProcessed={} missingInputs={}",
                               correction.serverTick,
                               correction.lastProcessedInputSeq,
                               replayResult.missingInputHistory);
        }
        if(replayResult.recoveryTriggered)
        {
            if(replayResult.recoveryRestarted)
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
        if(replayResult.recoveryResolved)
        {
            LOG_NET_DIAG_INFO("Prediction recovery resolved tick={} lastProcessed={}",
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

    void MultiplayerLevelScene::updateGameplayConnectionHealth(const net::NetClient& netClient)
    {
        const uint32_t silenceMs = netClient.gameplaySilenceMs();
        setGameplayConnectionDegraded(silenceMs >= kGameplayDegradedThresholdMs, silenceMs);
    }

    void MultiplayerLevelScene::setGameplayConnectionDegraded(const bool degraded, const uint32_t silenceMs)
    {
        if(gameplayConnectionDegraded_ == degraded)
            return;

        gameplayConnectionDegraded_ = degraded;

        if(gameplayConnectionDegraded_)
        {
            LOG_NET_CONN_WARN("Gameplay updates silent for {}ms - connection degraded, waiting for gameplay updates",
                              silenceMs);

            if(!gameplayStatusText_)
            {
                auto font = game->getAssetManager()->getFont(16);
                gameplayStatusText_ =
                    std::make_shared<Text>(font, game->getRenderer(), "WAITING FOR GAMEPLAY UPDATES");
                gameplayStatusText_->fitToContent();
                gameplayStatusText_->setColor(SDL_Color{0xFF, 0xD1, 0x66, 0xFF});
                gameplayStatusText_->setPosition(
                    game->getWindowWidth() / 2 - gameplayStatusText_->getWidth() / 2,
                    kGameplayStatusOffsetY);
                addObject(gameplayStatusText_);
            }
            return;
        }

        LOG_NET_CONN_INFO("Gameplay updates resumed - connection recovered");
        if(gameplayStatusText_)
        {
            removeObject(gameplayStatusText_);
            gameplayStatusText_.reset();
        }
    }

    void MultiplayerLevelScene::logLivePredictionTelemetry(const unsigned int delta)
    {
        if(!game->isPredictionEnabled())
            return;

        const auto& stats = localPrediction_.stats();
        if(stats.localInputsApplied == 0 &&
           stats.localInputsDeferred == 0 &&
           stats.rejectedLocalInputs == 0 &&
           stats.correctionsApplied == 0)
            return;

        livePredictionLogAccumulatorMs_ += delta;
        if(livePredictionLogAccumulatorMs_ < kLivePredictionLogIntervalMs)
            return;

        livePredictionLogAccumulatorMs_ = 0;
        updateMaxPendingInputDepth();

        const uint32_t pendingDepth = pendingInputDepth();
        const uint32_t pendingAgeMs = pendingDepth * kSimulationTickMs;
        LOG_NET_DIAG_DEBUG(
            "Prediction live ackSeq={} corrTick={} pendingDepth={} pendingAgeMs={} lastDeltaQ={} lastReplay={} "
            "lastMissingInputs={} remainingDeferredInputs={} recoveryActive={} recoveryCatchUpSeq={} maxPendingDepth={}",
            livePredictionTelemetry_.lastAckedInputSeq,
            livePredictionTelemetry_.lastCorrectionServerTick,
            pendingDepth,
            pendingAgeMs,
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
        if(lastRecorded <= livePredictionTelemetry_.lastAckedInputSeq)
            return 0;

        return lastRecorded - livePredictionTelemetry_.lastAckedInputSeq;
    }

    void MultiplayerLevelScene::logPredictionSummary() const
    {
        if(!game->isPredictionEnabled())
            return;

        const auto& stats = localPrediction_.stats();
        if(stats.localInputsApplied == 0 &&
           stats.localInputsDeferred == 0 &&
           stats.rejectedLocalInputs == 0 &&
           stats.correctionsApplied == 0)
            return;

        const double avgCorrectionDeltaQ =
            (stats.correctionsWithRetainedPredictedState > 0)
                ? static_cast<double>(stats.totalCorrectionDeltaQ) /
                    static_cast<double>(stats.correctionsWithRetainedPredictedState)
                : 0.0;

        LOG_NET_DIAG_INFO(
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

    void MultiplayerLevelScene::applyBootstrapLocalSnapshot(const net::MsgSnapshot::PlayerEntry& entry)
    {
        /*
         * Before the first owner correction arrives, keep local position authoritative from
         * snapshots while leaving facing and animation driven by the latest local input buttons.
         */
        playerPos_.xQ = entry.xQ;
        playerPos_.yQ = entry.yQ;
        syncPlayerSpriteToSimPosition();
    }

    void MultiplayerLevelScene::updateLocalPresentationFromInputButtons(const uint8_t buttons)
    {
        if(!player)
            return;

        // This helper is presentation-only.
        const bool isMoving = (net::buttonsToMoveX(buttons) != 0) || (net::buttonsToMoveY(buttons) != 0);
        if(isMoving)
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
        if(!player)
            return;

        /*
         * Once prediction is initialized, the predicted or recovery state is the source of truth
         * for local presentation and camera anchoring.
         */
        playerPos_ = localState.posQ;
        syncPlayerSpriteToSimPosition();

        const bool isMoving = (net::buttonsToMoveX(localState.buttons) != 0) ||
                              (net::buttonsToMoveY(localState.buttons) != 0);
        if(isMoving)
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

    void MultiplayerLevelScene::ensureLocalPresentation(const uint8_t localId)
    {
        if(!player)
            return;

        if(!localPlayerId_.has_value() || localPlayerId_.value() != localId)
        {
            localPlayerId_ = localId;

            const PlayerColor color = colorForPlayerId(localId);
            player->setColorMod(color.r, color.g, color.b);

            if(localPlayerTag_)
            {
                localPlayerTag_->setText(formatPlayerTag(localId));
                localPlayerTag_->fitToContent();
                localPlayerTag_->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});
            }
        }

        if(!localPlayerTag_)
        {
            const int pointSize = computeTagPointSize(scaledTileSize);
            auto font = game->getAssetManager()->getFont(pointSize);
            localPlayerTag_ = std::make_shared<Text>(font, game->getRenderer(), formatPlayerTag(localId));
            localPlayerTag_->fitToContent();

            const PlayerColor color = colorForPlayerId(localId);
            localPlayerTag_->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});

            addObject(localPlayerTag_);
        }
    }

    void MultiplayerLevelScene::applyAuthoritativeLocalSnapshot(const net::MsgSnapshot::PlayerEntry& entry)
    {
        if(game->isPredictionEnabled())
        {
            if(!localPrediction_.isInitialized())
                applyBootstrapLocalSnapshot(entry);
            return;
        }

        /*
         * Without prediction, keep local position authoritative from snapshots but let
         * direct local input own facing and animation for responsive local presentation.
         */
        playerPos_.xQ = entry.xQ;
        playerPos_.yQ = entry.yQ;
        syncPlayerSpriteToSimPosition();
    }

    void MultiplayerLevelScene::updateLocalPlayerTagPosition()
    {
        if(!player || !localPlayerTag_)
            return;

        const int tagX = player->getPositionX() + player->getWidth() / 2 - localPlayerTag_->getWidth() / 2;
        const int tagY = player->getPositionY() - localPlayerTag_->getHeight() - kNameTagOffsetPx;
        localPlayerTag_->setPosition(tagX, tagY);
    }

    // =================================================================================================================
    // ===== Remote Player Presentation ===============================================================================
    // =================================================================================================================

    void MultiplayerLevelScene::applySnapshotToRemotePlayers(const net::MsgSnapshot& snapshot,
                                                             const uint8_t localId)
    {
        std::unordered_set<uint8_t> seenRemoteIds;

        for(uint8_t i = 0; i < snapshot.playerCount; ++i)
        {
            const auto& entry = snapshot.players[i];
            const bool alive = snapshotEntryIsAlive(entry);

            if(entry.playerId == localId)
            {
                /*
                 * Local entries flow through dedicated local ownership rules rather than the
                 * remote snapshot and interpolation path.
                 */
                if(alive)
                {
                    applyAuthoritativeLocalSnapshot(entry);
                }
                continue;
            }

            if(!alive)
                continue;

            seenRemoteIds.insert(entry.playerId);
            updateOrCreateRemotePlayer(entry.playerId, entry.xQ, entry.yQ, snapshot.serverTick);
        }

        pruneMissingRemotePlayers(seenRemoteIds);
    }

    void MultiplayerLevelScene::updateOrCreateRemotePlayer(const uint8_t playerId,
                                                           const int16_t xQ,
                                                           const int16_t yQ,
                                                           const uint32_t snapshotTick)
    {
        auto [it, inserted] = remotePlayerPresentations_.try_emplace(playerId);
        RemotePlayerPresentation& presentation = it->second;

        if(inserted)
        {
            presentation.playerSprite =
                std::make_shared<Player>(game->getAssetManager()->getTexture(Texture::Player), game->getRenderer());
            presentation.playerSprite->setSize(scaledTileSize, scaledTileSize);
            presentation.playerSprite->setMovementDirection(MovementDirection::None);

            const PlayerColor color = colorForPlayerId(playerId);
            presentation.playerSprite->setColorMod(color.r, color.g, color.b);

            const int pointSize = computeTagPointSize(scaledTileSize);
            auto font = game->getAssetManager()->getFont(pointSize);
            presentation.playerTag = std::make_shared<Text>(font, game->getRenderer(), formatPlayerTag(playerId));
            presentation.playerTag->fitToContent();
            presentation.playerTag->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});

            addObject(presentation.playerSprite);
            addObject(presentation.playerTag);
        }

        recordSnapshotSample(presentation, xQ, yQ, snapshotTick);
        updateRemoteAnimationFromSnapshotDelta(presentation);

        presentation.authoritativePosQ.xQ = xQ;
        presentation.authoritativePosQ.yQ = yQ;

        const sim::TilePos presentedPosQ = computeRemotePresentedPosition(presentation);
        const int screenX = sim::tileQToScreenTopLeft(presentedPosQ.xQ, fieldPositionX, scaledTileSize, 0);
        const int screenY = sim::tileQToScreenTopLeft(presentedPosQ.yQ, fieldPositionY, scaledTileSize, 0);
        presentation.playerSprite->setPosition(screenX, screenY);

        updateRemotePlayerTagPosition(presentation);
    }

    void MultiplayerLevelScene::pruneMissingRemotePlayers(const std::unordered_set<uint8_t>& seenRemoteIds)
    {
        for(auto it = remotePlayerPresentations_.begin(); it != remotePlayerPresentations_.end();)
        {
            if(seenRemoteIds.contains(it->first))
            {
                ++it;
                continue;
            }

            if(it->second.playerSprite)
                removeObject(it->second.playerSprite);
            if(it->second.playerTag)
                removeObject(it->second.playerTag);

            it = remotePlayerPresentations_.erase(it);
        }
    }

    void MultiplayerLevelScene::removeAllRemotePlayers()
    {
        for(auto& entry : remotePlayerPresentations_)
        {
            auto& presentation = entry.second;
            if(presentation.playerSprite)
                removeObject(presentation.playerSprite);
            if(presentation.playerTag)
                removeObject(presentation.playerTag);
        }
        remotePlayerPresentations_.clear();
    }

    void MultiplayerLevelScene::recordSnapshotSample(RemotePlayerPresentation& presentation,
                                                     const int16_t xQ,
                                                     const int16_t yQ,
                                                     const uint32_t serverTick)
    {
        presentation.previousSnapshot = presentation.latestSnapshot;
        presentation.latestSnapshot.posQ = {xQ, yQ};
        presentation.latestSnapshot.serverTick = serverTick;
        presentation.latestSnapshot.valid = true;
        presentation.ticksSinceLatestSnapshot = 0.0f;
        presentation.receivedSnapshotThisUpdate = true;
    }

    void MultiplayerLevelScene::updateRemoteAnimationFromSnapshotDelta(RemotePlayerPresentation& presentation)
    {
        if(!presentation.latestSnapshot.valid || !presentation.playerSprite)
            return;

        if(!presentation.previousSnapshot.valid)
        {
            presentation.playerSprite->setMovementDirection(MovementDirection::None);
            return;
        }

        const int dxQ = presentation.latestSnapshot.posQ.xQ - presentation.previousSnapshot.posQ.xQ;
        const int dyQ = presentation.latestSnapshot.posQ.yQ - presentation.previousSnapshot.posQ.yQ;
        const int absDx = std::abs(dxQ);
        const int absDy = std::abs(dyQ);

        const bool isMovingFromSnapshots =
            (absDx >= kMovementDeltaThresholdQ) || (absDy >= kMovementDeltaThresholdQ);
        if(!isMovingFromSnapshots)
        {
            presentation.playerSprite->setMovementDirection(MovementDirection::None);
            return;
        }

        presentation.facingDirection = inferDirectionFromDelta(dxQ, dyQ, presentation.facingDirection);
        presentation.playerSprite->setMovementDirection(presentation.facingDirection);
    }

    void MultiplayerLevelScene::updateRemotePlayerPresentations(const unsigned int delta)
    {
        const float tickDelta =
            static_cast<float>(delta) / static_cast<float>(kSimulationTickMs);

        for(auto& entry : remotePlayerPresentations_)
        {
            auto& presentation = entry.second;
            if(!presentation.playerSprite)
                continue;

            if(presentation.receivedSnapshotThisUpdate)
            {
                /*
                 * Keep freshly received samples at alpha=0 for one update so interpolation
                 * blends from previous to latest instead of snapping immediately.
                 */
                presentation.receivedSnapshotThisUpdate = false;
            }
            else
            {
                presentation.ticksSinceLatestSnapshot += tickDelta;
            }

            const sim::TilePos presentedPosQ = computeRemotePresentedPosition(presentation);

            const int screenX = sim::tileQToScreenTopLeft(presentedPosQ.xQ, fieldPositionX, scaledTileSize, 0);
            const int screenY = sim::tileQToScreenTopLeft(presentedPosQ.yQ, fieldPositionY, scaledTileSize, 0);
            presentation.playerSprite->setPosition(screenX, screenY);

            updateRemotePlayerTagPosition(presentation);
        }
    }

    sim::TilePos MultiplayerLevelScene::computeRemotePresentedPosition(
        const RemotePlayerPresentation& presentation) const
    {
        if(!game->isRemoteSmoothingEnabled())
            return presentation.authoritativePosQ;

        if(!presentation.previousSnapshot.valid || !presentation.latestSnapshot.valid)
            return presentation.authoritativePosQ;

        const uint32_t observedSnapshotSpacingTicks =
            (presentation.latestSnapshot.serverTick > presentation.previousSnapshot.serverTick)
                ? (presentation.latestSnapshot.serverTick - presentation.previousSnapshot.serverTick)
                : 0u;

        if(observedSnapshotSpacingTicks == 0)
            return presentation.authoritativePosQ;

        /*
         * This path is interpolation only. Remote players remain snapshot-authoritative,
         * and the scene avoids extrapolation or dead reckoning here.
         *
         * TODO: If observed snapshot spacing becomes too noisy under loss, send the nominal
         * snapshot interval to clients during handshake.
         */
        const float alpha = std::clamp(
            presentation.ticksSinceLatestSnapshot / static_cast<float>(observedSnapshotSpacingTicks),
            0.0f,
            1.0f);

        sim::TilePos presentedPosQ{};
        presentedPosQ.xQ = static_cast<int32_t>(std::lround(
            static_cast<double>(presentation.previousSnapshot.posQ.xQ) +
            static_cast<double>(presentation.latestSnapshot.posQ.xQ - presentation.previousSnapshot.posQ.xQ) * alpha));
        presentedPosQ.yQ = static_cast<int32_t>(std::lround(
            static_cast<double>(presentation.previousSnapshot.posQ.yQ) +
            static_cast<double>(presentation.latestSnapshot.posQ.yQ - presentation.previousSnapshot.posQ.yQ) * alpha));

        return presentedPosQ;
    }

    void MultiplayerLevelScene::updateRemotePlayerTagPosition(RemotePlayerPresentation& presentation)
    {
        if(!presentation.playerTag || !presentation.playerSprite)
            return;

        const int tagX = presentation.playerSprite->getPositionX() + presentation.playerSprite->getWidth() / 2 -
                         presentation.playerTag->getWidth() / 2;
        const int tagY = presentation.playerSprite->getPositionY() - presentation.playerTag->getHeight() -
                         kNameTagOffsetPx;
        presentation.playerTag->setPosition(tagX, tagY);
    }

    std::string MultiplayerLevelScene::formatPlayerTag(const uint8_t playerId)
    {
        /* Player IDs are zero-indexed, but the player-facing label is 1-indexed. */
        return "P" + std::to_string(static_cast<unsigned int>(playerId) + 1u);
    }

    // =================================================================================================================
    // ===== Leave and Teardown =======================================================================================
    // =================================================================================================================

    void MultiplayerLevelScene::returnToMenu(const bool disconnectClient, const std::string_view reason)
    {
        if(returningToMenu_)
            return;

        returningToMenu_ = true;

        if(disconnectClient)
        {
            LOG_NET_CONN_INFO("Leaving multiplayer level and disconnecting: {}", reason);
            /* The scene switches immediately; ENet disconnect completion finishes asynchronously. */
            game->disconnectNetClientIfActive(false);
        }
        else
        {
            LOG_NET_CONN_WARN("Multiplayer level lost connection (state={}) - returning to menu", reason);
        }

        game->getSceneManager()->activateScene("menu");
        game->getSceneManager()->removeScene("level");
    }
} // namespace bomberman
