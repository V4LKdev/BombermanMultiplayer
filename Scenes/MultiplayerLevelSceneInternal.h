/**
 * @file MultiplayerLevelSceneInternal.h
 * @brief Internal helpers shared by MultiplayerLevelScene implementation files.
 */

#ifndef BOMBERMAN_SCENES_MULTIPLAYERLEVELSCENEINTERNAL_H
#define BOMBERMAN_SCENES_MULTIPLAYERLEVELSCENEINTERNAL_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "Const.h"
#include "Core/Animation.h"
#include "Entities/Sprite.h"
#include "Net/NetCommon.h"
#include "Scenes/MultiplayerLevelScene.h"
#include "Sim/SimConfig.h"
#include "Sim/SpawnSlots.h"
#include "Util/PlayerColors.h"

namespace bomberman::multiplayer_level_scene_internal
{
    static_assert(sim::kDefaultSpawnSlots.size() > 0, "Spawn slot table must not be empty");
    static_assert(net::kMaxPlayers <= util::kPlayerColorCount,
                  "Player color table must cover all supported multiplayer player ids");

    inline constexpr int kMovementDeltaThresholdQ = 2;
    inline constexpr int kNameTagOffsetPx = 6;
    inline constexpr int kNameTagMinPointSize = 12;
    inline constexpr int kNameTagMaxPointSize = 20;
    inline constexpr uint32_t kGameplayDegradedThresholdMs = 2000;
    inline constexpr int kGameplayStatusOffsetY = 12;
    inline constexpr int kDebugHudOffsetX = 12;
    inline constexpr int kDebugHudOffsetY = 12;
    inline constexpr int kDebugHudPointSize = 12;
    inline constexpr int kDebugHudLineGapPx = 4;
    inline constexpr uint32_t kDebugHudRefreshIntervalMs = 250;
    inline constexpr int kCenterBannerPointSize = 56;
    inline constexpr int kCenterBannerDetailPointSize = 40;
    inline constexpr int kCenterBannerLineGapPx = 10;
    inline constexpr uint32_t kCenterBannerDurationTicks = static_cast<uint32_t>(sim::kTickRate);
    inline constexpr uint32_t kLivePredictionLogIntervalMs = 1000;
    inline constexpr uint32_t kSimulationTickMs = 1000u / static_cast<uint32_t>(sim::kTickRate);
    inline constexpr uint32_t kPreStartReturnTimeoutMs = 7000;
    inline constexpr uint32_t kPowerupBlinkIntervalMs = 140;
    inline constexpr int kBombAnimationFrameCount = 4;
    inline constexpr int kBoostedBombExtraPx = 4;
    inline constexpr int kExplosionAnimationStartFrame = 1;
    inline constexpr int kExplosionAnimationFrameCount = 11;
    inline constexpr uint32_t kExplosionLifetimeMs = 800;

    [[nodiscard]]
    constexpr uint16_t packCellKey(const uint8_t col, const uint8_t row)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(row) << 8u) | static_cast<uint16_t>(col));
    }

    inline void attachBombAnimation(const std::shared_ptr<Sprite>& bombSprite)
    {
        if (!bombSprite)
            return;

        auto animation = std::make_shared<Animation>();
        for (int frame = 0; frame < kBombAnimationFrameCount; ++frame)
        {
            animation->addAnimationEntity(AnimationEntity(tileSize * frame, 0, tileSize, tileSize));
        }

        animation->setSprite(bombSprite.get());
        bombSprite->addAnimation(animation);
        animation->play();
    }

    inline void attachExplosionAnimation(const std::shared_ptr<Sprite>& explosionSprite)
    {
        if (!explosionSprite)
            return;

        auto animation = std::make_shared<Animation>();
        for (int frame = 0; frame < kExplosionAnimationFrameCount; ++frame)
        {
            animation->addAnimationEntity(
                AnimationEntity(tileSize * (kExplosionAnimationStartFrame + frame), 0, tileSize, tileSize));
        }

        animation->setSprite(explosionSprite.get());
        explosionSprite->addAnimation(animation);
        animation->play();
    }

    [[nodiscard]]
    inline bool snapshotEntryIsAlive(const net::MsgSnapshot::PlayerEntry& entry)
    {
        const auto flags = static_cast<uint8_t>(entry.flags);
        return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::Alive)) != 0;
    }

    [[nodiscard]]
    inline bool snapshotEntryInputLocked(const net::MsgSnapshot::PlayerEntry& entry)
    {
        const auto flags = static_cast<uint8_t>(entry.flags);
        return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::InputLocked)) != 0;
    }

    [[nodiscard]]
    inline bool snapshotEntryInvulnerable(const net::MsgSnapshot::PlayerEntry& entry)
    {
        const auto flags = static_cast<uint8_t>(entry.flags);
        return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::Invulnerable)) != 0;
    }

    [[nodiscard]]
    inline bool snapshotEntryBombRangeBoost(const net::MsgSnapshot::PlayerEntry& entry)
    {
        const auto flags = static_cast<uint8_t>(entry.flags);
        return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::BombRangeBoost)) != 0;
    }

    [[nodiscard]]
    inline bool snapshotEntryMaxBombsBoost(const net::MsgSnapshot::PlayerEntry& entry)
    {
        const auto flags = static_cast<uint8_t>(entry.flags);
        return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::MaxBombsBoost)) != 0;
    }

    [[nodiscard]]
    inline bool snapshotEntrySpeedBoost(const net::MsgSnapshot::PlayerEntry& entry)
    {
        const auto flags = static_cast<uint8_t>(entry.flags);
        return (flags & static_cast<uint8_t>(net::MsgSnapshot::PlayerEntry::EPlayerFlags::SpeedBoost)) != 0;
    }

    [[nodiscard]]
    inline MovementDirection inferDirectionFromDelta(const int dxQ,
                                                     const int dyQ,
                                                     const MovementDirection fallback)
    {
        const int absDx = std::abs(dxQ);
        const int absDy = std::abs(dyQ);

        if (absDx < kMovementDeltaThresholdQ && absDy < kMovementDeltaThresholdQ)
            return fallback;

        if (absDx >= absDy)
            return dxQ >= 0 ? MovementDirection::Right : MovementDirection::Left;

        return dyQ >= 0 ? MovementDirection::Down : MovementDirection::Up;
    }

    [[nodiscard]]
    inline int computeTagPointSize(const int scaledTileSize)
    {
        return std::clamp(scaledTileSize / 3, kNameTagMinPointSize, kNameTagMaxPointSize);
    }

    [[nodiscard]]
    inline MovementDirection inferDirectionFromButtons(const uint8_t buttons)
    {
        const int8_t moveX = net::buttonsToMoveX(buttons);
        const int8_t moveY = net::buttonsToMoveY(buttons);

        if (moveX == 0 && moveY == 0)
            return MovementDirection::None;

        if (moveX != 0)
            return moveX > 0 ? MovementDirection::Right : MovementDirection::Left;

        return moveY > 0 ? MovementDirection::Down : MovementDirection::Up;
    }
} // namespace bomberman::multiplayer_level_scene_internal

#endif // BOMBERMAN_SCENES_MULTIPLAYERLEVELSCENEINTERNAL_H
