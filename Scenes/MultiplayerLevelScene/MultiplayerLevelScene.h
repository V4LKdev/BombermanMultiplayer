/**
 * @file MultiplayerLevelScene.h
 * @brief Multiplayer gameplay scene interface.
 * @ingroup multiplayer_level_scene
 */

#ifndef BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H
#define BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Entities/Sound.h"
#include "Net/ClientPrediction.h"
#include "Net/Client/NetClient.h"
#include "Net/NetCommon.h"
#include "Scenes/LevelScene.h"

namespace bomberman
{
    class Sprite;
    class Text;

    /**
     * @brief Client-side scene for one authoritative multiplayer match.
     * @ingroup multiplayer_level_scene
     */
    class MultiplayerLevelScene final : public LevelScene
    {
      public:
        // =============================================================================================================
        // ===== Construction and Scene Hooks ==========================================================================
        // =============================================================================================================

        /** @brief Construct the scene from stage data. */
        MultiplayerLevelScene(Game* game, unsigned int stage, unsigned int prevScore,
                              std::optional<uint32_t> mapSeed = std::nullopt);
        /** @brief Construct the scene from authoritative level info. */
        MultiplayerLevelScene(Game* game, unsigned int stage, unsigned int prevScore,
                              const net::MsgLevelInfo& levelInfo);

        /** @brief Release multiplayer scene state before leaving the scene. */
        void onExit() override;
        /** @brief Consume one locally queued input for prediction and presentation. */
        void onNetInputQueued(uint32_t inputSeq, uint8_t buttons) override;
        [[nodiscard]]
        /** @brief This scene applies movement from queued network input instead of events. */
        bool usesEventDrivenLocalMovement() const override { return false; }
        [[nodiscard]]
        /** @brief Return whether local gameplay input should currently be polled. */
        bool wantsNetworkInputPolling() const override;

      protected:
        /** @brief Run one multiplayer gameplay frame. */
        void updateLevel(unsigned int delta) override;
        /** @brief Handle scene-local key presses such as leaving the match. */
        void onKeyPressed(SDL_Scancode scancode) override;
        [[nodiscard]]
        /** @brief Multiplayer matches cannot be paused locally. */
        bool supportsPause() const override { return false; }

      private:
        // =============================================================================================================
        // ===== Scene State ===========================================================================================
        // =============================================================================================================

        // ----- Local and Remote Snapshot State -----

        /** @brief One remote snapshot sample used for interpolation. */
        struct SnapshotSample
        {
            sim::TilePos posQ{};
            uint32_t serverTick = 0;
            bool valid = false;
        };

        /** @brief Render state for one remote player. */
        struct RemotePlayerPresentation
        {
            std::shared_ptr<Player> playerSprite = nullptr;
            std::shared_ptr<Text> playerTag = nullptr;
            sim::TilePos authoritativePosQ{};
            SnapshotSample previousSnapshot{};
            SnapshotSample latestSnapshot{};
            MovementDirection facingDirection = MovementDirection::Right;
            float ticksSinceLatestSnapshot = 0.0f;
            bool receivedSnapshotThisUpdate = false; ///< Prevents aging a fresh sample in the same frame.
            uint8_t effectFlags = 0;
        };

        /** @brief Active bomb presentation tracked from authoritative state. */
        struct BombPresentation
        {
            std::shared_ptr<Sprite> bombSprite = nullptr;
            uint8_t ownerId = 0;
            uint8_t radius = 0;
        };

        /** @brief Active powerup presentation tracked from authoritative state. */
        struct PowerupPresentation
        {
            std::shared_ptr<Sprite> powerupSprite = nullptr;
            sim::PowerupType type = sim::PowerupType::SpeedBoost;
        };

        /** @brief Temporary explosion effect. */
        struct ExplosionPresentation
        {
            std::shared_ptr<Sprite> explosionSprite = nullptr;
            uint32_t remainingLifetimeMs = 0;
        };

        /** @brief Temporary local bomb feedback effect. */
        struct BombSparkPresentation
        {
            std::shared_ptr<Sprite> sparkSprite = nullptr;
            uint32_t remainingLifetimeMs = 0;
        };

        /** @brief Local bomb placement reserved until authority catches up. */
        struct PendingLocalBombPlacement
        {
            uint16_t cellKey = 0;
            uint32_t remainingLifetimeMs = 0;
        };

        /** @brief Live prediction stats used by the debug HUD and logs. */
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

        // =============================================================================================================
        // ===== Match Flow and Authority ==============================================================================
        // =============================================================================================================

