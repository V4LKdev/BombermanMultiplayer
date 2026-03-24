/**
 * @file MultiplayerLevelScene.h
 * @brief Multiplayer gameplay scene that bridges transport state, local prediction, and presentation.
 */

#ifndef BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H
#define BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Entities/Sound.h"
#include "Net/ClientPrediction.h"
#include "Net/NetCommon.h"
#include "Scenes/LevelScene.h"

namespace bomberman
{
    namespace net { class NetClient; }
    class Sprite;
    class Text;

    /**
     * @brief Multiplayer gameplay scene for one authoritative server session.
     *
     * This scene applies authoritative transport state to the shared `LevelScene`
     * while keeping gameplay-scene ownership rules local:
     * - the local player can be presented from raw input before prediction is armed
     * - bootstrap snapshots can seed local position before the first owner correction
     * - once prediction is initialized, local presentation comes from `ClientPrediction`
     * - remote players remain snapshot-authoritative and are interpolated
     *
     * The scene also owns gameplay-scoped leave/disconnect handling, gameplay-session HUD,
     * and client-local prediction diagnostics.
     */
    class MultiplayerLevelScene final : public LevelScene
    {
      public:
        /** @brief Constructs the multiplayer gameplay scene for the selected stage. */
        MultiplayerLevelScene(Game* game, unsigned int stage, unsigned int prevScore,
                              std::optional<uint32_t> mapSeed = std::nullopt);

        // ----- Scene Hooks -----

        /** @brief Releases multiplayer-only presentation state and logs final client-local prediction telemetry. */
        void onExit() override;

        /**
         * @brief Applies locally queued input to client-side presentation and prediction state.
         * @note Before prediction is initialized, this only updates facing and animation.
         */
        void onNetInputQueued(uint32_t inputSeq, uint8_t buttons) override;

        /** @brief Disables event-driven local movement because multiplayer polls and queues input each tick. */
        [[nodiscard]]
        bool usesEventDrivenLocalMovement() const override { return false; }

        /** @brief Enables per-tick network input polling for the multiplayer scene. */
        [[nodiscard]]
        bool wantsNetworkInputPolling() const override { return true; }

      protected:
        /** @brief Runs one multiplayer frame in correction, snapshot, then presentation order. */
        void updateLevel(unsigned int delta) override;
        void onKeyPressed(SDL_Scancode scancode) override;
        [[nodiscard]]
        bool supportsPause() const override { return false; }

      private:
        // ----- Local and Remote Snapshot State -----

        /** @brief One authoritative snapshot sample retained for bootstrap or interpolation decisions. */
        struct SnapshotSample
        {
            sim::TilePos posQ{};
            uint32_t serverTick = 0;
            bool valid = false;
        };

        /** @brief Scene-owned presentation state for one remote player entry. */
        struct RemotePlayerPresentation
        {
            std::shared_ptr<Player> playerSprite = nullptr;
            std::shared_ptr<Text> playerTag = nullptr;
            sim::TilePos authoritativePosQ{};
            SnapshotSample previousSnapshot{};
            SnapshotSample latestSnapshot{};
            MovementDirection facingDirection = MovementDirection::Right;
            float ticksSinceLatestSnapshot = 0.0f;
            bool receivedSnapshotThisUpdate = false;
        };

        /** @brief Scene-owned presentation state for one snapshot-authoritative bomb. */
        struct BombPresentation
        {
            std::shared_ptr<Sprite> bombSprite = nullptr;
            uint8_t ownerId = 0;
            uint8_t radius = 0;
        };

        /** @brief One short-lived explosion visual spawned from a reliable authoritative event. */
        struct ExplosionPresentation
        {
            std::shared_ptr<Sprite> explosionSprite = nullptr;
            uint32_t remainingLifetimeMs = 0;
        };

        /** @brief Rolling per-session telemetry mirrored into periodic diagnostic logs. */
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

        // ----- Snapshot and Correction Application -----

        /** @brief Returns the connected client or initiates a leave if the session is no longer valid. */
        [[nodiscard]]
        net::NetClient* requireConnectedNetClient();
        /** @brief Applies the newest unapplied owner correction when prediction is active. */
        void applyLatestCorrectionIfAvailable(const net::NetClient& netClient);
        /** @brief Applies the newest unapplied gameplay snapshot, if any. */
        void applyLatestSnapshotIfAvailable();
        /** @brief Applies any pending reliable gameplay events after snapshot state in original receive order. */
        void applyPendingGameplayEvents(net::NetClient& netClient);
        /** @brief Ticks scene objects, remote presentation, camera, and diagnostics after state application. */
        void finalizeFrameUpdate(unsigned int delta);

