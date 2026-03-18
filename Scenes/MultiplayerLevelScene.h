#ifndef BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H
#define BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Net/ClientPrediction.h"
#include "Net/NetCommon.h"
#include "Scenes/LevelScene.h"

namespace bomberman
{
    namespace net { class NetClient; }
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
        virtual void onKeyPressed(SDL_Scancode scancode) override;
        virtual void onNetworkInputSent(uint32_t inputSeq, uint8_t buttons) override;
        [[nodiscard]]
        bool supportsPause() const override { return false; }
        [[nodiscard]]
        bool usesEventDrivenLocalMovement() const override { return false; }
        [[nodiscard]]
        bool wantsNetworkInputPolling() const override { return true; }

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
            sim::TilePos presentedPosQ{};
            SnapshotSample previousSample{};
            SnapshotSample latestSample{};
            MovementDirection lastFacing = MovementDirection::Right;
            bool isMoving = false;
            float ticksSinceLatestSample = 0.0f;
            bool receivedSampleThisUpdate = false;
            uint32_t lastSeenSnapshotTick = 0;
        };

        struct LivePredictionTelemetry
        {
            uint32_t lastAckedInputSeq = 0;
            uint32_t lastCorrectionServerTick = 0;
            uint32_t lastCorrectionDeltaQ = 0;
            uint32_t lastReplayCount = 0;
            uint32_t lastMissingInputs = 0;
            uint32_t lastRemainingDeferredInputs = 0;
            uint32_t maxPendingInputDepth = 0;
            uint32_t recoveryCatchUpSeq = 0;
            bool recoveryActive = false;
        };

        void applySnapshot(const net::MsgSnapshot& snapshot);
        void ensureLocalPresentation(uint8_t localId);
        void applyAuthoritativeCorrection(const net::MsgCorrection& correction);
        void applyBootstrapLocalSnapshotSample(const net::MsgSnapshot::PlayerEntry& entry, uint32_t tick);
        void syncLocalPresentationFromInputButtons(uint8_t buttons);
        void syncLocalPresentationFromPredictedState(const net::PredictedPlayerState& predictedState);
        void updateGameplaySessionHealth(const net::NetClient& netClient);
        void setGameplayDegraded(bool degraded, uint32_t silenceMs = 0);
        void logLivePredictionTelemetry(unsigned int delta);
        void updateLivePredictionPendingDepth();
        [[nodiscard]]
        uint32_t currentPendingInputDepth() const;
        void logPredictionDiagnosticsSummary() const;
        void applyLocalSnapshotSample(const net::MsgSnapshot::PlayerEntry& entry, uint8_t localId, uint32_t tick);
        void updateLocalNameTagPosition();

        void syncRemotePlayersFromSnapshot(const net::MsgSnapshot& snapshot, uint8_t localId);
        void upsertRemotePlayer(uint8_t playerId, int16_t xQ, int16_t yQ, uint32_t snapshotTick);
        void removeMissingRemotePlayers(const std::unordered_map<uint8_t, bool>& seenRemoteIds);
        void removeAllRemotePlayers();

        void updateSnapshotHistory(RemotePlayerView& view, int16_t xQ, int16_t yQ, uint32_t tick);
        void updateAnimationFromSnapshotDelta(RemotePlayerView& view);
        void updateRemotePresentations(unsigned int delta);
        [[nodiscard]]
        sim::TilePos resolvePresentedPosition(const RemotePlayerView& view) const;
        void updateRemoteNameTagPosition(RemotePlayerView& view, uint8_t playerId);
        [[nodiscard]]
        static std::string makePlayerTagText(uint8_t playerId);
        void leaveToMenu(bool disconnectClient, std::string_view reason);

        uint32_t lastAppliedSnapshotTick_ = 0;
        uint32_t lastAppliedCorrectionTick_ = 0;
        uint8_t localPlayerId_ = 0xFF;
        std::shared_ptr<Text> localNameTag_ = nullptr;
        std::shared_ptr<Text> gameplayStatusText_ = nullptr;
        SnapshotSample localPreviousSample_{};
        SnapshotSample localLatestSample_{};
        net::ClientPrediction localPrediction_{};
        LivePredictionTelemetry livePredictionTelemetry_{};
        MovementDirection localLastFacing_ = MovementDirection::Right;
        bool localIsMoving_ = false;
        uint32_t livePredictionLogAccumulatorMs_ = 0;

        std::unordered_map<uint8_t, RemotePlayerView> remotePlayers_;
        bool gameplayDegraded_ = false;
        bool leavingToMenu_ = false;
    };
} // namespace bomberman

#endif // BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H