        [[nodiscard]]
        /** @brief Return the active net client or `nullptr` if multiplayer is no longer valid. */
        net::NetClient* requireConnectedNetClient();
        /** @brief Create scene-local multiplayer presentation objects. */
        void initializeMultiplayerScenePresentation();
        /** @brief Acknowledge that the match scene finished loading. */
        void ensureMatchLoadedAckSent(net::NetClient& netClient);

        [[nodiscard]]
        /** @brief Advance bootstrap flow and report whether gameplay may continue this frame. */
        bool updatePreStartFlow(net::NetClient& netClient);
        /** @brief Update match-start state and GO banner flow. */
        void updateMatchStartFlow(net::NetClient& netClient);
        [[nodiscard]]
        /** @brief Handle server-directed return to the lobby scene. */
        bool updateLobbyReturnFlow(net::NetClient& netClient);
        /** @brief Update result state and end-of-match presentation. */
        void updateMatchResultFlow(net::NetClient& netClient);

        /** @brief Merge the latest authoritative corrections, snapshots, and gameplay events. */
        void consumeAuthoritativeNetState(net::NetClient& netClient);
        /** @brief Apply the newest local-owner correction if one is available. */
        void applyLatestCorrectionIfAvailable(const net::NetClient& netClient);

        /** @brief Drain queued reliable gameplay events from the net client. */
        void collectPendingGameplayEvents(net::NetClient& netClient);
        /** @brief Apply the next queued gameplay event in authoritative order. */
        void applyNextPendingGameplayEvent();

        [[nodiscard]]
        /** @brief Return whether local gameplay input is currently allowed. */
        bool allowsLocalGameplayInput() const;
        /** @brief Finish the frame by updating scene objects and presentation. */
        void finalizeFrameUpdate(unsigned int delta);

        /** @brief Apply one authoritative snapshot to the scene. */
        void applySnapshot(const net::MsgSnapshot& snapshot);
        [[nodiscard]]
        /** @brief Return whether a gameplay event should still be applied. */
        bool shouldApplyGameplayEvent(uint32_t gameplayEventTick, const char* gameplayEventName) const;

        /** @brief Apply authoritative brick destruction tiles from an explosion result. */
        void applyExplosionResolvedTiles(const net::MsgExplosionResolved& explosion);
        /** @brief Ensure the local player scene objects exist. */
        void ensureLocalPresentation(uint8_t localId);
        /** @brief Seed the local spawn point from the assigned player id. */
        void seedLocalSpawnFromAssignedPlayerId();
        /** @brief Reconcile local prediction against an authoritative correction. */
        void applyAuthoritativeCorrection(const net::MsgCorrection& correction);
        /** @brief Log the outcome of a prediction replay pass. */
        static void logCorrectionReplayOutcome(const net::MsgCorrection& correction,
                                               const net::CorrectionReplayResult& replayResult);
        /** @brief Store live telemetry from the latest prediction replay pass. */
        void storeCorrectionTelemetry(const net::MsgCorrection& correction,
                                      const net::CorrectionReplayResult& replayResult);
        /** @brief Seed local state from the first valid authoritative snapshot. */
        void applyBootstrapLocalSnapshot(const net::MsgSnapshot::PlayerEntry& entry);
        /** @brief Update local facing and animation from raw input buttons. */
        void updateLocalPresentationFromInputButtons(uint8_t buttons);
        /** @brief Sync the local presentation from prediction-owned state. */
        void syncLocalPresentationFromOwnedState(const net::LocalPlayerState& localState);
        /** @brief Update scene-owned objects without re-running level logic. */
        void updateSceneObjects(unsigned int delta);

        // =============================================================================================================
        // ===== Presentation and Diagnostics ==========================================================================
        // =============================================================================================================

        /** @brief Update connection-health presentation for live gameplay. */
        void updateGameplayConnectionHealth(const net::NetClient& netClient);
        /** @brief Toggle degraded-connection presentation state. */
        void setGameplayConnectionDegraded(bool degraded, uint32_t silenceMs = 0);
        /** @brief Periodically log live prediction telemetry. */
        void logLivePredictionTelemetry(unsigned int delta);
        /** @brief Track the maximum locally pending input depth seen so far. */
        void updateMaxPendingInputDepth();
        [[nodiscard]]
        /** @brief Return the current number of locally pending predicted inputs. */
        uint32_t pendingInputDepth() const;
        /** @brief Log a compact prediction summary when leaving the scene. */
        void logPredictionSummary() const;
        [[nodiscard]]
        /** @brief Return whether owner prediction work should still run. */
        bool shouldProcessOwnerPrediction() const;
        [[nodiscard]]
        /** @brief Return whether local state should currently come from prediction. */
        bool shouldOwnLocalStateFromPrediction() const;
        [[nodiscard]]
        /** @brief Return the most recent authoritative gameplay tick known to the client. */
        uint32_t currentAuthoritativeGameplayTick(const net::NetClient& netClient) const;