        /** @brief Applies one authoritative gameplay snapshot to local and remote scene state. */
        void applySnapshot(const net::MsgSnapshot& snapshot);
        /** @brief Ensures the local player's multiplayer color and tag match the assigned player ID. */
        void ensureLocalPresentation(uint8_t localId);
        /** @brief Seeds local player presentation from the assigned player-id spawn slot before the first snapshot/correction arrives. */
        void seedLocalSpawnFromAssignedPlayerId();
        /** @brief Reconciles prediction against one owner correction and updates local presentation. */
        void applyAuthoritativeCorrection(const net::MsgCorrection& correction);
        /** @brief Logs replay truncation and recovery outcomes for one applied correction. */
        static void logCorrectionReplayOutcome(const net::MsgCorrection& correction,
                                               const net::CorrectionReplayResult& replayResult);
        /** @brief Stores the latest correction outcome for live diagnostics and summary logging. */
        void storeCorrectionTelemetry(const net::MsgCorrection& correction,
                                      const net::CorrectionReplayResult& replayResult);
        /** @brief Seeds local position from snapshots before prediction is initialized. */
        void applyBootstrapLocalSnapshot(const net::MsgSnapshot::PlayerEntry& entry);
        /** @brief Updates local-facing presentation directly from raw input buttons. */
        void updateLocalPresentationFromInputButtons(uint8_t buttons);
        /** @brief Syncs local position and facing from the current prediction-owned state. */
        void syncLocalPresentationFromOwnedState(const net::LocalPlayerState& localState);
        /** @brief Advances shared scene objects without re-entering `LevelScene::updateLevel()`. */
        void updateSceneObjects(unsigned int delta);

        // ----- Session Health and Diagnostics -----

        /** @brief Refreshes the multiplayer gameplay-health HUD from recent gameplay traffic. */
        void updateGameplayConnectionHealth(const net::NetClient& netClient);
        /** @brief Toggles the degraded-gameplay HUD and logs connection-health transitions. */
        void setGameplayConnectionDegraded(bool degraded, uint32_t silenceMs = 0);
        /** @brief Emits periodic live prediction telemetry while multiplayer input is active. */
        void logLivePredictionTelemetry(unsigned int delta);
        /** @brief Tracks the highest observed unacknowledged local input depth for this scene session. */
        void updateMaxPendingInputDepth();
        /** @brief Returns the current number of locally recorded inputs not yet acknowledged by corrections. */
        [[nodiscard]]
        uint32_t pendingInputDepth() const;
        /** @brief Emits one end-of-session client-local prediction summary before the scene exits. */
        void logPredictionSummary() const;
        /** @brief Applies the snapshot-owned local state path when prediction is disabled or unarmed. */
        void applyAuthoritativeLocalSnapshot(const net::MsgSnapshot::PlayerEntry& entry);
        /** @brief Toggles local-player presentation visibility from authoritative alive state. */
        void setLocalPlayerAlivePresentation(bool alive);
        /** @brief Toggles local-player control lock from authoritative snapshot state. */
        void setLocalPlayerInputLock(bool locked);
        /** @brief Repositions the local multiplayer tag above the current player sprite. */
        void updateLocalPlayerTagPosition();
        /** @brief Removes one remote-player presentation immediately after an authoritative kill event. */
        void removeRemotePlayerPresentation(uint8_t playerId);

        // ----- Remote Player Presentation -----

