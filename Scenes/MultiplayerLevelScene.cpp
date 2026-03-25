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
#include "Entities/Sprite.h"
#include "Entities/Text.h"
#include "Game.h"
#include "Net/NetClient.h"
#include "Scenes/LobbyScene.h"
#include "Sim/SimConfig.h"
#include "Sim/SpawnSlots.h"
#include "Util/Log.h"
#include "Util/PlayerColors.h"

namespace bomberman
{
    // =================================================================================================================
    // ===== Internal Helpers ==========================================================================================
    // =================================================================================================================

    namespace
    {
        static_assert(sim::kDefaultSpawnSlots.size() > 0, "Spawn slot table must not be empty");
        static_assert(net::kMaxPlayers <= util::kPlayerColorCount,
                      "Player color table must cover all supported multiplayer player ids");
        constexpr int kMovementDeltaThresholdQ = 2;
        constexpr int kNameTagOffsetPx = 6;
        constexpr int kNameTagMinPointSize = 12;
        constexpr int kNameTagMaxPointSize = 20;
        constexpr uint32_t kGameplayDegradedThresholdMs = 2000;
        constexpr int kGameplayStatusOffsetY = 12;
        constexpr int kCenterBannerPointSize = 56;
        constexpr int kCenterBannerDetailPointSize = 40;
        constexpr int kCenterBannerLineGapPx = 10;
        constexpr uint32_t kCenterBannerDurationTicks = static_cast<uint32_t>(sim::kTickRate);
        constexpr uint32_t kLivePredictionLogIntervalMs = 1000;
        constexpr uint32_t kSimulationTickMs = 1000u / static_cast<uint32_t>(sim::kTickRate);
        constexpr uint32_t kPreStartReturnTimeoutMs = 7000;
        constexpr int kBombAnimationFrameCount = 4;
        constexpr int kExplosionAnimationStartFrame = 1;
        constexpr int kExplosionAnimationFrameCount = 11;
        constexpr uint32_t kExplosionLifetimeMs = 800;

        uint16_t packCellKey(const uint8_t col, const uint8_t row)
        {
            return static_cast<uint16_t>((static_cast<uint16_t>(row) << 8u) | static_cast<uint16_t>(col));
        }

        void attachBombAnimation(const std::shared_ptr<Sprite>& bombSprite)
        {
            if(!bombSprite)
                return;

            auto animation = std::make_shared<Animation>();
            for(int frame = 0; frame < kBombAnimationFrameCount; ++frame)
            {
                animation->addAnimationEntity(AnimationEntity(tileSize * frame, 0, tileSize, tileSize));
            }

            animation->setSprite(bombSprite.get());
            bombSprite->addAnimation(animation);
            animation->play();
        }

        void attachExplosionAnimation(const std::shared_ptr<Sprite>& explosionSprite)
        {
            if(!explosionSprite)
                return;

            auto animation = std::make_shared<Animation>();
            for(int frame = 0; frame < kExplosionAnimationFrameCount; ++frame)
            {
                animation->addAnimationEntity(
                    AnimationEntity(tileSize * (kExplosionAnimationStartFrame + frame), 0, tileSize, tileSize));
            }

            animation->setSprite(explosionSprite.get());
            explosionSprite->addAnimation(animation);
            animation->play();
        }

