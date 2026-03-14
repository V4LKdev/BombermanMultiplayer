#include "Scenes/SingleplayerLevelScene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <utility>

#include "Entities/Sprite.h"
#include "Game.h"
#include "Scenes/StageScene.h"
#include "Util/Collision.h"
#include "Util/Pathfinding.h"

namespace bomberman
{
    namespace
    {
        constexpr float kDamageHitboxScale = 0.2f; ///< Bang/damage hitbox shrink
    }

    SingleplayerLevelScene::SingleplayerLevelScene(Game* game, const unsigned int stage,
                                                   const unsigned int prevScore,
                                                   std::optional<uint32_t> mapSeed)
        : LevelScene(game, stage, prevScore, mapSeed)
        , score(prevScore)
    {
        spawnTextObjects();
        initializeLevelWorld(mapSeed);
        setTimerTextFromMilliseconds(kLevelTimerStart);

        gameoverSound = std::make_shared<Sound>(this->game->getAssetManager()->getSound(SoundEnum::Lose));
        winSound = std::make_shared<Sound>(this->game->getAssetManager()->getSound(SoundEnum::Win));
        explosionSound = std::make_shared<Sound>(this->game->getAssetManager()->getSound(SoundEnum::Explosion));

        generateEnemies();
    }

    void SingleplayerLevelScene::spawnTextObjects()
    {
        const int fontWidth = static_cast<int>(game->getWindowWidth() / 32.0f);
        const int fontHeight = static_cast<int>(game->getWindowHeight() / 30.0f);

        timerLabel =
            std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "TIME");
        timerLabel->setSize(fontWidth * 4, fontHeight);
        timerLabel->setPosition(30, 10);
        timerLabel->attachToCamera(false);
        addObject(timerLabel);
        backgroundObjectLastNumber++;

