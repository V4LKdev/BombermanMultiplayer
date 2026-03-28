/**
 * @file MultiplayerLevelScene.Presentation.cpp
 * @brief MultiplayerLevelScene presentation, HUD, and visual state logic.
 */

#include "Scenes/MultiplayerLevelSceneInternal.h"

#include <cassert>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_set>

#include <SDL.h>

#include "Entities/Player.h"
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
        util::PlayerColor boostedBombAccentColor()
        {
            return {0xFF, 0x58, 0x58};
        }

        [[nodiscard]]
        util::PlayerColor powerupEffectColor(const uint8_t effectFlags)
        {
            using PlayerFlags = net::MsgSnapshot::PlayerEntry::EPlayerFlags;

            if ((effectFlags & static_cast<uint8_t>(PlayerFlags::Invulnerable)) != 0)
                return {0xFF, 0xE6, 0x5A};
            if ((effectFlags & static_cast<uint8_t>(PlayerFlags::SpeedBoost)) != 0)
                return {0x42, 0xD9, 0xC7};
            if ((effectFlags & static_cast<uint8_t>(PlayerFlags::BombRangeBoost)) != 0)
                return boostedBombAccentColor();
            if ((effectFlags & static_cast<uint8_t>(PlayerFlags::MaxBombsBoost)) != 0)
                return {0xB8, 0x72, 0xFF};

            return {0xFF, 0xFF, 0xFF};
        }

        [[nodiscard]]
        bool hasVisiblePowerupEffect(const uint8_t effectFlags)
        {
            using PlayerFlags = net::MsgSnapshot::PlayerEntry::EPlayerFlags;

            return (effectFlags & static_cast<uint8_t>(PlayerFlags::Invulnerable)) != 0 ||
                   (effectFlags & static_cast<uint8_t>(PlayerFlags::SpeedBoost)) != 0 ||
                   (effectFlags & static_cast<uint8_t>(PlayerFlags::BombRangeBoost)) != 0 ||
                   (effectFlags & static_cast<uint8_t>(PlayerFlags::MaxBombsBoost)) != 0;
        }

        [[nodiscard]]
        Texture textureForPowerupType(const sim::PowerupType type)
        {
            switch (type)
            {
                case sim::PowerupType::SpeedBoost:
                    return Texture::PowerupSpeed;
                case sim::PowerupType::Invincibility:
                    return Texture::PowerupInvincible;
                case sim::PowerupType::BombRangeBoost:
                    return Texture::PowerupRange;
                case sim::PowerupType::MaxBombsBoost:
                    return Texture::PowerupBombs;
            }

            return Texture::PowerupSpeed;
        }

        [[nodiscard]]
        std::string formatLossPercent(const uint32_t lossPermille)
        {
            std::ostringstream out;
            out << std::fixed << std::setprecision(1)
                << (static_cast<double>(lossPermille) / 10.0);
            return out.str();
        }
    } // namespace

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

    void MultiplayerLevelScene::showCenterBanner(const std::string_view message, const SDL_Color color)
    {
        showCenterBanner(message, {}, color);
    }

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
        if (!player)
            return;

        if (!localPlayerId_.has_value() || localPlayerId_.value() != localId)
        {
            localPlayerId_ = localId;

            const util::PlayerColor color = util::colorForPlayerId(localId);
            player->setColorMod(color.r, color.g, color.b);

            if (localPlayerTag_)
            {
                localPlayerTag_->setText(formatPlayerTag(localId));
                localPlayerTag_->fitToContent();
                localPlayerTag_->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});
            }
        }

        if (!localPlayerTag_)
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

    void MultiplayerLevelScene::updateLocalPlayerTagPosition()
    {
        if (!player || !localPlayerTag_ || !localPlayerAlive_)
            return;

        const int tagX = player->getPositionX() + player->getWidth() / 2 - localPlayerTag_->getWidth() / 2;
        const int tagY = player->getPositionY() - localPlayerTag_->getHeight() - kNameTagOffsetPx;
        localPlayerTag_->setPosition(tagX, tagY);
    }

    void MultiplayerLevelScene::removeRemotePlayerPresentation(const uint8_t playerId)
    {
        const auto it = remotePlayerPresentations_.find(playerId);
        if (it == remotePlayerPresentations_.end())
            return;

        if (it->second.playerSprite)
            removeObject(it->second.playerSprite);
        if (it->second.playerTag)
            removeObject(it->second.playerTag);

        remotePlayerPresentations_.erase(it);
    }

    void MultiplayerLevelScene::applySnapshotToRemotePlayers(const net::MsgSnapshot& snapshot,
                                                             const uint8_t localId)
    {
        std::unordered_set<uint8_t> seenRemoteIds;

        for (uint8_t i = 0; i < snapshot.playerCount; ++i)
        {
            const auto& entry = snapshot.players[i];
            const bool alive = snapshotEntryIsAlive(entry);
            const bool inputLocked = snapshotEntryInputLocked(entry);
            const uint8_t rawFlags = static_cast<uint8_t>(entry.flags);

            if (entry.playerId == localId)
            {
                const bool shouldSuspendPredictionForLock =
                    inputLocked &&
                    !localPlayerInputLocked_ &&
                    player &&
                    game->isPredictionEnabled();
                if (snapshot.serverTick >= lastAppliedCorrectionTick_)
                {
                    // Snapshot wins when it is same-tick or newer than the latest correction, but older snapshots must not
                    // roll owner-local effect/loadout flags back behind a newer correction.
                    localPlayerEffectFlags_ = rawFlags;
                }
                // Owner death/input-lock presentation remains snapshot/reliable-event authoritative rather than correction-owned.
                setLocalPlayerAlivePresentation(alive);
                setLocalPlayerInputLock(inputLocked);
                if (shouldSuspendPredictionForLock)
                {
                    localPrediction_.suspend();
                }
                if (alive)
                {
                    applyAuthoritativeLocalSnapshot(entry);
                }
                continue;
            }

            if (!alive)
                continue;

            seenRemoteIds.insert(entry.playerId);
            updateOrCreateRemotePlayer(entry.playerId, entry.xQ, entry.yQ, snapshot.serverTick, rawFlags);
        }

        pruneMissingRemotePlayers(seenRemoteIds);
    }

    void MultiplayerLevelScene::applySnapshotBombs(const net::MsgSnapshot& snapshot)
    {
        std::unordered_set<uint16_t> seenBombCells;
        seenBombCells.reserve(snapshot.bombCount);

        for (uint8_t i = 0; i < snapshot.bombCount; ++i)
        {
            const auto& entry = snapshot.bombs[i];
            seenBombCells.insert(packCellKey(entry.col, entry.row));
            updateOrCreateBombPresentation(entry);
        }

        pruneMissingBombPresentations(seenBombCells);
    }

    void MultiplayerLevelScene::applySnapshotPowerups(const net::MsgSnapshot& snapshot)
    {
        std::unordered_set<uint16_t> seenPowerupCells;
        seenPowerupCells.reserve(snapshot.powerupCount);

        for (uint8_t i = 0; i < snapshot.powerupCount; ++i)
        {
            const auto& entry = snapshot.powerups[i];
            seenPowerupCells.insert(packCellKey(entry.col, entry.row));
            updateOrCreatePowerupPresentation(entry);
        }

        pruneMissingPowerupPresentations(seenPowerupCells);
    }

    void MultiplayerLevelScene::updateOrCreateRemotePlayer(const uint8_t playerId,
                                                           const int16_t xQ,
                                                           const int16_t yQ,
                                                           const uint32_t snapshotTick,
                                                           const uint8_t flags)
    {
        auto [it, inserted] = remotePlayerPresentations_.try_emplace(playerId);
        RemotePlayerPresentation& presentation = it->second;

        if (inserted)
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

        presentation.effectFlags = flags;
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
        const bool boostedRangeBomb = entry.radius > sim::kDefaultPlayerBombRange;

        if (inserted)
        {
            presentation.bombSprite =
                std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Bomb), game->getRenderer());
            presentation.bombSprite->setSize(scaledTileSize, scaledTileSize);
            attachBombAnimation(presentation.bombSprite);
            insertObject(presentation.bombSprite, backgroundObjectLastNumber);
        }

        presentation.ownerId = entry.ownerId;
        presentation.radius = entry.radius;

        if (localPlayerId_.has_value() && entry.ownerId == localPlayerId_.value())
        {
            for (auto pendingIt = pendingLocalBombPlacements_.begin(); pendingIt != pendingLocalBombPlacements_.end(); ++pendingIt)
            {
                if (pendingIt->cellKey == bombCellKey)
                {
                    pendingLocalBombPlacements_.erase(pendingIt);
                    break;
                }
            }
        }

        const util::PlayerColor color = util::colorForPlayerId(entry.ownerId);
        presentation.bombSprite->setColorMod(color.r, color.g, color.b);

        const int bombExtraPx = boostedRangeBomb ? kBoostedBombExtraPx : 0;
        const int bombSize = scaledTileSize + bombExtraPx;
        presentation.bombSprite->setSize(bombSize, bombSize);

        const int screenX = fieldPositionX + static_cast<int>(entry.col) * scaledTileSize - bombExtraPx / 2;
        const int screenY = fieldPositionY + static_cast<int>(entry.row) * scaledTileSize - bombExtraPx / 2;
        presentation.bombSprite->setPosition(screenX, screenY);
    }

    void MultiplayerLevelScene::updateOrCreatePowerupPresentation(const net::MsgSnapshot::PowerupEntry& entry)
    {
        const uint16_t powerupCellKey = packCellKey(entry.col, entry.row);
        auto [it, inserted] = powerupPresentations_.try_emplace(powerupCellKey);
        PowerupPresentation& presentation = it->second;

        if (!inserted && presentation.type != entry.type)
        {
            if (presentation.powerupSprite)
                removeObject(presentation.powerupSprite);

            presentation = {};
            inserted = true;
        }

        if (inserted || !presentation.powerupSprite)
        {
            presentation.powerupSprite =
                std::make_shared<Sprite>(game->getAssetManager()->getTexture(textureForPowerupType(entry.type)),
                                         game->getRenderer());
            presentation.powerupSprite->setSize(scaledTileSize, scaledTileSize);
            insertObject(presentation.powerupSprite, backgroundObjectLastNumber);
        }

        presentation.type = entry.type;

        const int screenX = fieldPositionX + static_cast<int>(entry.col) * scaledTileSize;
        const int screenY = fieldPositionY + static_cast<int>(entry.row) * scaledTileSize;
        presentation.powerupSprite->setPosition(screenX, screenY);
    }

    void MultiplayerLevelScene::pruneMissingRemotePlayers(const std::unordered_set<uint8_t>& seenRemoteIds)
    {
        for (auto it = remotePlayerPresentations_.begin(); it != remotePlayerPresentations_.end();)
        {
            if (seenRemoteIds.contains(it->first))
            {
                ++it;
                continue;
            }

            if (it->second.playerSprite)
                removeObject(it->second.playerSprite);
            if (it->second.playerTag)
                removeObject(it->second.playerTag);

            it = remotePlayerPresentations_.erase(it);
        }
    }

    void MultiplayerLevelScene::pruneMissingBombPresentations(const std::unordered_set<uint16_t>& seenBombCells)
    {
        for (auto it = bombPresentations_.begin(); it != bombPresentations_.end();)
        {
            if (seenBombCells.contains(it->first))
            {
                ++it;
                continue;
            }

            if (it->second.bombSprite)
                removeObject(it->second.bombSprite);

            it = bombPresentations_.erase(it);
        }
    }

    void MultiplayerLevelScene::pruneMissingPowerupPresentations(const std::unordered_set<uint16_t>& seenPowerupCells)
    {
        for (auto it = powerupPresentations_.begin(); it != powerupPresentations_.end();)
        {
            if (seenPowerupCells.contains(it->first))
            {
                ++it;
                continue;
            }

            if (it->second.powerupSprite)
                removeObject(it->second.powerupSprite);

            it = powerupPresentations_.erase(it);
        }
    }

    void MultiplayerLevelScene::removeAllRemotePlayers()
    {
        for (auto& entry : remotePlayerPresentations_)
        {
            auto& presentation = entry.second;
            if (presentation.playerSprite)
                removeObject(presentation.playerSprite);
            if (presentation.playerTag)
                removeObject(presentation.playerTag);
        }

        remotePlayerPresentations_.clear();
    }

    void MultiplayerLevelScene::removeAllSnapshotBombs()
    {
        for (auto& entry : bombPresentations_)
        {
            auto& presentation = entry.second;
            if (presentation.bombSprite)
                removeObject(presentation.bombSprite);
        }

        bombPresentations_.clear();
    }

    void MultiplayerLevelScene::removeAllSnapshotPowerups()
    {
        for (auto& entry : powerupPresentations_)
        {
            auto& presentation = entry.second;
            if (presentation.powerupSprite)
                removeObject(presentation.powerupSprite);
        }

        powerupPresentations_.clear();
    }

    void MultiplayerLevelScene::applyBombPlacedEvent(const net::MsgBombPlaced& bombPlaced)
    {
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
        removeBombPresentation(explosion.originCol, explosion.originRow);

        for (uint8_t i = 0; i < explosion.destroyedBrickCount; ++i)
        {
            const auto& brickCell = explosion.destroyedBricks[i];
            destroyBrickPresentation(brickCell.col, brickCell.row);
        }

        for (uint8_t i = 0; i < explosion.blastCellCount; ++i)
        {
            spawnExplosionPresentation(explosion.blastCells[i]);
        }

        if (explosionSound_)
        {
            explosionSound_->play();
        }

        for (uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
        {
            const uint8_t playerBit = static_cast<uint8_t>(1u << playerId);
            if ((explosion.killedPlayerMask & playerBit) == 0)
                continue;

            if (localPlayerId_.has_value() && localPlayerId_.value() == playerId)
            {
                const bool shouldSuspendPredictionForDeath =
                    !localPlayerInputLocked_ &&
                    player &&
                    game->isPredictionEnabled();
                setLocalPlayerInputLock(true);
                if (shouldSuspendPredictionForDeath)
                {
                    localPrediction_.suspend();
                }
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
        if (it != brickPresentations_.end())
        {
            if (it->second)
                removeObject(it->second);
            brickPresentations_.erase(it);
        }
    }

    void MultiplayerLevelScene::removeBombPresentation(const uint8_t col, const uint8_t row)
    {
        const uint16_t bombCellKey = packCellKey(col, row);
        const auto it = bombPresentations_.find(bombCellKey);
        if (it == bombPresentations_.end())
            return;

        if (it->second.bombSprite)
            removeObject(it->second.bombSprite);
        bombPresentations_.erase(it);
    }

    bool MultiplayerLevelScene::canSpawnLocalBombSparkPresentation() const
    {
        if (!player || !localPlayerId_.has_value() || scaledTileSize <= 0)
            return false;

        const int32_t col = playerPos_.xQ / 256;
        const int32_t row = playerPos_.yQ / 256;
        if (col < 0 || row < 0 ||
            col >= static_cast<int32_t>(tileArrayWidth) ||
            row >= static_cast<int32_t>(tileArrayHeight))
        {
            return false;
        }

        const Tile tile = tiles[row][col];
        if (tile == Tile::Stone || tile == Tile::Brick)
            return false;

        const uint16_t cellKey = packCellKey(static_cast<uint8_t>(col), static_cast<uint8_t>(row));
        const uint8_t localPlayerId = localPlayerId_.value();
        const uint8_t maxBombs =
            (localPlayerEffectFlags_ &
             static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::MaxBombsBoost))
                ? sim::kBoostedMaxBombs
                : sim::kDefaultPlayerMaxBombs;

        uint8_t ownedAuthoritativeBombCount = 0;
        for (const auto& [bombCellKey, presentation] : bombPresentations_)
        {
            if (bombCellKey == cellKey)
                return false;

            if (presentation.ownerId == localPlayerId)
                ++ownedAuthoritativeBombCount;
        }

        for (const auto& pendingPlacement : pendingLocalBombPlacements_)
        {
            if (pendingPlacement.cellKey == cellKey)
                return false;
        }

        return static_cast<uint8_t>(ownedAuthoritativeBombCount + pendingLocalBombPlacements_.size()) < maxBombs;
    }

    void MultiplayerLevelScene::spawnLocalBombSparkPresentation()
    {
        if (!player || scaledTileSize <= 0)
            return;

        const int32_t col = playerPos_.xQ / 256;
        const int32_t row = playerPos_.yQ / 256;
        if (col < 0 || row < 0 ||
            col >= static_cast<int32_t>(tileArrayWidth) ||
            row >= static_cast<int32_t>(tileArrayHeight))
        {
            return;
        }

        auto sparkSprite =
            std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::BombSpark), game->getRenderer());
        sparkSprite->setSize(scaledTileSize, scaledTileSize);
        attachBombSparkAnimation(sparkSprite);

        const int screenX = fieldPositionX + static_cast<int>(col) * scaledTileSize;
        const int screenY = fieldPositionY + static_cast<int>(row) * scaledTileSize;
        sparkSprite->setPosition(screenX, screenY);
        addObject(sparkSprite);

        pendingLocalBombPlacements_.push_back(
            {packCellKey(static_cast<uint8_t>(col), static_cast<uint8_t>(row)),
             kPendingLocalBombPlacementLifetimeMs});
        bombSparkPresentations_.push_back({sparkSprite, kBombSparkLifetimeMs});
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

    void MultiplayerLevelScene::updateLocalBombSparkPresentations(const unsigned int delta)
    {
        for (auto it = bombSparkPresentations_.begin(); it != bombSparkPresentations_.end();)
        {
            auto& presentation = *it;
            if (presentation.remainingLifetimeMs > delta)
            {
                presentation.remainingLifetimeMs -= delta;
                ++it;
                continue;
            }

            if (presentation.sparkSprite)
                removeObject(presentation.sparkSprite);

            it = bombSparkPresentations_.erase(it);
        }
    }

    void MultiplayerLevelScene::updatePendingLocalBombPlacements(const unsigned int delta)
    {
        for (auto it = pendingLocalBombPlacements_.begin(); it != pendingLocalBombPlacements_.end();)
        {
            if (it->remainingLifetimeMs > delta)
            {
                it->remainingLifetimeMs -= delta;
                ++it;
                continue;
            }

            it = pendingLocalBombPlacements_.erase(it);
        }
    }

    void MultiplayerLevelScene::updateExplosionPresentations(const unsigned int delta)
    {
        for (auto it = explosionPresentations_.begin(); it != explosionPresentations_.end();)
        {
            auto& presentation = *it;
            if (presentation.remainingLifetimeMs > delta)
            {
                presentation.remainingLifetimeMs -= delta;
                ++it;
                continue;
            }

            if (presentation.explosionSprite)
                removeObject(presentation.explosionSprite);

            it = explosionPresentations_.erase(it);
        }
    }

    void MultiplayerLevelScene::removeAllLocalBombSparkPresentations()
    {
        for (auto& presentation : bombSparkPresentations_)
        {
            if (presentation.sparkSprite)
                removeObject(presentation.sparkSprite);
        }

        bombSparkPresentations_.clear();
    }

    void MultiplayerLevelScene::removeAllExplosionPresentations()
    {
        for (auto& presentation : explosionPresentations_)
        {
            if (presentation.explosionSprite)
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

    uint32_t MultiplayerLevelScene::gameplayEventServerTick(const net::NetClient::GameplayEvent& gameplayEvent)
    {
        switch (gameplayEvent.type)
        {
            case net::NetClient::GameplayEvent::EType::BombPlaced:
                return gameplayEvent.bombPlaced.serverTick;
            case net::NetClient::GameplayEvent::EType::ExplosionResolved:
                return gameplayEvent.explosionResolved.serverTick;
        }

        LOG_NET_SNAPSHOT_WARN("Unhandled gameplay event type={} - treating serverTick as 0",
                              static_cast<unsigned int>(gameplayEvent.type));
        assert(false && "Unhandled gameplay event type");
        return 0;
    }

    void MultiplayerLevelScene::updateRemoteAnimationFromSnapshotDelta(RemotePlayerPresentation& presentation)
    {
        if (!presentation.latestSnapshot.valid || !presentation.playerSprite)
            return;

        if (!presentation.previousSnapshot.valid)
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
        if (!isMovingFromSnapshots)
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

        for (auto& entry : remotePlayerPresentations_)
        {
            auto& presentation = entry.second;
            if (!presentation.playerSprite)
                continue;

            if (presentation.receivedSnapshotThisUpdate)
            {
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

    void MultiplayerLevelScene::updatePowerupEffectPresentations(const unsigned int delta)
    {
        powerupBlinkAccumulatorMs_ += delta;
        const bool highlightOn =
            ((powerupBlinkAccumulatorMs_ / kPowerupBlinkIntervalMs) % 2u) == 0u;

        if (player && localPlayerId_.has_value())
        {
            const util::PlayerColor baseColor = util::colorForPlayerId(localPlayerId_.value());
            util::PlayerColor appliedColor = baseColor;
            if (localPlayerAlive_ &&
                hasVisiblePowerupEffect(localPlayerEffectFlags_) &&
                highlightOn)
            {
                appliedColor = powerupEffectColor(localPlayerEffectFlags_);
            }

            player->setColorMod(appliedColor.r, appliedColor.g, appliedColor.b);
        }

        for (auto& entry : remotePlayerPresentations_)
        {
            auto& presentation = entry.second;
            if (!presentation.playerSprite)
                continue;

            const util::PlayerColor baseColor = util::colorForPlayerId(entry.first);
            util::PlayerColor appliedColor = baseColor;
            if (hasVisiblePowerupEffect(presentation.effectFlags) && highlightOn)
            {
                appliedColor = powerupEffectColor(presentation.effectFlags);
            }

            presentation.playerSprite->setColorMod(appliedColor.r, appliedColor.g, appliedColor.b);
        }

        for (auto& entry : bombPresentations_)
        {
            auto& presentation = entry.second;
            if (!presentation.bombSprite)
                continue;

            util::PlayerColor appliedColor = util::colorForPlayerId(presentation.ownerId);
            if (presentation.radius > sim::kDefaultPlayerBombRange && highlightOn)
            {
                appliedColor = boostedBombAccentColor();
            }

            presentation.bombSprite->setColorMod(appliedColor.r, appliedColor.g, appliedColor.b);
        }
    }

    sim::TilePos MultiplayerLevelScene::computeRemotePresentedPosition(
        const RemotePlayerPresentation& presentation) const
    {
        if (!game->isRemoteSmoothingEnabled())
            return presentation.authoritativePosQ;

        if (!presentation.previousSnapshot.valid || !presentation.latestSnapshot.valid)
            return presentation.authoritativePosQ;

        const uint32_t observedSnapshotSpacingTicks =
            (presentation.latestSnapshot.serverTick > presentation.previousSnapshot.serverTick)
                ? (presentation.latestSnapshot.serverTick - presentation.previousSnapshot.serverTick)
                : 0u;

        if (observedSnapshotSpacingTicks == 0)
            return presentation.authoritativePosQ;

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
        if (!presentation.playerTag || !presentation.playerSprite)
            return;

        const int tagX = presentation.playerSprite->getPositionX() + presentation.playerSprite->getWidth() / 2 -
                         presentation.playerTag->getWidth() / 2;
        const int tagY = presentation.playerSprite->getPositionY() - presentation.playerTag->getHeight() -
                         kNameTagOffsetPx;
        presentation.playerTag->setPosition(tagX, tagY);
    }

    std::string MultiplayerLevelScene::formatPlayerTag(const uint8_t playerId)
    {
        return "P" + std::to_string(static_cast<unsigned int>(playerId) + 1u);
    }

    void MultiplayerLevelScene::onCollisionObjectSpawned(const Tile tile, const std::shared_ptr<Object>& object)
    {
        if (tile != Tile::Brick || !object || scaledTileSize <= 0)
            return;

        const int col = (object->getPositionX() - fieldPositionX) / scaledTileSize;
        const int row = (object->getPositionY() - fieldPositionY) / scaledTileSize;
        if (col < 0 || row < 0 ||
            col >= static_cast<int>(tileArrayWidth) ||
            row >= static_cast<int>(tileArrayHeight))
        {
            return;
        }

        brickPresentations_[packCellKey(static_cast<uint8_t>(col), static_cast<uint8_t>(row))] = object;
    }
} // namespace bomberman
