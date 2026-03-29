/** @file MultiplayerLevelScene.WorldPresentation.cpp
 *  @brief Bomb, powerup, and world-effect presentation logic.
 *  @ingroup multiplayer_level_scene
 */

#include "Scenes/MultiplayerLevelScene/MultiplayerLevelSceneInternal.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <unordered_set>

#include <SDL.h>

#include "Entities/Player.h"
#include "Entities/Text.h"
#include "Game.h"
#include "Net/Client/NetClient.h"
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
    } // namespace

    // =============================================================================================================
    // ===== Snapshot-Owned World Objects ===========================================================================
    // =============================================================================================================

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
            for (auto pendingIt = pendingLocalBombPlacements_.begin(); pendingIt != pendingLocalBombPlacements_.end();
                 ++pendingIt)
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

    // =============================================================================================================
    // ===== Gameplay Events and Temporary Effects ==================================================================
    // =============================================================================================================

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

    // =============================================================================================================
    // ===== Shared World Visuals ==================================================================================
    // =============================================================================================================

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
