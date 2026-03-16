#include "Scenes/MultiplayerLevelScene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>

#include <SDL.h>

#include "Entities/Player.h"
#include "Entities/Text.h"
#include "Game.h"
#include "Net/NetClient.h"
#include "Sim/SimConfig.h"
#include "Util/Log.h"

namespace bomberman
{
    namespace
    {
        struct PlayerColor
        {
            uint8_t r;
            uint8_t g;
            uint8_t b;
        };

        static constexpr PlayerColor kPlayerColors[] = {
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
        constexpr uint32_t kGameplayStaleTimeoutMs = 1500;
        constexpr uint32_t kLivePredictionLogIntervalMs = 1000;
        constexpr uint32_t kPredictionTickMs = 1000u / static_cast<uint32_t>(sim::kTickRate);

        bool isAlive(const net::MsgSnapshot::PlayerEntry& entry)
        {
            const uint8_t flags = static_cast<uint8_t>(entry.flags);
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

        MovementDirection inferDirectionFromButtons(const uint8_t buttons,
                                                    const MovementDirection fallback)
        {
            const int8_t moveX = net::buttonsToMoveX(buttons);
            const int8_t moveY = net::buttonsToMoveY(buttons);

            if (moveX == 0 && moveY == 0)
                return MovementDirection::None;

            if (moveX != 0)
                return moveX > 0 ? MovementDirection::Right : MovementDirection::Left;

            if (moveY != 0)
                return moveY > 0 ? MovementDirection::Down : MovementDirection::Up;

            return fallback;
        }
    } // namespace

    MultiplayerLevelScene::MultiplayerLevelScene(Game* game, const unsigned int stage,
                                                 const unsigned int prevScore,
                                                 std::optional<uint32_t> mapSeed)
        : LevelScene(game, stage, prevScore, mapSeed)
    {
        initializeLevelWorld(mapSeed);
    }

    void MultiplayerLevelScene::updateLevel(const unsigned int delta)
    {
        net::NetClient* netClient = game->getNetClient();
        if(netClient == nullptr || netClient->connectState() != net::EConnectState::Connected)
        {
            const auto stateName = netClient ? net::connectStateName(netClient->connectState()) : std::string_view("NoClient");
            leaveToMenu(false, stateName);
            return;
        }

        maybeHandleStaleGameplaySession(*netClient);
        if(leavingToMenu_)
            return;

        if(game->isPredictionEnabled())
        {
            net::MsgCorrection correction{};
            if(netClient->tryGetLatestCorrection(correction) &&
               correction.serverTick > lastAppliedCorrectionTick_)
            {
                applyAuthoritativeCorrection(correction);
            }
        }

        net::MsgSnapshot snapshot{};
        if(game->tryGetLatestSnapshot(snapshot))
        {
            applySnapshot(snapshot);
        }
        Scene::update(delta);
        updateRemotePresentations(delta);
        updateLocalNameTagPosition();
        updateCamera();
        logLivePredictionTelemetry(delta);
    }

    void MultiplayerLevelScene::onExit()
    {
        logPredictionDiagnosticsSummary();
        removeAllRemotePlayers();
        localPrediction_.reset();
        lastAppliedCorrectionTick_ = 0;
        livePredictionTelemetry_ = {};
        livePredictionLogAccumulatorMs_ = 0;

        if(localNameTag_)
        {
            removeObject(localNameTag_);
            localNameTag_.reset();
        }

        LevelScene::onExit();
    }

    void MultiplayerLevelScene::onKeyPressed(const SDL_Scancode scancode)
    {
        if(scancode != SDL_SCANCODE_ESCAPE)
            return;

        leaveToMenu(true, "LocalLeave");
    }

    void MultiplayerLevelScene::onNetworkInputSent(const uint32_t inputSeq, const uint8_t buttons)
    {
        if(!game->isPredictionEnabled())
            return;

        applyPredictedLocalInput(inputSeq, buttons);
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
        syncRemotePlayersFromSnapshot(snapshot, localId);
        lastAppliedSnapshotTick_ = snapshot.serverTick;
    }

    void MultiplayerLevelScene::seedLocalPrediction(const sim::TilePos posQ,
                                                    const uint32_t lastProcessedInputSeq)
    {
        localPrediction_.initialize(posQ, lastProcessedInputSeq);
        playerPos_ = posQ;
        syncPlayerSpriteToSimPosition();
    }

    void MultiplayerLevelScene::applyPredictedLocalInput(const uint32_t inputSeq, const uint8_t buttons)
    {
        if(!player)
            return;

        if(!localPrediction_.isInitialized())
        {
            const uint32_t previousSeq = (inputSeq > 0) ? (inputSeq - 1u) : 0u;
            seedLocalPrediction(playerPos_, previousSeq);
        }

        if(!localPrediction_.applyLocalInput(inputSeq, buttons, tiles))
            return;

        updateLivePredictionPendingDepth();
        syncLocalPresentationFromPredictedState(localPrediction_.currentState());
    }

    void MultiplayerLevelScene::applyAuthoritativeCorrection(const net::MsgCorrection& correction)
    {
        if(correction.serverTick <= lastAppliedCorrectionTick_)
            return;

        const auto replayResult = localPrediction_.applyCorrectionAndReplay(correction, tiles);
        if(replayResult.missingInputHistory > 0)
        {
            LOG_NET_INPUT_WARN("Prediction replay truncated tick={} lastProcessed={} missingInputs={}",
                               correction.serverTick,
                               correction.lastProcessedInputSeq,
                               replayResult.missingInputHistory);
        }
        if(replayResult.recoveryTriggered)
        {
            if(replayResult.recoveryRetruncated)
            {
                LOG_NET_INPUT_WARN(
                    "Prediction recovery retruncated tick={} lastProcessed={} remainingDeferredInputs={} catchUpSeq={}",
                    correction.serverTick,
                    correction.lastProcessedInputSeq,
                    replayResult.remainingDeferredInputs,
                    replayResult.catchUpSeq);
            }
            else
            {
                LOG_NET_INPUT_WARN(
                    "Prediction recovery active tick={} lastProcessed={} remainingDeferredInputs={} catchUpSeq={}",
                    correction.serverTick,
                    correction.lastProcessedInputSeq,
                    replayResult.remainingDeferredInputs,
                    replayResult.catchUpSeq);
            }
        }
        if(replayResult.recoveryResolved)
        {
            LOG_NET_DIAG_INFO("Prediction recovery resolved tick={} lastProcessed={}",
                              correction.serverTick,
                              correction.lastProcessedInputSeq);
        }

        syncLocalPresentationFromPredictedState(localPrediction_.currentState());
        livePredictionTelemetry_.lastAckedInputSeq = correction.lastProcessedInputSeq;
        livePredictionTelemetry_.lastCorrectionServerTick = correction.serverTick;
        livePredictionTelemetry_.lastCorrectionDeltaQ = replayResult.deltaManhattanQ;
        livePredictionTelemetry_.lastReplayCount = replayResult.replayedInputs;
        livePredictionTelemetry_.lastMissingInputs = replayResult.missingInputHistory;
        livePredictionTelemetry_.lastRemainingDeferredInputs = replayResult.remainingDeferredInputs;
        livePredictionTelemetry_.recoveryActive = replayResult.recoveryStillActive;
        livePredictionTelemetry_.recoveryCatchUpSeq = replayResult.catchUpSeq;
        updateLivePredictionPendingDepth();
        lastAppliedCorrectionTick_ = correction.serverTick;
    }

    void MultiplayerLevelScene::maybeHandleStaleGameplaySession(const net::NetClient& netClient)
    {
        const uint32_t silenceMs = netClient.gameplaySilenceMs();
        if(silenceMs < kGameplayStaleTimeoutMs)
            return;

        LOG_NET_CONN_WARN("Multiplayer gameplay stream stale for {}ms - returning to menu", silenceMs);
        leaveToMenu(true, "GameplayStale");
    }

    void MultiplayerLevelScene::logLivePredictionTelemetry(const unsigned int delta)
    {
        if(!game->isPredictionEnabled())
            return;

        const auto& stats = localPrediction_.stats();
        if(stats.localInputsApplied == 0 && stats.correctionsApplied == 0)
            return;

        livePredictionLogAccumulatorMs_ += delta;
        if(livePredictionLogAccumulatorMs_ < kLivePredictionLogIntervalMs)
            return;

        livePredictionLogAccumulatorMs_ = 0;
        updateLivePredictionPendingDepth();

        const uint32_t pendingDepth = currentPendingInputDepth();
        const uint32_t pendingAgeMs = pendingDepth * kPredictionTickMs;
        LOG_NET_DIAG_DEBUG(
            "Prediction live ackSeq={} corrTick={} pendingDepth={} pendingAgeMs={} lastDeltaQ={} lastReplay={} lastMissingInputs={} remainingDeferredInputs={} recoveryActive={} catchUpSeq={} maxPendingDepth={}",
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

    void MultiplayerLevelScene::updateLivePredictionPendingDepth()
    {
        livePredictionTelemetry_.maxPendingInputDepth =
            std::max(livePredictionTelemetry_.maxPendingInputDepth, currentPendingInputDepth());
    }

    uint32_t MultiplayerLevelScene::currentPendingInputDepth() const
    {
        if(!localPrediction_.isInitialized())
            return 0;

        const uint32_t lastRecorded = localPrediction_.lastRecordedInputSeq();
        if(lastRecorded <= livePredictionTelemetry_.lastAckedInputSeq)
            return 0;

        return lastRecorded - livePredictionTelemetry_.lastAckedInputSeq;
    }

    void MultiplayerLevelScene::logPredictionDiagnosticsSummary() const
    {
        if(!game->isPredictionEnabled())
            return;

        const auto& stats = localPrediction_.stats();
        if(stats.localInputsApplied == 0 && stats.correctionsApplied == 0)
            return;

        const double avgCorrectionDeltaQ =
            (stats.correctionsWithPredictedState > 0)
                ? static_cast<double>(stats.totalCorrectionDeltaQ) /
                    static_cast<double>(stats.correctionsWithPredictedState)
                : 0.0;

        LOG_NET_DIAG_INFO(
            "Prediction summary localInputs={} deferredInputs={} rejectedInputs={} corrections={} mismatches={} avgDeltaQ={:.2f} maxDeltaQ={} replayedInputs={} maxReplay={} truncations={} recoveries={} recoveryResolutions={} maxMissingInputs={} maxPendingDepth={}",
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

    void MultiplayerLevelScene::syncLocalPresentationFromPredictedState(const net::PredictedPlayerState& predictedState)
    {
        if(!player)
            return;

        playerPos_ = predictedState.posQ;
        syncPlayerSpriteToSimPosition();

        localIsMoving_ = (net::buttonsToMoveX(predictedState.buttons) != 0) ||
                         (net::buttonsToMoveY(predictedState.buttons) != 0);
        if(localIsMoving_)
        {
            localLastFacing_ = inferDirectionFromButtons(predictedState.buttons, localLastFacing_);
            player->setMovementDirection(localLastFacing_);
        }
        else
        {
            player->setMovementDirection(MovementDirection::None);
        }

        updateLocalNameTagPosition();
        updateCamera();
    }

    void MultiplayerLevelScene::ensureLocalPresentation(const uint8_t localId)
    {
        if(!player)
            return;

        if(localPlayerId_ != localId)
        {
            localPlayerId_ = localId;

            const PlayerColor color = colorForPlayerId(localId);
            player->setColorMod(color.r, color.g, color.b);

            if(localNameTag_)
            {
                localNameTag_->setText(makePlayerTagText(localId));
                localNameTag_->fitToContent();
                localNameTag_->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});
            }
        }

        if(!localNameTag_)
        {
            const int pointSize = computeTagPointSize(scaledTileSize);
            auto font = game->getAssetManager()->getFont(pointSize);
            localNameTag_ = std::make_shared<Text>(font, game->getRenderer(), makePlayerTagText(localId));
            localNameTag_->fitToContent();

            const PlayerColor color = colorForPlayerId(localId);
            localNameTag_->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});

            addObject(localNameTag_);
        }
    }

    void MultiplayerLevelScene::applyLocalSnapshotSample(const net::MsgSnapshot::PlayerEntry& entry,
                                                         const uint8_t /*localId*/,
                                                         const uint32_t tick)
    {
        if(game->isPredictionEnabled())
        {
            if(!localPrediction_.isInitialized())
            {
                seedLocalPrediction({entry.xQ, entry.yQ}, 0);
            }
            return;
        }

        localPreviousSample_ = localLatestSample_;
        localLatestSample_.posQ = {entry.xQ, entry.yQ};
        localLatestSample_.tick = tick;
        localLatestSample_.valid = true;

        playerPos_.xQ = entry.xQ;
        playerPos_.yQ = entry.yQ;
        syncPlayerSpriteToSimPosition();

        if(!localPreviousSample_.valid || !player)
            return;

        const int dxQ = localLatestSample_.posQ.xQ - localPreviousSample_.posQ.xQ;
        const int dyQ = localLatestSample_.posQ.yQ - localPreviousSample_.posQ.yQ;
        const int absDx = std::abs(dxQ);
        const int absDy = std::abs(dyQ);

        localIsMoving_ = (absDx >= kMovementDeltaThresholdQ) || (absDy >= kMovementDeltaThresholdQ);
        if(!localIsMoving_)
        {
            player->setMovementDirection(MovementDirection::None);
            return;
        }

        localLastFacing_ = inferDirectionFromDelta(dxQ, dyQ, localLastFacing_);
        player->setMovementDirection(localLastFacing_);
    }

    void MultiplayerLevelScene::updateLocalNameTagPosition()
    {
        if(!player || !localNameTag_)
            return;

        const int tagX = player->getPositionX() + player->getWidth() / 2 - localNameTag_->getWidth() / 2;
        const int tagY = player->getPositionY() - localNameTag_->getHeight() - kNameTagOffsetPx;
        localNameTag_->setPosition(tagX, tagY);
    }

    void MultiplayerLevelScene::syncRemotePlayersFromSnapshot(const net::MsgSnapshot& snapshot,
                                                              const uint8_t localId)
    {
        std::unordered_map<uint8_t, bool> seenRemoteIds;

        for(uint8_t i = 0; i < snapshot.playerCount; ++i)
        {
            const auto& entry = snapshot.players[i];
            const bool alive = isAlive(entry);

            if(entry.playerId == localId)
            {
                // Keep existing local-player authoritative application behavior.
                if(alive)
                {
                    applyLocalSnapshotSample(entry, localId, snapshot.serverTick);
                }
                continue;
            }

            if(!alive)
                continue;

            seenRemoteIds[entry.playerId] = true;
            upsertRemotePlayer(entry.playerId, entry.xQ, entry.yQ, snapshot.serverTick);
        }

        removeMissingRemotePlayers(seenRemoteIds);
    }

    void MultiplayerLevelScene::upsertRemotePlayer(const uint8_t playerId,
                                                   const int16_t xQ,
                                                   const int16_t yQ,
                                                   const uint32_t snapshotTick)
    {
        auto [it, inserted] = remotePlayers_.try_emplace(playerId);
        RemotePlayerView& view = it->second;

        if(inserted)
        {
            view.sprite = std::make_shared<Player>(game->getAssetManager()->getTexture(Texture::Player),
                                                   game->getRenderer());
            view.sprite->setSize(scaledTileSize, scaledTileSize);
            view.sprite->setMovementDirection(MovementDirection::None);

            const PlayerColor color = colorForPlayerId(playerId);
            view.sprite->setColorMod(color.r, color.g, color.b);

            const int pointSize = computeTagPointSize(scaledTileSize);
            auto font = game->getAssetManager()->getFont(pointSize);
            view.nameTag = std::make_shared<Text>(font, game->getRenderer(), makePlayerTagText(playerId));
            view.nameTag->fitToContent();
            view.nameTag->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});

            addObject(view.sprite);
            addObject(view.nameTag);
        }