        /** @brief Applies snapshot membership and state updates to all remote player presentations. */
        void applySnapshotToRemotePlayers(const net::MsgSnapshot& snapshot, uint8_t localId);
        /** @brief Applies snapshot-owned bomb membership and positions to scene presentation. */
        void applySnapshotBombs(const net::MsgSnapshot& snapshot);
        /** @brief Creates or refreshes one remote player's snapshot-owned presentation state. */
        void updateOrCreateRemotePlayer(uint8_t playerId, int16_t xQ, int16_t yQ, uint32_t snapshotTick);
        /** @brief Creates or refreshes one snapshot-owned bomb presentation for the given tile cell. */
        void updateOrCreateBombPresentation(const net::MsgSnapshot::BombEntry& entry);
        /** @brief Removes remote player presentations absent from the newest authoritative snapshot. */
        void pruneMissingRemotePlayers(const std::unordered_set<uint8_t>& seenRemoteIds);
        /** @brief Removes bomb presentations absent from the newest authoritative snapshot. */
        void pruneMissingBombPresentations(const std::unordered_set<uint16_t>& seenBombCells);
        /** @brief Removes every remote player presentation object owned by this scene. */
        void removeAllRemotePlayers();
        /** @brief Removes every snapshot-owned bomb presentation object owned by this scene. */
        void removeAllSnapshotBombs();
        /** @brief Applies one reliable authoritative bomb-placement event for presentation acceleration. */
        void applyBombPlacedEvent(const net::MsgBombPlaced& bombPlaced);
        /** @brief Applies one reliable authoritative explosion-resolution event to client world presentation. */
        void applyExplosionResolvedEvent(const net::MsgExplosionResolved& explosion);
        /** @brief Removes one locally tracked brick presentation and updates collision tiles for that cell. */
        void destroyBrickPresentation(uint8_t col, uint8_t row);
        /** @brief Removes one bomb presentation immediately, if present. */
        void removeBombPresentation(uint8_t col, uint8_t row);
        /** @brief Spawns one short-lived explosion sprite at the given tile cell. */
        void spawnExplosionPresentation(const net::MsgCell& cell);
        /** @brief Retires expired explosion sprites after their visual lifetime ends. */
        void updateExplosionPresentations(unsigned int delta);
        /** @brief Removes every active explosion sprite owned by this scene. */
        void removeAllExplosionPresentations();

        /** @brief Stores one remote snapshot sample for interpolation and movement inference. */
        static void recordSnapshotSample(RemotePlayerPresentation& presentation,
                                         int16_t xQ,
                                         int16_t yQ,
                                         uint32_t serverTick);
        /** @brief Updates remote-facing animation from snapshot delta rather than local input. */
        static void updateRemoteAnimationFromSnapshotDelta(RemotePlayerPresentation& presentation);
        /** @brief Advances interpolation and on-screen positions for all remote presentations. */
        void updateRemotePlayerPresentations(unsigned int delta);
        /** @brief Computes the current on-screen remote position from snapshot authority and smoothing settings. */
        [[nodiscard]]
        sim::TilePos computeRemotePresentedPosition(const RemotePlayerPresentation& presentation) const;
        /** @brief Repositions one remote multiplayer tag above its current sprite location. */
        static void updateRemotePlayerTagPosition(RemotePlayerPresentation& presentation);
        /** @brief Formats the compact multiplayer label shown above each player. */
        [[nodiscard]]
        static std::string formatPlayerTag(uint8_t playerId);
        /** @brief Leaves the multiplayer scene and optionally disconnects the active network client. */
        void returnToMenu(bool disconnectClient, std::string_view reason);
        /** @brief Tracks spawned collision objects by tile cell so reliable destruction can remove them cleanly. */
        void onCollisionObjectSpawned(Tile tile, const std::shared_ptr<Object>& object) override;

        // ----- State -----

        uint32_t lastAppliedSnapshotTick_ = 0;
        uint32_t lastAppliedCorrectionTick_ = 0;
        std::optional<uint8_t> localPlayerId_{};
        std::shared_ptr<Text> localPlayerTag_ = nullptr;
        std::shared_ptr<Text> gameplayStatusText_ = nullptr;
        net::ClientPrediction localPrediction_{};
        LivePredictionTelemetry livePredictionTelemetry_{};
        MovementDirection localFacingDirection_ = MovementDirection::Right;
        uint32_t livePredictionLogAccumulatorMs_ = 0;
        uint32_t lastAppliedGameplayEventTick_ = 0;
        bool localPlayerAlive_ = true;
        bool localPlayerInputLocked_ = false;
        std::shared_ptr<Sound> explosionSound_ = nullptr;

        std::unordered_map<uint8_t, RemotePlayerPresentation> remotePlayerPresentations_;
        std::unordered_map<uint16_t, BombPresentation> bombPresentations_;
        std::unordered_map<uint16_t, std::shared_ptr<Object>> brickPresentations_;
        std::vector<ExplosionPresentation> explosionPresentations_;
        bool gameplayConnectionDegraded_ = false;
        bool returningToMenu_ = false;
    };
} // namespace bomberman

#endif // BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H