        timerNumber = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "000");
        timerNumber->setSize(fontWidth * 3, fontHeight);
        timerNumber->setPosition(timerLabel->getPositionX() + timerLabel->getWidth() + 30,
                                 timerLabel->getPositionY());
        timerNumber->attachToCamera(false);
        addObject(timerNumber);
        backgroundObjectLastNumber++;

        std::string scoreText = std::to_string(score);
        scoreNumber =
            std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), scoreText);
        scoreNumber->setSize(fontWidth * static_cast<int>(scoreText.size()), fontHeight);
        scoreNumber->setPosition(
            static_cast<int>(game->getWindowWidth() / 2.0f - scoreNumber->getWidth() / 2.0f),
            timerLabel->getPositionY());
        scoreNumber->attachToCamera(false);
        addObject(scoreNumber);
        backgroundObjectLastNumber++;

        std::string stageTextConv = "STAGE " + std::to_string(stage);
        stageLabel =
            std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), stageTextConv);
        stageLabel->setSize(fontWidth * static_cast<int>(stageTextConv.size()), fontHeight);
        stageLabel->setPosition(game->getWindowWidth() - 30 - stageLabel->getWidth(),
                                timerLabel->getPositionY());
        stageLabel->attachToCamera(false);
        addObject(stageLabel);
        backgroundObjectLastNumber++;
    }

    void SingleplayerLevelScene::updateScore()
    {
        std::string scoreText = std::to_string(score);
        scoreNumber->setText(scoreText);
        scoreNumber->setSize(static_cast<int>(timerNumber->getWidth() / 3.0f) *
                                 static_cast<int>(scoreText.size()),
                             scoreNumber->getHeight());
        scoreNumber->setPosition(game->getWindowWidth() / 2 - scoreNumber->getWidth() / 2,
                                 scoreNumber->getPositionY());
    }

    void SingleplayerLevelScene::setTimerTextFromMilliseconds(int milliseconds)
    {
        if(milliseconds < 0)
            return;

        const int timeInSec = static_cast<int>(milliseconds / 1000.0f);
        std::string timeString = std::to_string(timeInSec);
        while(timeString.size() < 3)
        {
            timeString = "0" + timeString;
        }
        timerNumber->setText(timeString);
    }

    void SingleplayerLevelScene::onCollisionObjectSpawned(const Tile tile,
                                                          const std::shared_ptr<Object>& object)
    {
        collisions.push_back(std::make_pair(tile, object));
    }

    void SingleplayerLevelScene::updateLevel(const unsigned int delta)
    {
        stepLocalPlayerMovement();
        Scene::update(delta);
        updatePlayerCollision();
        updateEnemiesCollision();
        updateBangsCollision();
        updateCamera();
        updateTimers(delta);
    }

    void SingleplayerLevelScene::onKeyPressed(const SDL_Scancode scancode)
    {
        if(scancode == SDL_SCANCODE_ESCAPE)
        {
            gameOver();
            isWin = false;
            gameOverTimer = kWinTimerStart;
        }
        else if(scancode == SDL_SCANCODE_SPACE)
        {
            if(!isGameOver)
            {
                spawnBomb(player.get());
            }
        }
        else if(scancode == SDL_SCANCODE_BACKSPACE)
        {
            gameOver();
            isWin = true;
            score += kScoreRewardForStage * 100;
            gameOverTimer = kWinTimerStart;
        }
    }

    void SingleplayerLevelScene::generateEnemies()
    {
        const auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        auto randCount = std::bind(std::uniform_int_distribution<int>(minEnemiesOnLevel, maxEnemiesOnLevel),
                                   std::mt19937(static_cast<unsigned int>(seed)));
        auto randType = std::bind(std::uniform_int_distribution<int>(0, 1),
                                  std::mt19937(static_cast<unsigned int>(seed)));
        auto randTexture = std::bind(std::uniform_int_distribution<int>(0, 2),
                                     std::mt19937(static_cast<unsigned int>(seed)));
        auto randCellX = std::bind(std::uniform_int_distribution<int>(0, tileArrayHeight - 1),
                                   std::mt19937(static_cast<unsigned int>(seed)));
        auto randCellY = std::bind(std::uniform_int_distribution<int>(0, tileArrayWidth - 1),
                                   std::mt19937(static_cast<unsigned int>(seed)));

        for(int i = 0; i < randCount(); i++)
        {
            int cellX = randCellX();
            int cellY = randCellY();
            while(tiles[cellX][cellY] == Tile::Brick || tiles[cellX][cellY] == Tile::Stone ||
                  tiles[cellX][cellY] == Tile::EmptyGrass)
            {
                cellX = randCellX();
                cellY = randCellY();
            }

            const int textureRand = randTexture();
            spawnEnemy(textureRand == 0 ? Texture::Enemy1 :
                                          (textureRand == 1 ? Texture::Enemy2 : Texture::Enemy3),
                       randType() == 0 ? AIType::Wandering : AIType::Chasing,
                       fieldPositionX + cellY * scaledTileSize, fieldPositionY + cellX * scaledTileSize);
        }
    }

    void SingleplayerLevelScene::spawnEnemy(const Texture texture, const AIType type,
                                            const int positionX, const int positionY)
    {
        auto enemy =
            std::make_shared<Enemy>(game->getAssetManager()->getTexture(texture), game->getRenderer());
        enemy->setPosition(positionX, positionY);
        enemy->setSize(scaledTileSize, scaledTileSize);
        enemy->setAIType(type);
        addObject(enemy);
        enemies.push_back(enemy);
    }

    void SingleplayerLevelScene::spawnBomb(Object* object)
    {
        if(bomb || !object)
        {
            return;
        }

        int bombPositionX = object->getPositionX();
        int bombPositionY = object->getPositionY() - fieldPositionY;
        const int bombPositionDiffX = bombPositionX % scaledTileSize;
        const int bombPositionDiffY = bombPositionY % scaledTileSize;
        bombPositionX = (bombPositionDiffX > scaledTileSize / 2) ?
                            bombPositionX + scaledTileSize - bombPositionDiffX :
                            bombPositionX - bombPositionDiffX;
        bombPositionY = (bombPositionDiffY > scaledTileSize / 2) ?
                            bombPositionY + scaledTileSize - bombPositionDiffY :
                            bombPositionY - bombPositionDiffY;
        bombPositionY += fieldPositionY;

        bomb =
            std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Bomb), game->getRenderer());
        bomb->setSize(scaledTileSize, scaledTileSize);
        bomb->setPosition(bombPositionX, bombPositionY);
        insertObject(bomb, backgroundObjectLastNumber);

        auto animation = std::make_shared<Animation>();
        animation->addAnimationEntity(AnimationEntity(0, 0, tileSize, tileSize));
        animation->addAnimationEntity(AnimationEntity(tileSize * 1, 0, tileSize, tileSize));
        animation->addAnimationEntity(AnimationEntity(tileSize * 2, 0, tileSize, tileSize));
        animation->addAnimationEntity(AnimationEntity(tileSize * 3, 0, tileSize, tileSize));
        animation->setSprite(bomb.get());
        bomb->addAnimation(animation);

        const int bombCellX = static_cast<int>(
            std::round((bomb->getPositionX() - fieldPositionX) / static_cast<float>(scaledTileSize)));
        const int bombCellY = static_cast<int>(
            std::round((bomb->getPositionY() - fieldPositionY) / static_cast<float>(scaledTileSize)));
        tiles[bombCellY][bombCellX] = Tile::Bomb;
        bombTimer = kBombTimerStart;
        animation->play();
    }

    void SingleplayerLevelScene::spawnBang(Object* object)
    {
        const int bombCellX = static_cast<int>(
            std::round((bomb->getPositionX() - fieldPositionX) / static_cast<float>(scaledTileSize)));
        const int bombCellY = static_cast<int>(
            std::round((bomb->getPositionY() - fieldPositionY) / static_cast<float>(scaledTileSize)));
        tiles[bombCellY][bombCellX] = Tile::Grass;

        for(unsigned int i = 0; i < bangSpawnCells; i++)
        {
            auto bang = std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Explosion),
                                                 game->getRenderer());
            bang->setSize(scaledTileSize, scaledTileSize);
            bang->setPosition(object->getPositionX() + bangSpawnPositions[i][0] * scaledTileSize,
                              object->getPositionY() + bangSpawnPositions[i][1] * scaledTileSize);
            addObject(bang);
            bangs.push_back(bang);

            const int bangCellX = static_cast<int>(
                std::round((bang->getPositionX() - fieldPositionX) / static_cast<float>(scaledTileSize)));
            const int bangCellY = static_cast<int>(
                std::round((bang->getPositionY() - fieldPositionY) / static_cast<float>(scaledTileSize)));
            tiles[bangCellY][bangCellX] = Tile::Bang;

            auto animation = std::make_shared<Animation>();
            for(unsigned int j = 1; j < 12; j++)
            {
                animation->addAnimationEntity(AnimationEntity(tileSize * j, 0, tileSize, tileSize));
            }
            animation->setSprite(bang.get());
            bang->addAnimation(animation);
            animation->play();
            explosionSound->play();
        }
        bangTimer = kBangTimerStart;
    }

    void SingleplayerLevelScene::spawnDoor(Object* object)
    {
        door =
            std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Door), game->getRenderer());
        door->setSize(scaledTileSize, scaledTileSize);
        door->setPosition(object->getPositionX(), object->getPositionY());
        insertObject(door, backgroundObjectLastNumber);
    }

    void SingleplayerLevelScene::finish() const
    {
        menuMusic->stop();
        if(isWin)
        {
            winSound->play();
            game->getSceneManager()->addScene("stage", std::make_shared<StageScene>(game, stage + 1, score, LevelMode::Singleplayer));
            game->getSceneManager()->activateScene("stage");
        }
        else
        {
            gameoverSound->play();
            game->getSceneManager()->activateScene("gameover");
        }
        game->getSceneManager()->removeScene("level");
    }

    void SingleplayerLevelScene::gameOver()
    {
        menuMusic->stop();
        gameOverTimer = kGameOverTimerStart;
        isGameOver = true;
    }

    void SingleplayerLevelScene::updateTimers(const unsigned int delta)
    {
        levelTimer -= delta;
        levelTimerDelta += delta;
        if(levelTimerDelta >= kLevelTimerUpdateText)
        {
            setTimerTextFromMilliseconds(levelTimer);
            levelTimerDelta = 0;
        }
        if(bomb != nullptr)
        {
            updateBombTimer(delta);
        }
        if(!bangs.empty())
        {
            updateBangTimer(delta);
        }
        if(isGameOver)
        {
            updateGameOverTimer(delta);
        }

        if(levelTimer <= 0 && !isGameOver)
        {
            gameOver();
            isWin = false;
            gameOverTimer = kWinTimerStart;
        }
    }

    void SingleplayerLevelScene::updateBombTimer(const unsigned int delta)
    {
        if(bombTimer > 0)
        {
            bombTimer -= delta;
        }
        else
        {
            spawnBang(bomb.get());
            removeObject(bomb);
            bomb = nullptr;
        }
    }

    void SingleplayerLevelScene::updateBangTimer(const unsigned int delta)
    {
        if(bangTimer > 0)
        {
            bangTimer -= delta;
        }
        else
        {
            for(auto& bang : bangs)
            {
                removeObject(bang);
                const int bangCellX = static_cast<int>(
                    std::round((bang->getPositionX() - fieldPositionX) / static_cast<float>(scaledTileSize)));
                const int bangCellY = static_cast<int>(
                    std::round((bang->getPositionY() - fieldPositionY) / static_cast<float>(scaledTileSize)));
                tiles[bangCellY][bangCellX] = baseTiles[bangCellY][bangCellX];
            }
            bangs.clear();
        }
    }

    void SingleplayerLevelScene::updateGameOverTimer(const unsigned int delta)
    {
        if(gameOverTimer > 0)
        {
            gameOverTimer -= delta;
        }
        else
        {
            finish();
        }
    }

    void SingleplayerLevelScene::updatePlayerCollision()
    {
        if(player == nullptr)
            return;

        if(door != nullptr)
        {
            SDL_FRect playerRect = collision::scaleCentered(player->getRectF(), sim::kPlayerHitboxScale);
            if(isCollisionDetected(playerRect, door->getRectF()))
            {
                if(!isGameOver && enemies.empty())
                {
                    gameOver();
                    isWin = true;
                    score += kScoreRewardForStage;
                    gameOverTimer = kWinTimerStart;
                }
            }
        }
    }

    void SingleplayerLevelScene::updateEnemiesCollision()
    {
        for(const auto& enemy : enemies)
        {
            for(const auto& collisionObject : collisions)
            {
                if(isCollisionDetected(enemy->getRectF(), collisionObject.second->getRectF()))
                {
                    enemy->setMoving(false);
                    enemy->revertLastMove();
                }
            }
            if(bomb && isCollisionDetected(enemy->getRectF(), bomb->getRectF()))
            {
                enemy->setMoving(false);
                enemy->revertLastMove();
            }
            if(player != nullptr)
            {
                SDL_FRect playerRect = collision::scaleCentered(player->getRectF(), kDamageHitboxScale);
                if(isCollisionDetected(playerRect, enemy->getRectF()))
                {
                    removeObject(player);
                    player = nullptr;
                    gameOver();
                }
            }
            if(player != nullptr)
            {
                if(!enemy->isMovingToCell() && enemy->canAttack())
                {
                    if(std::abs(player->getPositionX() + player->getWidth() / 2 - enemy->getPositionX() -
                                enemy->getWidth() / 2) < enemy->getAttackRadius() &&
                       std::abs(player->getPositionY() + player->getHeight() / 2 - enemy->getPositionY() -
                                enemy->getHeight() / 2) < enemy->getAttackRadius())
                    {
                        followToPlayer(enemy.get());
                    }
                }
            }
        }
    }

    void SingleplayerLevelScene::updateBangsCollision()
    {
        for(const auto& bang : bangs)
        {
            auto itCollision = collisions.begin();
            while(itCollision != collisions.end())
            {
                if((*itCollision).first == Tile::Brick)
                {
                    auto brick = (*itCollision).second;
                    if(isCollisionDetected(brick->getRectF(), bang->getRectF()))
                    {
                        destroyBrick(brick);
                        itCollision = collisions.erase(itCollision);
                        continue;
                    }
                }
                ++itCollision;
            }

            auto itEnemies = enemies.begin();
            while(itEnemies != enemies.end())
            {
                SDL_FRect enemyRect =
                    collision::scaleCentered((*itEnemies)->getRectF(), kDamageHitboxScale);
                if(isCollisionDetected(enemyRect, bang->getRectF()))
                {
                    removeObject(*itEnemies);
                    itEnemies = enemies.erase(itEnemies);
                    score += kScoreRewardForKill;
                    updateScore();
                    continue;
                }
                ++itEnemies;
            }

            if(player != nullptr)
            {
                SDL_FRect playerRect = collision::scaleCentered(player->getRectF(), kDamageHitboxScale);
                if(isCollisionDetected(playerRect, bang->getRectF()))
                {
                    removeObject(player);
                    player = nullptr;
                    gameOver();
                }
            }
        }
    }

    bool SingleplayerLevelScene::isCollisionDetected(const SDL_FRect& rect1, const SDL_FRect& rect2) const
    {
        return collision::intersects(rect1, rect2);
    }

    void SingleplayerLevelScene::destroyBrick(std::shared_ptr<Object> brick)
    {
        if(door == nullptr)
        {
            const long bricksCount = std::count_if(collisions.begin(), collisions.end(),
                                                   [](const auto& collision) { return collision.first == Tile::Brick; });
            const auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            auto randDoor = std::bind(std::uniform_int_distribution<int>(0, doorSpawnRandomize),
                                      std::mt19937(static_cast<unsigned int>(seed)));
            if(randDoor() == 0 || bricksCount <= 1)
            {
                spawnDoor(brick.get());
            }
        }

        const int brickCellX = static_cast<int>(
            std::round((brick->getPositionX() - fieldPositionX) / static_cast<float>(scaledTileSize)));
        const int brickCellY = static_cast<int>(
            std::round((brick->getPositionY() - fieldPositionY) / static_cast<float>(scaledTileSize)));
        tiles[brickCellY][brickCellX] = Tile::Grass;
        removeObject(brick);
    }

    void SingleplayerLevelScene::followToPlayer(Enemy* enemy)
    {
        if(enemy->isMoving())
        {
            std::pair<int, int> cell = std::make_pair(0, 0);
            enemy->moveToCell(cell);
            return;
        }

        const int playerCellX = static_cast<int>(
            std::round((player->getPositionX() - fieldPositionX) / static_cast<float>(scaledTileSize)));
        const int playerCellY = static_cast<int>(
            std::round((player->getPositionY() - fieldPositionY) / static_cast<float>(scaledTileSize)));
        const int enemyCellX = static_cast<int>(
            std::round((enemy->getPositionX() - fieldPositionX) / static_cast<float>(scaledTileSize)));
        const int enemyCellY = static_cast<int>(
            std::round((enemy->getPositionY() - fieldPositionY) / static_cast<float>(scaledTileSize)));

        std::pair<unsigned int, unsigned int> src = std::make_pair(enemyCellY, enemyCellX);
        std::pair<unsigned int, unsigned int> dest = std::make_pair(playerCellY, playerCellX);

        std::pair<int, int> cell = findBestCell(tiles, src, dest);
        if(cell.first >= 0 && cell.second >= 0)
        {
            cell.first -= src.first;
            cell.second -= src.second;
            enemy->moveToCell(cell);
        }
        else
        {
            enemy->generateNewPath();
        }
    }

} // namespace bomberman

