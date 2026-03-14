#ifndef BOMBERMAN_SCENES_LEVEL_SCENE_FACTORY_H
#define BOMBERMAN_SCENES_LEVEL_SCENE_FACTORY_H

#include <cstdint>
#include <memory>
#include <optional>

namespace bomberman
{
    class Game;
    class LevelScene;

    /**
     * @brief High-level level-scene mode selection.
     */
    enum class LevelMode : uint8_t
    {
        Singleplayer,
        Multiplayer
    };

    /**
     * @brief Creates the appropriate concrete level scene for the requested mode.
     */
    [[nodiscard]]
    std::shared_ptr<LevelScene> createLevelScene(Game* game, unsigned int stage, unsigned int prevScore,
                                                 LevelMode mode,
                                                 std::optional<uint32_t> mapSeed = std::nullopt);
} // namespace bomberman

#endif // BOMBERMAN_SCENES_LEVEL_SCENE_FACTORY_H

