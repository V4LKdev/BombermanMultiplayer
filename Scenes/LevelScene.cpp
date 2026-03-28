#include <cmath>
#include <chrono>
#include <optional>

#include "Entities/Sprite.h"
#include "Game.h"
#include "Scenes/LevelScene.h"
#include "Sim/TileMapGen.h"

namespace bomberman
{
    LevelScene::LevelScene(Game* _game, const unsigned int _stage, const unsigned int /*prevScore*/,
                           std::optional<uint32_t> /*mapSeed*/)
        : Scene(_game), tiles{}, stage(_stage)
    {

        fieldPositionX = 0;
        fieldPositionY = game->getWindowHeight() / 15;
        const float scale =
            (game->getWindowHeight() - fieldPositionY) / static_cast<float>(tileArrayHeight * tileSize);
        scaledTileSize = static_cast<int>(std::round(scale * tileSize));

        menuMusic = std::make_shared<Music>(game->getAssetManager()->getMusic(MusicEnum::Level));
    }

    void LevelScene::onEnter()
    {
        Scene::onEnter();

        if (menuMusic != nullptr)
        {
            menuMusic->play();
        }
    }

    void LevelScene::onExit()
    {
        if (menuMusic != nullptr)
        {
            menuMusic->stop();
        }

        Scene::onExit();
    }

    void LevelScene::initializeLevelWorld(std::optional<uint32_t> mapSeed)
    {
        generateTileMap(mapSeed);

        spawnPlayer(fieldPositionX + playerStartX * scaledTileSize,
                    fieldPositionY + playerStartY * scaledTileSize);
        setLocalPlayerPositionQ({ playerStartX * 256 + 128, playerStartY * 256 + 128 });
    }

    void LevelScene::generateTileMap(std::optional<uint32_t> mapSeed)
    {
        const uint32_t seed = mapSeed.has_value()
            ? mapSeed.value()
            : static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());

        sim::generateTileMap(seed, tiles);

