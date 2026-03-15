#ifndef BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H
#define BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "Net/NetCommon.h"
#include "Scenes/LevelScene.h"

namespace bomberman
{
    class Text;

    /**
     * @brief Multiplayer presentation scene driven by authoritative server snapshots.
     *
     * Snapshot player entries are treated as authoritative membership+position state.
     * The local player is still applied through LevelScene's canonical player position,
     * while remote players are represented as scene-owned Player objects keyed by playerId.
     */
    class MultiplayerLevelScene final : public LevelScene
    {
      public:
        MultiplayerLevelScene(Game* game, unsigned int stage, unsigned int prevScore,
                              std::optional<uint32_t> mapSeed = std::nullopt);

      protected:
        virtual void updateLevel(unsigned int delta) override;
        virtual void onExit() override;
        [[nodiscard]]
        bool supportsPause() const override { return false; }

      private:
        struct SnapshotSample
        {
            sim::TilePos posQ{};
            uint32_t tick = 0;
            bool valid = false;
        };

        struct RemotePlayerView
        {
            std::shared_ptr<Player> sprite = nullptr;
            std::shared_ptr<Text> nameTag = nullptr;
            sim::TilePos authoritativePosQ{};
            SnapshotSample previousSample{};
            SnapshotSample latestSample{};
            MovementDirection lastFacing = MovementDirection::Right;
            bool isMoving = false;
            uint32_t lastSeenSnapshotTick = 0;
        };

        void applySnapshot(const net::MsgSnapshot& snapshot);
        void ensureLocalPresentation(uint8_t localId);
        void applyLocalSnapshotSample(const net::MsgSnapshot::PlayerEntry& entry, uint8_t localId, uint32_t tick);
        void updateLocalNameTagPosition();

        void syncRemotePlayersFromSnapshot(const net::MsgSnapshot& snapshot, uint8_t localId);
        void upsertRemotePlayer(uint8_t playerId, int16_t xQ, int16_t yQ, uint32_t snapshotTick);
        void removeMissingRemotePlayers(const std::unordered_map<uint8_t, bool>& seenRemoteIds);
        void removeAllRemotePlayers();

        void updateSnapshotHistory(RemotePlayerView& view, int16_t xQ, int16_t yQ, uint32_t tick);
        void updateAnimationFromSnapshotDelta(RemotePlayerView& view);
        [[nodiscard]]
        sim::TilePos resolvePresentedPosition(const RemotePlayerView& view) const;
        void updateRemoteNameTagPosition(RemotePlayerView& view, uint8_t playerId);
        [[nodiscard]]
        static std::string makePlayerTagText(uint8_t playerId);

        uint32_t lastAppliedSnapshotTick_ = 0;
        uint8_t localPlayerId_ = 0xFF;
        std::shared_ptr<Text> localNameTag_ = nullptr;
        SnapshotSample localPreviousSample_{};
        SnapshotSample localLatestSample_{};
        MovementDirection localLastFacing_ = MovementDirection::Right;
        bool localIsMoving_ = false;

        std::unordered_map<uint8_t, RemotePlayerView> remotePlayers_;
    };
} // namespace bomberman

#endif // BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H


