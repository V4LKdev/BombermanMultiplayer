/** @file MultiplayerLevelScene.cpp
 *  @brief Multiplayer scene lifecycle and frame flow.
 *  @ingroup multiplayer_level_scene
 */

#include "Scenes/MultiplayerLevelScene/MultiplayerLevelSceneInternal.h"

#include <SDL.h>

#include "Game.h"
#include "Net/ClientDiagnostics.h"
#include "Net/Client/NetClient.h"
#include "Scenes/LobbyScene.h"
#include "Util/Log.h"

namespace bomberman
{
    using namespace multiplayer_level_scene_internal;

    // =============================================================================================================
    // ===== Construction ===========================================================================================
    // =============================================================================================================

    MultiplayerLevelScene::MultiplayerLevelScene(Game* game,
                                                 const unsigned int stage,
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

    // =============================================================================================================
    // ===== Scene Activity =========================================================================================
    // =============================================================================================================

    bool MultiplayerLevelScene::wantsNetworkInputPolling() const
    {
        if (exited_ || !allowsLocalGameplayInput())
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

    // =============================================================================================================
    // ===== Frame Update ===========================================================================================
    // =============================================================================================================

    void MultiplayerLevelScene::updateLevel(const unsigned int delta)
    {
        if (exited_)
            return;

        net::NetClient* netClient = requireConnectedNetClient();
        if (netClient == nullptr)
            return;

        if (!updatePreStartFlow(*netClient))
            return;

        if (updateLobbyReturnFlow(*netClient))
            return;

        if (netClient->hasBrokenGameplayEventStream())
        {
            LOG_NET_SNAPSHOT_ERROR("Reliable gameplay event stream became unusable during matchId={} - leaving match", matchId_);
            returnToMenu(true, "GameplayEventStreamBroken");
            return;
        }

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

    // =============================================================================================================
    // ===== Match Flow =============================================================================================
    // =============================================================================================================

    net::NetClient* MultiplayerLevelScene::requireConnectedNetClient()
    {
        net::NetClient* netClient = game->getNetClient();
        if (netClient == nullptr || netClient->connectState() != net::EConnectState::Connected)
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
            const bool firstResultForThisMatch = !currentMatchResult_.has_value();
            currentMatchResult_ = matchResult;
            gameplayUnlocked_ = false;
            setGameplayConnectionDegraded(false);
            if (firstResultForThisMatch && menuMusic != nullptr)
            {
                menuMusic->stop();
            }
        }

        if (!currentMatchResult_.has_value())
        {
            return;
        }

        if (currentMatchResult_->result == net::MsgMatchResult::EResult::Draw)
        {
            showCenterBanner("DRAW!", SDL_Color{0xFF, 0xD1, 0x66, 0xFF}); // Gold
            return;
        }

        showCenterBanner("WINS!",
                         net::matchResultWinnerName(*currentMatchResult_),
                         SDL_Color{0xFF, 0xD1, 0x66, 0xFF});
    }

    void MultiplayerLevelScene::updateDeathBannerFlow()
    {
        if (localPlayerAlive_ || currentMatchResult_.has_value())
            return;

        showCenterBanner("DEAD", SDL_Color{0xE8, 0x6A, 0x6A, 0xFF}); // Red
    }

    // =============================================================================================================
    // ===== Finalization and Exit ==================================================================================
    // =============================================================================================================

    void MultiplayerLevelScene::finalizeFrameUpdate(const unsigned int delta)
    {
        /*
         * Flow:
         *  1) updateSceneObjects to tick all presentations and world objects with the new authoritative state.
         *  2) update pending bomb placements to spawn bomb presentations at the right time and remove them if expired.
         *  3) update bomb and explosion presentations to match the latest authoritative state and remove them when expired.
         *  4) update remote player presentations to advance interpolation and update tags.
         *  5) update powerup effect presentations to match the latest authoritative state and remove them when expired.
         *  6) update the local player tag position to follow the local player sprite.
         *  7) update the camera to follow the local player.
         *  8) log live prediction telemetry for diagnostics and tuning.
         *  9) update the debug HUD with current multiplayer stats if enabled.
         */
        updateSceneObjects(delta);
        updatePendingLocalBombPlacements(delta);
        updateLocalBombSparkPresentations(delta);
        updateExplosionPresentations(delta);
        updateRemotePlayerPresentations(delta);
        updatePowerupEffectPresentations(delta);
        updateLocalPlayerTagPosition();
        updateCamera();
        logLivePredictionTelemetry(delta);
        updateDebugHud(delta);
    }

    // =============================================================================================================
    // ===== Scene Transitions ======================================================================================
    // =============================================================================================================

    void MultiplayerLevelScene::onExit()
    {
        exited_ = true;
        // Log final prediction stats before cleaning up remote player presentations.
        if (auto* netClient = game ? game->getNetClient() : nullptr; netClient != nullptr)
        {
            const auto& stats = localPrediction_.stats();
            netClient->clientDiagnostics().feedPredictionStats(
                stats,
                localPrediction_.isInitialized() || stats.correctionsApplied > 0,
                stats.recoveryActivations > 0);

            netClient->updateLivePredictionStats(false, false, 0, 0, 0, 0);
        }
        logPredictionSummary();
        removeAllRemotePlayers();
        removeAllSnapshotBombs();
        removeAllSnapshotPowerups();

        localPrediction_.reset();
        lastAppliedSnapshotTick_ = 0;
        lastAppliedCorrectionTick_ = 0;
        lastAppliedGameplayEventTick_ = 0;
        localPlayerId_.reset();
        matchId_ = 0;
        matchBootstrapStartedMs_ = 0;
        matchStarted_ = false;
        gameplayUnlocked_ = false;
        matchLoadedAckSent_ = false;
        goBannerHideTick_ = 0;
        currentMatchStart_.reset();
        currentMatchResult_.reset();
        localPlayerAlive_ = true;
        localPlayerInputLocked_ = false;
        localBombHeldOnLastQueuedInput_ = false;
        localPlayerEffectFlags_ = 0;
        powerupBlinkAccumulatorMs_ = 0;
        livePredictionTelemetry_ = {};
        localFacingDirection_ = MovementDirection::Right;
        livePredictionLogAccumulatorMs_ = 0;
        debugHudRefreshAccumulatorMs_ = 0;
        gameplayConnectionDegraded_ = false;
        returningToMenu_ = true;
        pendingGameplayEvents_.clear();

        if (localPlayerTag_)
        {
            removeObject(localPlayerTag_);
            localPlayerTag_.reset();
        }

        if (gameplayStatusText_)
        {
            removeObject(gameplayStatusText_);
            gameplayStatusText_.reset();
        }

        hideCenterBanner();
        removeDebugHudPresentations();

        removeAllLocalBombSparkPresentations();
        pendingLocalBombPlacements_.clear();
        removeAllExplosionPresentations();
        brickPresentations_.clear();

        LevelScene::onExit();
    }

    void MultiplayerLevelScene::onKeyPressed(const SDL_Scancode scancode)
    {
        if (scancode != SDL_SCANCODE_ESCAPE)
            return;

        returnToMenu(true, "LocalLeave");
    }

    void MultiplayerLevelScene::returnToMenu(const bool disconnectClient, const std::string_view reason)
    {
        if (returningToMenu_)
            return;

        returningToMenu_ = true;

        if (disconnectClient)
        {
            if (auto* netClient = game->getNetClient(); netClient != nullptr)
            {
                net::NetEvent event{};
                event.type = net::NetEventType::Flow;
                event.peerId = netClient->playerId();
                event.note = std::string("return to menu: ") + std::string(reason);
                netClient->clientDiagnostics().recordEvent(event);
            }
            LOG_NET_CONN_INFO("Leaving multiplayer level and disconnecting: {}", reason);
            game->disconnectNetClientIfActive(false);
        }
        else
        {
            if (auto* netClient = game->getNetClient(); netClient != nullptr)
            {
                net::NetEvent event{};
                event.type = net::NetEventType::Flow;
                event.peerId = netClient->playerId();
                event.note = std::string("multiplayer level lost connection: ") + std::string(reason);
                netClient->clientDiagnostics().recordEvent(event);
            }
            LOG_NET_CONN_WARN("Multiplayer level lost connection (state={}) - returning to menu", reason);
        }

        game->getSceneManager()->activateScene("menu");
        game->getSceneManager()->removeScene("level");
    }

    void MultiplayerLevelScene::returnToLobby(const std::string_view reason)
    {
        if (returningToMenu_)
            return;

        const bool wasMatchStarted = matchStarted_;
        matchStarted_ = false;
        returningToMenu_ = true;
        if (wasMatchStarted)
        {
            if (auto* netClient = game->getNetClient(); netClient != nullptr)
            {
                net::NetEvent event{};
                event.type = net::NetEventType::Flow;
                event.peerId = netClient->playerId();
                event.note = std::string("return to lobby: ") + std::string(reason);
                netClient->clientDiagnostics().recordEvent(event);
            }
            LOG_NET_CONN_INFO("Leaving multiplayer match back to lobby ({})", reason);
        }
        else
        {
            if (auto* netClient = game->getNetClient(); netClient != nullptr)
            {
                net::NetEvent event{};
                event.type = net::NetEventType::Flow;
                event.peerId = netClient->playerId();
                event.note = std::string("bootstrap cancelled to lobby: ") + std::string(reason);
                netClient->clientDiagnostics().recordEvent(event);
            }
            LOG_NET_CONN_WARN("Match bootstrap aborted before start ({}) - returning to lobby", reason);
        }

        game->getSceneManager()->addScene("lobby", std::make_shared<LobbyScene>(game));
        game->getSceneManager()->activateScene("lobby");
        game->getSceneManager()->removeScene("level");
    }
} // namespace bomberman
