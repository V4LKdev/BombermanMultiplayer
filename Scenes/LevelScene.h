#ifndef _BOMBERMAN_LEVEL_SCENE_H_
#define _BOMBERMAN_LEVEL_SCENE_H_

#include <SDL.h>
#include <memory>
#include <optional>

#include "Const.h"
#include "Entities/Music.h"
#include "Entities/Player.h"
#include "Scenes/Scene.h"
#include "Sim/Movement.h"

namespace bomberman
{
    /**
     * @brief Shared level-scene scaffold used by both singleplayer and multiplayer.
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
         * @brief Construct a shared level scene scaffold.
         *
         * @param game      - game pointer
         * @param stage     - stage number
         * @param prevScore - score carried over from previous stage
         * @param mapSeed   - optional authoritative map seed from server.
         */
        LevelScene(Game* game, const unsigned int stage, const unsigned int prevScore,
                   std::optional<uint32_t> mapSeed = std::nullopt);

        virtual ~LevelScene() override = default;

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

        /** @brief Clears locally held movement input and resets player facing/animation state. */
        void clearLocalMovementInput();

      protected:
        /** @brief Completes shared world setup after derived state is ready. */
        void initializeLevelWorld(std::optional<uint32_t> mapSeed);

        // spawn and generation of shared map objects
        void generateTileMap(std::optional<uint32_t> mapSeed);
        void spawnGrass(const int positionX, const int positionY);
        void spawnBrick(const int positionX, const int positionY);
        void spawnStone(const int positionX, const int positionY);
        void spawnPlayer(const int positionX, const int positionY);

        /** @brief Allows derived scenes to collect collision objects during map spawn. */
        virtual void onCollisionObjectSpawned(Tile tile, const std::shared_ptr<Object>& object);

        // update movement
        void updateMovement(const bool isPressed, const int keycode);
        // update camera
        void updateCamera();

        /**
         * @brief Per-mode update body executed when the scene is not paused.
         */
        virtual void updateLevel(const unsigned int delta) = 0;

        /**
         * @brief Optional per-mode key-down handling.
         */
        virtual void onKeyPressed(SDL_Scancode scancode);

        /** @brief Whether ENTER pause toggle is enabled for this level mode. */
        [[nodiscard]]
        virtual bool supportsPause() const { return true; }

        /**
         * @brief Steps the local player position by one simulation tick.
         */
        void stepLocalPlayerMovement();

        /**
         * @brief Writes `playerPos_` to the player sprite in screen space.
         */
        void syncPlayerSpriteToSimPosition();

        std::shared_ptr<Music> menuMusic = nullptr;                       // menu music
        std::shared_ptr<Player> player = nullptr;                         // player
        Tile tiles[tileArrayHeight][tileArrayWidth];                      // tilemap

        int playerDirectionX = 0; // direction used for control
        int playerDirectionY = 0; // direction used for control
        bool isPaused = false;
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

    };
} // namespace bomberman

#endif // _BOMBERMAN_LEVEL_SCENE_H_
