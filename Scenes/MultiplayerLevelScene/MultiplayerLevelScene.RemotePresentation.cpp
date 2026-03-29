/** @file MultiplayerLevelScene.RemotePresentation.cpp
 *  @brief Remote player presentation and smoothing logic.
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
#include "Net/NetClient.h"
#include "Util/Log.h"

namespace bomberman
{
    using namespace multiplayer_level_scene_internal;

    // =============================================================================================================
    // ===== Remote Player Presentation =============================================================================
    // =============================================================================================================

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
                    // Snapshot wins when it is same-tick or newer than the latest correction, but older snapshots must
                    // not roll owner-local effect/loadout flags back behind a newer correction.
                    localPlayerEffectFlags_ = rawFlags;
                }

                // Owner death/input-lock presentation remains snapshot/reliable-event authoritative.
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

        presentation.authoritativePosQ = {xQ, yQ};

        const sim::TilePos presentedPosQ = computeRemotePresentedPosition(presentation);
        const int screenX = sim::tileQToScreenTopLeft(presentedPosQ.xQ, fieldPositionX, scaledTileSize, 0);
        const int screenY = sim::tileQToScreenTopLeft(presentedPosQ.yQ, fieldPositionY, scaledTileSize, 0);
        presentation.playerSprite->setPosition(screenX, screenY);

        updateRemotePlayerTagPosition(presentation);
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

    // =============================================================================================================
    // ===== Remote Snapshot Smoothing ==============================================================================
    // =============================================================================================================

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
} // namespace bomberman