        for(int i = 0; i < static_cast<int>(tileArrayHeight); i++)
        {
            for(int j = 0; j < static_cast<int>(tileArrayWidth); j++)
            {
                if(tiles[i][j] == Tile::Brick)
                {
                    spawnGrass(fieldPositionX + j * scaledTileSize, fieldPositionY + i * scaledTileSize);
                    spawnBrick(fieldPositionX + j * scaledTileSize, fieldPositionY + i * scaledTileSize);
                }
                if(tiles[i][j] == Tile::Grass || tiles[i][j] == Tile::EmptyGrass)
                {
                    spawnGrass(fieldPositionX + j * scaledTileSize, fieldPositionY + i * scaledTileSize);
                }
                if(tiles[i][j] == Tile::Stone)
                {
                    spawnStone(fieldPositionX + j * scaledTileSize, fieldPositionY + i * scaledTileSize);
                }
            }
        }
    }

    void LevelScene::spawnGrass(const int positionX, const int positionY)
    {
        auto grass = std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Grass),
                                              game->getRenderer());
        grass->setPosition(positionX, positionY);
        grass->setSize(scaledTileSize, scaledTileSize);
        addObject(grass);
        backgroundObjectLastNumber++;
    }

    void LevelScene::spawnBrick(const int positionX, const int positionY)
    {
        auto brick = std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Brick),
                                              game->getRenderer());
        brick->setPosition(positionX, positionY);
        brick->setSize(scaledTileSize, scaledTileSize);
        addObject(brick);
        onCollisionObjectSpawned(Tile::Brick, brick);
    }

    void LevelScene::spawnStone(const int positionX, const int positionY)
    {
        auto stone = std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::Stone),
                                              game->getRenderer());
        stone->setPosition(positionX, positionY);
        stone->setSize(scaledTileSize, scaledTileSize);
        addObject(stone);
        onCollisionObjectSpawned(Tile::Stone, stone);
        backgroundObjectLastNumber++;
    }

    void LevelScene::onCollisionObjectSpawned(const Tile /*tile*/, const std::shared_ptr<Object>& /*object*/) {}

    void LevelScene::spawnPlayer(const int positionX, const int positionY)
    {
        player = std::make_shared<Player>(game->getAssetManager()->getTexture(Texture::Player),
                                          game->getRenderer());
        player->setPosition(positionX, positionY);
        player->setSize(scaledTileSize, scaledTileSize);
        player->setClip(tileSize, tileSize, tileSize * 4, 0);
        addObject(player);
    }

    void LevelScene::onEvent(const SDL_Event& event)
    {
        Scene::onEvent(event);

        if(usesEventDrivenLocalMovement() &&
           (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
           event.key.repeat == 0)
        {
            updateMovement(event.type == SDL_KEYDOWN, event.key.keysym.scancode);
        }

        if(event.type != SDL_KEYDOWN || event.key.repeat != 0)
            return;

        if(event.key.keysym.scancode == SDL_SCANCODE_RETURN)
        {
            if(!supportsPause())
                return;

            isPaused = !isPaused;
            if(isPaused)
                menuMusic->pause();
            else
                menuMusic->resume();
            return;
        }

        onKeyPressed(event.key.keysym.scancode);
    }

    void LevelScene::update(const unsigned int delta)
    {
        if(isPaused)
            return;

        updateLevel(delta);
    }

    void LevelScene::onKeyPressed(const SDL_Scancode /*scancode*/) {}

    void LevelScene::updateMovement(const bool isPressed, const int keycode)
    {
        if(player == nullptr)
        {
            return;
        }

        if(isPressed)
        {
            switch(keycode)
            {
                case SDL_SCANCODE_W:
                case SDL_SCANCODE_UP:
                    playerDirectionY -= 1;
                    break;
                case SDL_SCANCODE_S:
                case SDL_SCANCODE_DOWN:
                    playerDirectionY += 1;
                    break;
                case SDL_SCANCODE_A:
                case SDL_SCANCODE_LEFT:
                    playerDirectionX -= 1;
                    break;
                case SDL_SCANCODE_D:
                case SDL_SCANCODE_RIGHT:
                    playerDirectionX += 1;
                    break;
                default:
                    break;
            }
        }
        else
        {
            switch(keycode)
            {
                case SDL_SCANCODE_W:
                case SDL_SCANCODE_UP:
                    playerDirectionY += 1;
                    break;
                case SDL_SCANCODE_S:
                case SDL_SCANCODE_DOWN:
                    playerDirectionY -= 1;
                    break;
                case SDL_SCANCODE_A:
                case SDL_SCANCODE_LEFT:
                    playerDirectionX += 1;
                    break;
                case SDL_SCANCODE_D:
                case SDL_SCANCODE_RIGHT:
                    playerDirectionX -= 1;
                    break;
                default:
                    break;
            }
        }

        MovementDirection direction = MovementDirection::None;
        if(playerDirectionX != 0)
        {
            direction = playerDirectionX > 0 ? MovementDirection::Right : MovementDirection::Left;
        }
        else if(playerDirectionY != 0)
        {
            direction = playerDirectionY > 0 ? MovementDirection::Down : MovementDirection::Up;
        }

        player->setMovementDirection(direction);
    }

    void LevelScene::updateCamera()
    {
        if(player == nullptr)
        {
            return;
        }

        const int screenStart = fieldPositionX;
        const int screenFinish = fieldPositionX + scaledTileSize * static_cast<int>(tileArrayWidth);
        const int screenWidthHalf = game->getWindowWidth() / 2;
        int cameraPositionX = player->getPositionX();
        if(cameraPositionX <= screenWidthHalf)
        {
            cameraPositionX = screenStart;
        }
        else if(cameraPositionX + screenWidthHalf >= screenFinish)
        {
            cameraPositionX = screenFinish - game->getWindowWidth();
        }
        else
        {
            cameraPositionX -= screenWidthHalf;
        }
        setCamera(cameraPositionX, 0);
    }


    void LevelScene::clearLocalMovementInput()
    {
        playerDirectionX = 0;
        playerDirectionY = 0;

        if(player != nullptr)
            player->setMovementDirection(MovementDirection::None);
    }

    void LevelScene::stepLocalPlayerMovement()
    {
        if(!player)
            return;

        const auto clampDir = [](int d) -> int8_t {
            return static_cast<int8_t>(d > 0 ? 1 : (d < 0 ? -1 : 0));
        };
        const int8_t moveX = clampDir(playerDirectionX);
        const int8_t moveY = clampDir(playerDirectionY);

        playerPos_ = sim::stepMovementWithCollision(playerPos_, moveX, moveY, tiles);
        syncPlayerSpriteToSimPosition();
    }

    void LevelScene::syncPlayerSpriteToSimPosition()
    {
        if(!player)
            return;

        const int screenX = sim::tileQToScreenTopLeft(playerPos_.xQ, fieldPositionX, scaledTileSize, 0);
        const int screenY = sim::tileQToScreenTopLeft(playerPos_.yQ, fieldPositionY, scaledTileSize, 0);
        player->setPosition(screenX, screenY);
    }

    void LevelScene::setLocalPlayerPositionQ(const sim::TilePos posQ)
    {
        playerPos_ = posQ;
        syncPlayerSpriteToSimPosition();
    }

} // namespace bomberman