        updateSnapshotHistory(view, xQ, yQ, snapshotTick);
        updateAnimationFromSnapshotDelta(view);

        view.authoritativePosQ.xQ = xQ;
        view.authoritativePosQ.yQ = yQ;
        view.lastSeenSnapshotTick = snapshotTick;

        const sim::TilePos presentedPosQ = resolvePresentedPosition(view);
        view.presentedPosQ = presentedPosQ;
        const int screenX = sim::tileQToScreenTopLeft(presentedPosQ.xQ, fieldPositionX, scaledTileSize, 0);
        const int screenY = sim::tileQToScreenTopLeft(presentedPosQ.yQ, fieldPositionY, scaledTileSize, 0);
        view.sprite->setPosition(screenX, screenY);

        updateRemoteNameTagPosition(view, playerId);
    }

    void MultiplayerLevelScene::removeMissingRemotePlayers(const std::unordered_map<uint8_t, bool>& seenRemoteIds)
    {
        for(auto it = remotePlayers_.begin(); it != remotePlayers_.end();)
        {
            if(seenRemoteIds.find(it->first) != seenRemoteIds.end())
            {
                ++it;
                continue;
            }

            if(it->second.sprite)
                removeObject(it->second.sprite);
            if(it->second.nameTag)
                removeObject(it->second.nameTag);

            it = remotePlayers_.erase(it);
        }
    }

