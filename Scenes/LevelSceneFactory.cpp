#include "Scenes/LevelSceneFactory.h"

#include "Scenes/MultiplayerLevelScene.h"
#include "Scenes/SingleplayerLevelScene.h"

namespace bomberman
{
    std::shared_ptr<LevelScene> createLevelScene(Game* game, const unsigned int stage,
                                                 const unsigned int prevScore, const LevelMode mode,
                                                 std::optional<uint32_t> mapSeed)
    {
        switch(mode)
        {
            case LevelMode::Multiplayer:
                return std::make_shared<MultiplayerLevelScene>(game, stage, prevScore, mapSeed);
            case LevelMode::Singleplayer:
            default:
                return std::make_shared<SingleplayerLevelScene>(game, stage, prevScore, mapSeed);
        }
    }
} // namespace bomberman

