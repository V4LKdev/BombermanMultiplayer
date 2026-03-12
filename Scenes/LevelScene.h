#ifndef _BOMBERMAN_LEVEL_SCENE_H_
#define _BOMBERMAN_LEVEL_SCENE_H_

#include <SDL.h>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Const.h"
#include "Entities/Enemy.h"
#include "Entities/Music.h"
#include "Entities/Player.h"
#include "Entities/Sound.h"
#include "Entities/Text.h"
#include "Net/NetCommon.h"
#include "Scenes/Scene.h"
#include "Sim/Movement.h"

namespace bomberman
{
    /**
     * @brief Level Scene
     *
     */
    class LevelScene : public Scene
    {
      public:
        /** @brief Parameters needed to convert tile-Q8 world coords to screen pixels. */
        struct FieldTransform
        {
            int fieldX      = 0; ///< Screen X of tile column 0.
            int fieldY      = 0; ///< Screen Y of tile row 0.
            int scaledTile  = 0; ///< Rendered tile size in pixels.
        };

        /**
         * @brief Construct a new Level Scene
         *
         * @param game      - game pointer
         * @param stage     - stage number
         * @param prevScore - score carried over from previous stage
         * @param mapSeed   - optional authoritative map seed from server.
         */
        LevelScene(Game* game, const unsigned int stage, const unsigned int prevScore,
                   std::optional<uint32_t> mapSeed = std::nullopt);

        /** @brief Returns the field transform needed to map tile-Q8 world coords to screen pixels. */
        [[nodiscard]]
        FieldTransform getFieldTransform() const
        {
            return { fieldPositionX, fieldPositionY, scaledTileSize };
        }

        /**
         * @brief Catch SDL2 events
         *
         * @param event - SDL2 event
         */
        virtual void onEvent(const SDL_Event& event) override;
        /**
         * @brief Update level scene
         *
         * @param delta - delta time since previous update in milliseconds
         */
        virtual void update(const unsigned int delta) override;

      private:
        // spawn and generation of map objects
        void spawnTextObjects();
        void generateTileMap(std::optional<uint32_t> mapSeed);
        void generateEnemies();
        void spawnGrass(const int positionX, const int positionY);
        void spawnBrick(const int positionX, const int positionY);
        void spawnStone(const int positionX, const int positionY);
        void spawnPlayer(const int positionX, const int positionY);
        void spawnEnemy(Texture texture, AIType type, const int positionX, const int positionY);
        void spawnBomb(Object* object);
        void spawnBang(Object* object);
        void spawnDoor(Object* object);

        /**
         * @brief Finish level
         *
         */
        void finish() const;
        /**
         * @brief Game over
         *
         */
        void gameOver();
        /**
         * @brief Exit level
         *
         */
        void exit() const;

        // timers update
        void updateTimers(const unsigned int delta);
        void updateLevelTimer();
        void updateBombTimer(const unsigned int delta);
        void updateBangTimer(const unsigned int delta);
        void updateGameOverTimer(const unsigned int delta);
        // update movement
        void updateMovement(const bool isPressed, const int keycode);
        // update camera
        void updateCamera();
        // score update
        void updateScore();
        // update collisions
        void updatePlayerCollision();
        void updateEnemiesCollision();
        void updateBangsCollision();
        bool isCollisionDetected(const SDL_FRect& rect1, const SDL_FRect& rect2) const;
        // destroy brick
        void destroyBrick(std::shared_ptr<Object> brick);
        // enemy follow to player if in attack radius
        void followToPlayer(Enemy* enemy);

        /**
         * @brief Returns true when the game is running in networked (multiplayer) mode.
         */
        [[nodiscard]]
        bool isNetworked() const;

        /**
         * @brief Steps the local player position by one simulation tick (singleplayer only).
         */
        void stepLocalPlayerMovement();

        /**
         * @brief Applies the server-authoritative player position from a snapshot.
         *
         * @param snapshot Most recent server snapshot.
         */
        void applySnapshot(const net::MsgSnapshot& snapshot);

        // timers in ms const
        const int levelTimerStart = 200500;
        const int levelTimerUpdateText = 1000;
        const int bombTimerStart = 1500;
        const int bangTimerStart = 800;
        const int gameOverTimerStart = 1000;
        const int winTimerStart = 200;
        // const for score
        const unsigned int scoreRewardForKill = 200;
        const unsigned int scoreRewardForStage = 1000;

        std::shared_ptr<Music> menuMusic = nullptr;                       // menu music
        std::shared_ptr<Sound> gameoverSound = nullptr;                   // game over sound
        std::shared_ptr<Sound> winSound = nullptr;                        // win sound
        std::shared_ptr<Sound> explosionSound = nullptr;                  // explosion sound
        std::shared_ptr<Text> timerNumber = nullptr;                      // timer
        std::shared_ptr<Text> scoreNumber = nullptr;                      // score
        std::shared_ptr<Player> player = nullptr;                         // player
        std::shared_ptr<Sprite> bomb = nullptr;                           // player's bomb
        std::shared_ptr<Sprite> door = nullptr;                           // door for level finish
        std::vector<std::shared_ptr<Enemy>> enemies;                      // enemies
        std::vector<std::pair<Tile, std::shared_ptr<Object>>> collisions; // collisions
        std::vector<std::shared_ptr<Object>> bangs;                       // bomb's bang
        Tile tiles[tileArrayHeight][tileArrayWidth];                      // tilemap

        int playerDirectionX = 0; // direction used for control
        int playerDirectionY = 0; // direction used for control
        // timer variables
        int levelTimer = levelTimerStart;
        int levelTimerDelta = 0;
        int bombTimer = 0;
        int bangTimer = 0;
        int gameOverTimer = 0;
        // bool flags
        bool isGameOver = false;
        bool isPaused = false;
        bool isWin = false;
        // variables
        unsigned int score = 0;
        unsigned int stage = 0;

        // level positioning
        int fieldPositionX = 0;
        int fieldPositionY = 0;
        // size of tiles
        int scaledTileSize = 0;
        // last object that used as background (grass)
        int backgroundObjectLastNumber = 0;

        /// Canonical player position in tile-Q8, owned by LevelScene.
        sim::TilePos playerPos_{};

        /// Last server snapshot tick that was applied to the player sprite.
        /// Guards against re-applying the same snapshot on consecutive scene ticks.
        uint32_t lastAppliedSnapshotTick_ = 0;
    };
} // namespace bomberman

#endif // _BOMBERMAN_LEVEL_SCENE_H_