        /** @brief Show a single-line center banner. */
        void showCenterBanner(std::string_view message, SDL_Color color);
        /** @brief Show a two-line center banner. */
        void showCenterBanner(std::string_view mainMessage, std::string_view detailMessage, SDL_Color color);
        /** @brief Hide the center banner. */
        void hideCenterBanner();

        /** @brief Apply authoritative local state when prediction does not own it. */
        void applyAuthoritativeLocalSnapshot(const net::MsgSnapshot::PlayerEntry& entry);
        /** @brief Update the local death banner flow. */
        void updateDeathBannerFlow();
        /** @brief Toggle local alive/dead presentation state. */
        void setLocalPlayerAlivePresentation(bool alive);
        /** @brief Toggle authoritative local input lock state. */
        void setLocalPlayerInputLock(bool locked);
        /** @brief Keep the local player tag aligned with the local sprite. */
        void updateLocalPlayerTagPosition();
        /** @brief Update temporary powerup effect visuals. */
        void updatePowerupEffectPresentations(unsigned int delta);
        /** @brief Lazily create debug HUD text objects. */
        void ensureDebugHudPresentations();
        /** @brief Refresh the debug HUD. */
        void updateDebugHud(unsigned int delta);
        /** @brief Remove all debug HUD objects. */
        void removeDebugHudPresentations();
        /** @brief Remove one remote player presentation by player id. */
        void removeRemotePlayerPresentation(uint8_t playerId);

        // ----- Remote Player Presentation -----

        /** @brief Apply remote player state from a snapshot. */
        void applySnapshotToRemotePlayers(const net::MsgSnapshot& snapshot, uint8_t localId);
        /** @brief Apply snapshot bomb state. */
        void applySnapshotBombs(const net::MsgSnapshot& snapshot);
        /** @brief Apply snapshot powerup state. */
        void applySnapshotPowerups(const net::MsgSnapshot& snapshot);
        /** @brief Update or create one remote player presentation. */
        void updateOrCreateRemotePlayer(uint8_t playerId, int16_t xQ, int16_t yQ, uint32_t snapshotTick, uint8_t flags);
        /** @brief Update or create one bomb presentation. */
        void updateOrCreateBombPresentation(const net::MsgSnapshot::BombEntry& entry);
        /** @brief Update or create one powerup presentation. */
        void updateOrCreatePowerupPresentation(const net::MsgSnapshot::PowerupEntry& entry);
        /** @brief Remove remote players missing from the latest snapshot. */
        void pruneMissingRemotePlayers(const std::unordered_set<uint8_t>& seenRemoteIds);
        /** @brief Remove bombs missing from the latest snapshot. */
        void pruneMissingBombPresentations(const std::unordered_set<uint16_t>& seenBombCells);
        /** @brief Remove powerups missing from the latest snapshot. */
        void pruneMissingPowerupPresentations(const std::unordered_set<uint16_t>& seenPowerupCells);
        /** @brief Remove all remote player presentation state. */
        void removeAllRemotePlayers();
        /** @brief Remove all snapshot bomb presentation state. */
        void removeAllSnapshotBombs();
        /** @brief Remove all snapshot powerup presentation state. */
        void removeAllSnapshotPowerups();
        /** @brief Apply one authoritative bomb-placement event. */
        void applyBombPlacedEvent(const net::MsgBombPlaced& bombPlaced);
        /** @brief Apply one authoritative explosion-resolution event. */
        void applyExplosionResolvedEvent(const net::MsgExplosionResolved& explosion);
        /** @brief Remove one destroyed brick presentation. */
        void destroyBrickPresentation(uint8_t col, uint8_t row);
        /** @brief Remove one bomb presentation by cell. */
        void removeBombPresentation(uint8_t col, uint8_t row);
        [[nodiscard]]
        /** @brief Return whether a local bomb spark may be spawned. */
        bool canSpawnLocalBombSparkPresentation() const;
        /** @brief Spawn immediate local bomb placement feedback. */
        void spawnLocalBombSparkPresentation();
        /** @brief Spawn temporary explosion visuals for one authoritative cell. */
        void spawnExplosionPresentation(const net::MsgCell& cell);
        /** @brief Age out pending local bomb reservations. */
        void updatePendingLocalBombPlacements(unsigned int delta);
        /** @brief Age out local bomb spark effects. */
        void updateLocalBombSparkPresentations(unsigned int delta);
        /** @brief Age out explosion effects. */
        void updateExplosionPresentations(unsigned int delta);
        /** @brief Remove all local bomb spark effects. */
        void removeAllLocalBombSparkPresentations();
        /** @brief Remove all explosion effects. */
        void removeAllExplosionPresentations();

