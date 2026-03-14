#ifndef BOMBERMAN_SCENES_SINGLEPLAYER_LEVEL_SCENE_H
#define BOMBERMAN_SCENES_SINGLEPLAYER_LEVEL_SCENE_H

#include <memory>
#include <utility>
#include <vector>

#include "Entities/Enemy.h"
#include "Entities/Sound.h"
#include "Entities/Sprite.h"
#include "Entities/Text.h"
#include "Scenes/LevelScene.h"

namespace bomberman
{
    enum class Texture : int;

    /**
     * @brief Offline/local gameplay implementation of the level scene.
     */
    class SingleplayerLevelScene final : public LevelScene
    {
      public:
        SingleplayerLevelScene(Game* game, unsigned int stage, unsigned int prevScore, std::optional<uint32_t> mapSeed = std::nullopt);

      protected:
        virtual void updateLevel(unsigned int delta) override;
        virtual void onKeyPressed(SDL_Scancode scancode) override;

      private:
        void spawnTextObjects();
        void updateScore();
        void setTimerTextFromMilliseconds(int milliseconds);
        void generateEnemies();
        void spawnEnemy(Texture texture, AIType type, int positionX, int positionY);
        void spawnBomb(Object* object);
        void spawnBang(Object* object);
        void spawnDoor(Object* object);

        void finish() const;
        void gameOver();

        void updateTimers(unsigned int delta);
        void updateBombTimer(unsigned int delta);
        void updateBangTimer(unsigned int delta);
        void updateGameOverTimer(unsigned int delta);

        void updatePlayerCollision();
        void updateEnemiesCollision();
        void updateBangsCollision();
        [[nodiscard]] bool isCollisionDetected(const SDL_FRect& rect1, const SDL_FRect& rect2) const;
        void destroyBrick(std::shared_ptr<Object> brick);
        void followToPlayer(Enemy* enemy);
        virtual void onCollisionObjectSpawned(Tile tile, const std::shared_ptr<Object>& object) override;

        static constexpr int kLevelTimerStart = 200500;
        static constexpr int kLevelTimerUpdateText = 1000;
        static constexpr int kBombTimerStart = 1500;
        static constexpr int kBangTimerStart = 800;
        static constexpr int kGameOverTimerStart = 1000;
        static constexpr int kWinTimerStart = 200;

        static constexpr unsigned int kScoreRewardForKill = 200;
        static constexpr unsigned int kScoreRewardForStage = 1000;

        std::shared_ptr<Sound> gameoverSound = nullptr;
        std::shared_ptr<Sound> winSound = nullptr;
        std::shared_ptr<Sound> explosionSound = nullptr;
        std::shared_ptr<Text> timerLabel = nullptr;
        std::shared_ptr<Text> timerNumber = nullptr;
        std::shared_ptr<Text> scoreNumber = nullptr;
        std::shared_ptr<Text> stageLabel = nullptr;
        std::shared_ptr<Sprite> bomb = nullptr;
        std::shared_ptr<Sprite> door = nullptr;
        std::vector<std::shared_ptr<Enemy>> enemies;
        std::vector<std::pair<Tile, std::shared_ptr<Object>>> collisions;
        std::vector<std::shared_ptr<Object>> bangs;

        unsigned int score = 0;
        int levelTimer = kLevelTimerStart;
        int levelTimerDelta = 0;
        int bombTimer = 0;
        int bangTimer = 0;
        int gameOverTimer = 0;

        bool isGameOver = false;
        bool isWin = false;
    };
} // namespace bomberman

#endif // BOMBERMAN_SCENES_SINGLEPLAYER_LEVEL_SCENE_H