    void MultiplayerLevelScene::removeAllRemotePlayers()
    {
        for(auto& [id, view] : remotePlayers_)
        {
            (void)id;
            if(view.sprite)
                removeObject(view.sprite);
            if(view.nameTag)
                removeObject(view.nameTag);
        }
        remotePlayers_.clear();
    }

    void MultiplayerLevelScene::updateSnapshotHistory(RemotePlayerView& view,
                                                      const int16_t xQ,
                                                      const int16_t yQ,
                                                      const uint32_t tick)
    {
        view.previousSample = view.latestSample;
        view.latestSample.posQ = {xQ, yQ};
        view.latestSample.tick = tick;
        view.latestSample.valid = true;
        view.ticksSinceLatestSample = 0.0f;
    }

    void MultiplayerLevelScene::updateAnimationFromSnapshotDelta(RemotePlayerView& view)
    {
        if(!view.latestSample.valid || !view.sprite)
            return;

        if(!view.previousSample.valid)
        {
            view.sprite->setMovementDirection(MovementDirection::None);
            view.isMoving = false;
            return;
        }

        const int dxQ = view.latestSample.posQ.xQ - view.previousSample.posQ.xQ;
        const int dyQ = view.latestSample.posQ.yQ - view.previousSample.posQ.yQ;
        const int absDx = std::abs(dxQ);
        const int absDy = std::abs(dyQ);

        view.isMoving = (absDx >= kMovementDeltaThresholdQ) || (absDy >= kMovementDeltaThresholdQ);
        if(!view.isMoving)
        {
            view.sprite->setMovementDirection(MovementDirection::None);
            return;
        }

        view.lastFacing = inferDirectionFromDelta(dxQ, dyQ, view.lastFacing);
        view.sprite->setMovementDirection(view.lastFacing);
    }