        bool snapshotEntryIsAlive(const net::MsgSnapshot::PlayerEntry& entry)
        {
            const auto flags = static_cast<uint8_t>(entry.flags);
            return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::Alive)) != 0;
        }

        bool snapshotEntryInputLocked(const net::MsgSnapshot::PlayerEntry& entry)
        {
            const auto flags = static_cast<uint8_t>(entry.flags);
            return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::InputLocked)) != 0;
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
        initializeMultiplayerScenePresentation();
    }

    MultiplayerLevelScene::MultiplayerLevelScene(Game* game,
                                                 const unsigned int stage,
                                                 const unsigned int prevScore,
                                                 const net::MsgLevelInfo& levelInfo)
        : LevelScene(game, stage, prevScore, levelInfo.mapSeed)
    {
        initializeLevelWorld(levelInfo.mapSeed);
        initializeMultiplayerScenePresentation();
        matchId_ = levelInfo.matchId;
        matchBootstrapStartedMs_ = SDL_GetTicks();
        matchStarted_ = false;
        gameplayUnlocked_ = false;
        matchLoadedAckSent_ = false;
    }

    // =================================================================================================================
    // ===== Scene Hooks ===============================================================================================
    // =================================================================================================================

    bool MultiplayerLevelScene::wantsNetworkInputPolling() const
    {
        if (!matchStarted_ || !gameplayUnlocked_ || !localPlayerAlive_ || localPlayerInputLocked_ || returningToMenu_)
        {
            return false;
        }

        const net::NetClient* netClient = game ? game->getNetClient() : nullptr;
        if (netClient == nullptr)
        {
            return false;
        }

        net::MsgLobbyState lobbyState{};
        return !netClient->tryGetLatestLobbyState(lobbyState);
    }

    void MultiplayerLevelScene::updateLevel(const unsigned int delta)
    {
        net::NetClient* netClient = requireConnectedNetClient();
        if(netClient == nullptr)
            return;

        if(!updatePreStartFlow(*netClient))
            return;

        if(updateLobbyReturnFlow(*netClient))
            return;

        /*
         * Runtime order:
         * 1) consume any newer owner correction first so local prediction owns local presentation.
         * 2) consume the newest snapshot for remote presentation and local bootstrap fallback.
         * 3) apply any pending reliable gameplay events so discrete world changes win over stale snapshots.
         * 4) tick scene objects after state application, then finish tag/camera/diagnostic updates.
         */
        consumeAuthoritativeNetState(*netClient);
        updateMatchStartFlow(*netClient);
        updateMatchResultFlow(*netClient);
        updateDeathBannerFlow();
        if (matchStarted_ && !currentMatchResult_.has_value())
        {
            updateGameplayConnectionHealth(*netClient);
        }
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

    void MultiplayerLevelScene::initializeMultiplayerScenePresentation()
    {
        seedLocalSpawnFromAssignedPlayerId();
        if (const auto* netClient = game->getNetClient();
            netClient != nullptr && netClient->playerId() != net::NetClient::kInvalidPlayerId)
        {
            ensureLocalPresentation(netClient->playerId());
        }

        explosionSound_ = std::make_shared<Sound>(game->getAssetManager()->getSound(SoundEnum::Explosion));
    }

    void MultiplayerLevelScene::ensureMatchLoadedAckSent(net::NetClient& netClient)
    {
        if (matchStarted_ || matchLoadedAckSent_ || matchId_ == 0)
        {
            return;
        }

        if (netClient.sendMatchLoaded(matchId_))
        {
            matchLoadedAckSent_ = true;
            LOG_NET_CONN_DEBUG("Acknowledged match bootstrap loaded matchId={}", matchId_);
        }
    }

    bool MultiplayerLevelScene::updatePreStartFlow(net::NetClient& netClient)
    {
        if (matchStarted_)
        {
            return true;
        }

        ensureMatchLoadedAckSent(netClient);

        if (netClient.isMatchCancelled(matchId_))
        {
            returnToLobby("MatchCancelled");
            return false;
        }

        net::MsgMatchStart matchStart{};
        if (netClient.tryGetLatestMatchStart(matchStart) && matchStart.matchId == matchId_)
        {
            currentMatchStart_ = matchStart;
            matchStarted_ = true;
            gameplayUnlocked_ = false;
            goBannerHideTick_ = matchStart.goShowServerTick + kCenterBannerDurationTicks;
            LOG_NET_CONN_INFO("Match start confirmed locally matchId={}", matchId_);
        }

        if (matchBootstrapStartedMs_ != 0 &&
            SDL_GetTicks() - matchBootstrapStartedMs_ >= kPreStartReturnTimeoutMs)
        {
            returnToLobby("MatchStartTimedOut");
            return false;
        }

        return true;
    }

    void MultiplayerLevelScene::updateMatchStartFlow(net::NetClient& netClient)
    {
        if (!matchStarted_ || !currentMatchStart_.has_value() || currentMatchResult_.has_value())
        {
            return;
        }

        const uint32_t authoritativeTick = currentAuthoritativeGameplayTick(netClient);
        if (authoritativeTick == 0)
        {
            return;
        }

        if (!gameplayUnlocked_ && authoritativeTick >= currentMatchStart_->unlockServerTick)
        {
            gameplayUnlocked_ = true;
        }

        if (authoritativeTick >= currentMatchStart_->goShowServerTick &&
            authoritativeTick < goBannerHideTick_)
        {
            showCenterBanner("GO!", SDL_Color{0xFF, 0xD1, 0x66, 0xFF});
            return;
        }

        hideCenterBanner();
    }

    bool MultiplayerLevelScene::updateLobbyReturnFlow(net::NetClient& netClient)
    {
        net::MsgLobbyState lobbyState{};
        if (!netClient.tryGetLatestLobbyState(lobbyState))
        {
            return false;
        }

        returnToLobby(matchStarted_ ? "ReturnedToLobby" : "BootstrapCancelledToLobby");
        return true;
    }

    void MultiplayerLevelScene::updateMatchResultFlow(net::NetClient& netClient)
    {
        net::MsgMatchResult matchResult{};
        if (netClient.tryGetLatestMatchResult(matchResult) && matchResult.matchId == matchId_)
        {
            currentMatchResult_ = matchResult;
            gameplayUnlocked_ = false;
            setGameplayConnectionDegraded(false);
        }

        if (!currentMatchResult_.has_value())
        {
            return;
        }

        if (currentMatchResult_->result == net::MsgMatchResult::EResult::Draw)
        {
            showCenterBanner("DRAW!", SDL_Color{0xFF, 0xD1, 0x66, 0xFF});
            return;
        }

        showCenterBanner("WINS!",
                         net::matchResultWinnerName(*currentMatchResult_),
                         SDL_Color{0xFF, 0xD1, 0x66, 0xFF});
    }

    void MultiplayerLevelScene::updateDeathBannerFlow()
    {
        if(localPlayerAlive_ || currentMatchResult_.has_value())
            return;

        showCenterBanner("DEAD", SDL_Color{0xE8, 0x6A, 0x6A, 0xFF});
    }

    void MultiplayerLevelScene::consumeAuthoritativeNetState(net::NetClient& netClient)
    {
        applyLatestCorrectionIfAvailable(netClient);
        applyLatestSnapshotIfAvailable();
        applyPendingGameplayEvents(netClient);
    }

    void MultiplayerLevelScene::applyLatestCorrectionIfAvailable(const net::NetClient& netClient)
    {
        if(!game->isPredictionEnabled() || !shouldProcessOwnerPrediction())
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

    void MultiplayerLevelScene::applyPendingGameplayEvents(net::NetClient& netClient)
    {
        net::NetClient::GameplayEvent gameplayEvent{};
        while(netClient.tryDequeueGameplayEvent(gameplayEvent))
        {
            switch(gameplayEvent.type)
            {
                case net::NetClient::GameplayEvent::EType::BombPlaced:
                    applyBombPlacedEvent(gameplayEvent.bombPlaced);
                    break;
                case net::NetClient::GameplayEvent::EType::ExplosionResolved:
                    applyExplosionResolvedEvent(gameplayEvent.explosionResolved);
                    break;
            }
        }
    }

    void MultiplayerLevelScene::finalizeFrameUpdate(const unsigned int delta)
    {
        updateSceneObjects(delta);
        updateExplosionPresentations(delta);
        updateRemotePlayerPresentations(delta);
        updateLocalPlayerTagPosition();
        updateCamera();
        logLivePredictionTelemetry(delta);
    }

    void MultiplayerLevelScene::onExit()
    {
        logPredictionSummary();
        removeAllRemotePlayers();
        removeAllSnapshotBombs();
        localPrediction_.reset();
        lastAppliedSnapshotTick_ = 0;
        lastAppliedCorrectionTick_ = 0;
        lastAppliedGameplayEventTick_ = 0;
        localPlayerId_.reset();
        matchId_ = 0;
        matchBootstrapStartedMs_ = 0;
        matchStarted_ = true;
        gameplayUnlocked_ = true;
        matchLoadedAckSent_ = true;
        goBannerHideTick_ = 0;
        currentMatchStart_.reset();
        currentMatchResult_.reset();
        localPlayerAlive_ = true;
        localPlayerInputLocked_ = false;
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

        hideCenterBanner();

        removeAllExplosionPresentations();
        brickPresentations_.clear();

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
        if(!localPlayerAlive_)
            return;
        if(localPlayerInputLocked_)
            return;

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

        if(snapshot.serverTick < lastAppliedGameplayEventTick_)
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
        applySnapshotBombs(snapshot);
        lastAppliedSnapshotTick_ = snapshot.serverTick;
    }

    void MultiplayerLevelScene::seedLocalSpawnFromAssignedPlayerId()
    {
        const net::NetClient* netClient = game->getNetClient();
        if(!netClient)
            return;

        const uint8_t playerId = netClient->playerId();
        if(playerId == net::NetClient::kInvalidPlayerId)
            return;

        setLocalPlayerPositionQ(sim::spawnTilePosForPlayerId(playerId));
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
            if(!currentMatchResult_.has_value())
            {
                LOG_NET_DIAG_DEBUG("Ignored stale local correction tick={} lastProcessed={} lastAppliedTick={}",
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
                gameplayStatusText_->attachToCamera(false);
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

        if(!shouldProcessOwnerPrediction())
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

    bool MultiplayerLevelScene::shouldProcessOwnerPrediction() const
    {
        return matchStarted_ &&
               gameplayUnlocked_ &&
               localPlayerAlive_ &&
               !currentMatchResult_.has_value() &&
               !returningToMenu_;
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
        if(!player || !localPlayerAlive_)
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

    uint32_t MultiplayerLevelScene::currentAuthoritativeGameplayTick(const net::NetClient& netClient) const
    {
        return std::max(netClient.lastSnapshotTick(), netClient.lastCorrectionTick());
    }

    void MultiplayerLevelScene::showCenterBanner(const std::string_view message, const SDL_Color color)
    {
        showCenterBanner(message, {}, color);
    }

    void MultiplayerLevelScene::showCenterBanner(const std::string_view mainMessage,
                                                 const std::string_view detailMessage,
                                                 const SDL_Color color)
    {
        if (!centerBannerText_)
        {
            auto font = game->getAssetManager()->getFont(kCenterBannerPointSize);
            centerBannerText_ = std::make_shared<Text>(font, game->getRenderer(), std::string(mainMessage));
            centerBannerText_->attachToCamera(false);
            addObject(centerBannerText_);
        }
        else
        {
            centerBannerText_->setText(std::string(mainMessage));
        }

        centerBannerText_->fitToContent();
        centerBannerText_->setColor(color);

        if (!detailMessage.empty())
        {
            if (!centerBannerDetailText_)
            {
                auto detailFont = game->getAssetManager()->getFont(kCenterBannerDetailPointSize);
                centerBannerDetailText_ =
                    std::make_shared<Text>(detailFont, game->getRenderer(), std::string(detailMessage));
                centerBannerDetailText_->attachToCamera(false);
                addObject(centerBannerDetailText_);
            }
            else
            {
                centerBannerDetailText_->setText(std::string(detailMessage));
            }

            centerBannerDetailText_->fitToContent();
            centerBannerDetailText_->setColor(color);
        }
        else if (centerBannerDetailText_)
        {
            removeObject(centerBannerDetailText_);
            centerBannerDetailText_.reset();
        }

        const int totalHeight = centerBannerDetailText_
            ? centerBannerDetailText_->getHeight() + kCenterBannerLineGapPx + centerBannerText_->getHeight()
            : centerBannerText_->getHeight();
        const int topY = game->getWindowHeight() / 2 - totalHeight / 2;

        if (centerBannerDetailText_)
        {
            centerBannerDetailText_->setPosition(
                game->getWindowWidth() / 2 - centerBannerDetailText_->getWidth() / 2,
                topY);
            centerBannerText_->setPosition(
                game->getWindowWidth() / 2 - centerBannerText_->getWidth() / 2,
                topY + centerBannerDetailText_->getHeight() + kCenterBannerLineGapPx);
            return;
        }

        centerBannerText_->setPosition(game->getWindowWidth() / 2 - centerBannerText_->getWidth() / 2,
                                       game->getWindowHeight() / 2 - centerBannerText_->getHeight() / 2);
    }

    void MultiplayerLevelScene::hideCenterBanner()
    {
        if (centerBannerText_)
        {
            removeObject(centerBannerText_);
            centerBannerText_.reset();
        }

        if (centerBannerDetailText_)
        {
            removeObject(centerBannerDetailText_);
            centerBannerDetailText_.reset();
        }
    }

    void MultiplayerLevelScene::ensureLocalPresentation(const uint8_t localId)
    {
        if(!player)
            return;

        if(!localPlayerId_.has_value() || localPlayerId_.value() != localId)
        {
            localPlayerId_ = localId;

            const util::PlayerColor color = util::colorForPlayerId(localId);
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

            const util::PlayerColor color = util::colorForPlayerId(localId);
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

    void MultiplayerLevelScene::setLocalPlayerAlivePresentation(const bool alive)
    {
        localPlayerAlive_ = alive;

        if(!player)
            return;

        player->setVisible(alive);
        if(localPlayerTag_)
        {
            localPlayerTag_->setVisible(alive);
        }

        if(alive)
            return;

        player->setMovementDirection(MovementDirection::None);
    }

    void MultiplayerLevelScene::setLocalPlayerInputLock(const bool locked)
    {
        const bool wasLocked = localPlayerInputLocked_;
        localPlayerInputLocked_ = locked;

        if(!player)
            return;

        if(localPlayerInputLocked_)
        {
            if(!wasLocked && game->isPredictionEnabled())
            {
                localPrediction_.suspend();
            }
            player->setMovementDirection(MovementDirection::None);
        }
    }

    void MultiplayerLevelScene::updateLocalPlayerTagPosition()
    {
        if(!player || !localPlayerTag_ || !localPlayerAlive_)
            return;

        const int tagX = player->getPositionX() + player->getWidth() / 2 - localPlayerTag_->getWidth() / 2;
        const int tagY = player->getPositionY() - localPlayerTag_->getHeight() - kNameTagOffsetPx;
        localPlayerTag_->setPosition(tagX, tagY);
    }

    void MultiplayerLevelScene::removeRemotePlayerPresentation(const uint8_t playerId)
    {
        const auto it = remotePlayerPresentations_.find(playerId);
        if(it == remotePlayerPresentations_.end())
            return;

        if(it->second.playerSprite)
            removeObject(it->second.playerSprite);
        if(it->second.playerTag)
            removeObject(it->second.playerTag);

        remotePlayerPresentations_.erase(it);
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
            const bool inputLocked = snapshotEntryInputLocked(entry);

            if(entry.playerId == localId)
            {
                /*
                 * Local entries flow through dedicated local ownership rules rather than the
                 * remote snapshot and interpolation path.
                 */
                setLocalPlayerAlivePresentation(alive);
                setLocalPlayerInputLock(inputLocked);
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

    void MultiplayerLevelScene::applySnapshotBombs(const net::MsgSnapshot& snapshot)
    {
        std::unordered_set<uint16_t> seenBombCells;
        seenBombCells.reserve(snapshot.bombCount);

        for(uint8_t i = 0; i < snapshot.bombCount; ++i)
        {
            const auto& entry = snapshot.bombs[i];
            const uint16_t bombCellKey = packCellKey(entry.col, entry.row);
            seenBombCells.insert(bombCellKey);
            updateOrCreateBombPresentation(entry);
        }

        pruneMissingBombPresentations(seenBombCells);
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

            const util::PlayerColor color = util::colorForPlayerId(playerId);
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

    void MultiplayerLevelScene::updateOrCreateBombPresentation(const net::MsgSnapshot::BombEntry& entry)
    {
        const uint16_t bombCellKey = packCellKey(entry.col, entry.row);
        auto [it, inserted] = bombPresentations_.try_emplace(bombCellKey);
        BombPresentation& presentation = it->second;

        if(inserted)
        {
            presentation.bombSprite =
                std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Bomb), game->getRenderer());
            presentation.bombSprite->setSize(scaledTileSize, scaledTileSize);
            attachBombAnimation(presentation.bombSprite);
            insertObject(presentation.bombSprite, backgroundObjectLastNumber);
        }

        presentation.ownerId = entry.ownerId;
        presentation.radius = entry.radius;

        const util::PlayerColor color = util::colorForPlayerId(entry.ownerId);
        presentation.bombSprite->setColorMod(color.r, color.g, color.b);

        const int screenX = fieldPositionX + static_cast<int>(entry.col) * scaledTileSize;
        const int screenY = fieldPositionY + static_cast<int>(entry.row) * scaledTileSize;
        presentation.bombSprite->setPosition(screenX, screenY);
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

    void MultiplayerLevelScene::pruneMissingBombPresentations(const std::unordered_set<uint16_t>& seenBombCells)
    {
        for(auto it = bombPresentations_.begin(); it != bombPresentations_.end();)
        {
            if(seenBombCells.contains(it->first))
            {
                ++it;
                continue;
            }

            if(it->second.bombSprite)
                removeObject(it->second.bombSprite);

            it = bombPresentations_.erase(it);
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

    void MultiplayerLevelScene::removeAllSnapshotBombs()
    {
        for(auto& entry : bombPresentations_)
        {
            auto& presentation = entry.second;
            if(presentation.bombSprite)
                removeObject(presentation.bombSprite);
        }

        bombPresentations_.clear();
    }

    void MultiplayerLevelScene::applyBombPlacedEvent(const net::MsgBombPlaced& bombPlaced)
    {
        if(bombPlaced.serverTick < lastAppliedGameplayEventTick_)
            return;

        net::MsgSnapshot::BombEntry entry{};
        entry.ownerId = bombPlaced.ownerId;
        entry.col = bombPlaced.col;
        entry.row = bombPlaced.row;
        entry.radius = bombPlaced.radius;
        updateOrCreateBombPresentation(entry);
        lastAppliedGameplayEventTick_ = std::max(lastAppliedGameplayEventTick_, bombPlaced.serverTick);
    }

    void MultiplayerLevelScene::applyExplosionResolvedEvent(const net::MsgExplosionResolved& explosion)
    {
        if(explosion.serverTick < lastAppliedGameplayEventTick_)
            return;

        removeBombPresentation(explosion.originCol, explosion.originRow);

        for(uint8_t i = 0; i < explosion.destroyedBrickCount; ++i)
        {
            const auto& brickCell = explosion.destroyedBricks[i];
            destroyBrickPresentation(brickCell.col, brickCell.row);
        }

        for(uint8_t i = 0; i < explosion.blastCellCount; ++i)
        {
            spawnExplosionPresentation(explosion.blastCells[i]);
        }

        if(explosionSound_)
        {
            explosionSound_->play();
        }

        for(uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
        {
            const uint8_t playerBit = static_cast<uint8_t>(1u << playerId);
            if((explosion.killedPlayerMask & playerBit) == 0)
                continue;

            if(localPlayerId_.has_value() && localPlayerId_.value() == playerId)
            {
                setLocalPlayerInputLock(true);
                setLocalPlayerAlivePresentation(false);
                continue;
            }

            removeRemotePlayerPresentation(playerId);
        }

        lastAppliedGameplayEventTick_ = std::max(lastAppliedGameplayEventTick_, explosion.serverTick);
    }

    void MultiplayerLevelScene::destroyBrickPresentation(const uint8_t col, const uint8_t row)
    {
        const uint16_t cellKey = packCellKey(col, row);
        const auto it = brickPresentations_.find(cellKey);
        if(it != brickPresentations_.end())
        {
            if(it->second)
                removeObject(it->second);
            brickPresentations_.erase(it);
        }

        if(row < tileArrayHeight && col < tileArrayWidth && tiles[row][col] == Tile::Brick)
        {
            tiles[row][col] = Tile::Grass;
        }
    }

    void MultiplayerLevelScene::removeBombPresentation(const uint8_t col, const uint8_t row)
    {
        const uint16_t bombCellKey = packCellKey(col, row);
        const auto it = bombPresentations_.find(bombCellKey);
        if(it == bombPresentations_.end())
            return;

        if(it->second.bombSprite)
            removeObject(it->second.bombSprite);
        bombPresentations_.erase(it);
    }

    void MultiplayerLevelScene::spawnExplosionPresentation(const net::MsgCell& cell)
    {
        auto explosionSprite =
            std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Explosion), game->getRenderer());
        explosionSprite->setSize(scaledTileSize, scaledTileSize);
        attachExplosionAnimation(explosionSprite);

        const int screenX = fieldPositionX + static_cast<int>(cell.col) * scaledTileSize;
        const int screenY = fieldPositionY + static_cast<int>(cell.row) * scaledTileSize;
        explosionSprite->setPosition(screenX, screenY);
        addObject(explosionSprite);

        explosionPresentations_.push_back({explosionSprite, kExplosionLifetimeMs});
    }

    void MultiplayerLevelScene::updateExplosionPresentations(const unsigned int delta)
    {
        for(auto it = explosionPresentations_.begin(); it != explosionPresentations_.end();)
        {
            auto& presentation = *it;
            if(presentation.remainingLifetimeMs > delta)
            {
                presentation.remainingLifetimeMs -= delta;
                ++it;
                continue;
            }

            if(presentation.explosionSprite)
                removeObject(presentation.explosionSprite);

            it = explosionPresentations_.erase(it);
        }
    }

    void MultiplayerLevelScene::removeAllExplosionPresentations()
    {
        for(auto& presentation : explosionPresentations_)
        {
            if(presentation.explosionSprite)
                removeObject(presentation.explosionSprite);
        }

        explosionPresentations_.clear();
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

    void MultiplayerLevelScene::onCollisionObjectSpawned(const Tile tile, const std::shared_ptr<Object>& object)
    {
        if(tile != Tile::Brick || !object || scaledTileSize <= 0)
            return;

        const int col = (object->getPositionX() - fieldPositionX) / scaledTileSize;
        const int row = (object->getPositionY() - fieldPositionY) / scaledTileSize;
        if(col < 0 || row < 0 ||
           col >= static_cast<int>(tileArrayWidth) ||
           row >= static_cast<int>(tileArrayHeight))
        {
            return;
        }

        brickPresentations_[packCellKey(static_cast<uint8_t>(col), static_cast<uint8_t>(row))] = object;
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

    void MultiplayerLevelScene::returnToLobby(const std::string_view reason)
    {
        if(returningToMenu_)
            return;

        const bool wasMatchStarted = matchStarted_;
        matchStarted_ = false;
        returningToMenu_ = true;
        if (wasMatchStarted)
        {
            LOG_NET_CONN_INFO("Leaving multiplayer match back to lobby ({})", reason);
        }
        else
        {
            LOG_NET_CONN_WARN("Match bootstrap aborted before start ({}) - returning to lobby", reason);
        }
        game->getSceneManager()->addScene("lobby", std::make_shared<LobbyScene>(game));
        game->getSceneManager()->activateScene("lobby");
        game->getSceneManager()->removeScene("level");
    }
} // namespace bomberman
