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
        net::MsgSnapshot snapshot{};
        if(game->tryGetLatestSnapshot(snapshot))
        {
            applySnapshot(snapshot);
        }
        Scene::update(delta);
        updateLocalNameTagPosition();
        updateCamera();
    }

    void MultiplayerLevelScene::onExit()
    {
        removeAllRemotePlayers();

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

        game->disconnectNetClientIfActive();
        game->getSceneManager()->activateScene("menu");
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

    sim::TilePos MultiplayerLevelScene::resolvePresentedPosition(const RemotePlayerView& view) const
    {
        // Stub for future interpolation/extrapolation strategy.
        // For now we present immediate authoritative positions.
        return view.authoritativePosQ;
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
} // namespace bomberman