    void MultiplayerLevelScene::updateRemotePresentations(const unsigned int delta)
    {
        static_cast<void>(delta);
        const float tickDelta = 1.0f;

        for(auto& [playerId, view] : remotePlayers_)
        {
            if(!view.sprite)
                continue;

            view.ticksSinceLatestSample += tickDelta;

            const sim::TilePos presentedPosQ = resolvePresentedPosition(view);
            view.presentedPosQ = presentedPosQ;

            const int screenX = sim::tileQToScreenTopLeft(presentedPosQ.xQ, fieldPositionX, scaledTileSize, 0);
            const int screenY = sim::tileQToScreenTopLeft(presentedPosQ.yQ, fieldPositionY, scaledTileSize, 0);
            view.sprite->setPosition(screenX, screenY);

            updateRemoteNameTagPosition(view, playerId);
        }
    }

    sim::TilePos MultiplayerLevelScene::resolvePresentedPosition(const RemotePlayerView& view) const
    {
        if(!game->isRemoteSmoothingEnabled())
            return view.authoritativePosQ;

        if(!view.previousSample.valid || !view.latestSample.valid)
            return view.authoritativePosQ;

        const uint32_t observedSnapshotSpacingTicks =
            (view.latestSample.tick > view.previousSample.tick)
                ? (view.latestSample.tick - view.previousSample.tick)
                : 0u;

        if(observedSnapshotSpacingTicks == 0)
            return view.authoritativePosQ;

        // TODO: If observed snapshot spacing becomes too noisy under loss, send the nominal
        // snapshot interval to clients during handshake/level setup and blend against that value.
        const float alpha = std::clamp(
            view.ticksSinceLatestSample / static_cast<float>(observedSnapshotSpacingTicks),
            0.0f,
            1.0f);

        sim::TilePos presentedPosQ{};
        presentedPosQ.xQ = static_cast<int32_t>(std::lround(
            static_cast<double>(view.previousSample.posQ.xQ) +
            static_cast<double>(view.latestSample.posQ.xQ - view.previousSample.posQ.xQ) * alpha));
        presentedPosQ.yQ = static_cast<int32_t>(std::lround(
            static_cast<double>(view.previousSample.posQ.yQ) +
            static_cast<double>(view.latestSample.posQ.yQ - view.previousSample.posQ.yQ) * alpha));

        return presentedPosQ;
    }

    void MultiplayerLevelScene::updateRemoteNameTagPosition(RemotePlayerView& view, const uint8_t /*playerId*/)
    {
        if(!view.nameTag || !view.sprite)
            return;

        const int tagX = view.sprite->getPositionX() + view.sprite->getWidth() / 2 - view.nameTag->getWidth() / 2;
        const int tagY = view.sprite->getPositionY() - view.nameTag->getHeight() - kNameTagOffsetPx;
        view.nameTag->setPosition(tagX, tagY);
    }

    std::string MultiplayerLevelScene::makePlayerTagText(const uint8_t playerId)
    {
        // Player IDs are zero-indexed, but for display we want them to be 1-indexed.
        return "P" + std::to_string(static_cast<unsigned int>(playerId) + 1u);
    }

    void MultiplayerLevelScene::leaveToMenu(const bool disconnectClient, const std::string_view reason)
    {
        if(leavingToMenu_)
            return;

        leavingToMenu_ = true;

        if(disconnectClient)
        {
            LOG_NET_CONN_INFO("Leaving multiplayer level and disconnecting: {}", reason);
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