        /** @brief Store a new interpolation sample for one remote player. */
        static void recordSnapshotSample(RemotePlayerPresentation& presentation,
                                         int16_t xQ,
                                         int16_t yQ,
                                         uint32_t serverTick);
        [[nodiscard]]
        /** @brief Return the authoritative server tick carried by a gameplay event. */
        static uint32_t gameplayEventServerTick(const net::NetClient::GameplayEvent& gameplayEvent);
        /** @brief Update remote-facing animation from snapshot motion. */
        static void updateRemoteAnimationFromSnapshotDelta(RemotePlayerPresentation& presentation);
        /** @brief Update all remote player presentation state for the frame. */
        void updateRemotePlayerPresentations(unsigned int delta);
        [[nodiscard]]
        /** @brief Compute the presented remote position after interpolation. */
        sim::TilePos computeRemotePresentedPosition(const RemotePlayerPresentation& presentation) const;
        /** @brief Keep a remote player tag aligned with its sprite. */
        static void updateRemotePlayerTagPosition(RemotePlayerPresentation& presentation);
        [[nodiscard]]
        /** @brief Format a compact on-screen player tag. */
        static std::string formatPlayerTag(uint8_t playerId);
        /** @brief Leave multiplayer and return to the main menu. */
        void returnToMenu(bool disconnectClient, std::string_view reason);
        /** @brief Leave the match scene and return to the lobby. */
        void returnToLobby(std::string_view reason);
        /** @brief Track tile collision objects created during the scene. */
        void onCollisionObjectSpawned(Tile tile, const std::shared_ptr<Object>& object) override;

        // =============================================================================================================
        // ===== Owned Runtime State ===================================================================================
        // =============================================================================================================

        uint32_t lastAppliedSnapshotTick_ = 0;
        uint32_t lastAppliedCorrectionTick_ = 0;
        std::optional<uint8_t> localPlayerId_{};
        uint32_t matchId_ = 0;
        uint32_t matchBootstrapStartedMs_ = 0;
        bool matchStarted_ = true;
        bool gameplayUnlocked_ = true; ///< Central gate for local gameplay input and prediction.
        bool matchLoadedAckSent_ = true;
        uint32_t goBannerHideTick_ = 0;
        std::optional<net::MsgMatchStart> currentMatchStart_{};
        std::optional<net::MsgMatchResult> currentMatchResult_{};
        std::shared_ptr<Text> localPlayerTag_ = nullptr;
        std::shared_ptr<Text> gameplayStatusText_ = nullptr;
        std::shared_ptr<Text> centerBannerText_ = nullptr;
        std::shared_ptr<Text> centerBannerDetailText_ = nullptr;
        std::shared_ptr<Text> debugHudNetText_ = nullptr;
        std::shared_ptr<Text> debugHudPredictionText_ = nullptr;
        std::shared_ptr<Text> debugHudSimulationText_ = nullptr;
        net::ClientPrediction localPrediction_{}; ///< Prediction-owned local state.
        LivePredictionTelemetry livePredictionTelemetry_{};
        MovementDirection localFacingDirection_ = MovementDirection::Right;
        uint32_t livePredictionLogAccumulatorMs_ = 0;
        uint32_t debugHudRefreshAccumulatorMs_ = 0;
        uint32_t lastAppliedGameplayEventTick_ = 0; ///< Stale-event guard for reliable gameplay events.
        bool localPlayerAlive_ = true;
        bool localPlayerInputLocked_ = false;
        bool localBombHeldOnLastQueuedInput_ = false; ///< Used to detect bomb-button edge transitions.
        uint8_t localPlayerEffectFlags_ = 0;
        uint32_t powerupBlinkAccumulatorMs_ = 0;
        std::shared_ptr<Sound> explosionSound_ = nullptr;

        std::unordered_map<uint8_t, RemotePlayerPresentation> remotePlayerPresentations_;
        std::unordered_map<uint16_t, BombPresentation> bombPresentations_;
        std::unordered_map<uint16_t, PowerupPresentation> powerupPresentations_;
        std::unordered_map<uint16_t, std::shared_ptr<Object>> brickPresentations_;
        std::vector<BombSparkPresentation> bombSparkPresentations_;
        std::vector<PendingLocalBombPlacement> pendingLocalBombPlacements_; ///< Local bomb reservations awaiting authority.
        std::vector<ExplosionPresentation> explosionPresentations_;
        std::deque<net::NetClient::GameplayEvent> pendingGameplayEvents_;
        bool gameplayConnectionDegraded_ = false;
        bool exited_ = false;
        bool returningToMenu_ = false; ///< Prevents duplicate exit/return handling.
    };
} // namespace bomberman

#endif // BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H
