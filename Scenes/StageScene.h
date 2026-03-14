#ifndef _BOMBERMAN_SCENES_STAGE_SCENE_H_
#define _BOMBERMAN_SCENES_STAGE_SCENE_H_

#include "Scenes/LevelSceneFactory.h"
#include "Scenes/Scene.h"

namespace bomberman
{
    /**
     * @brief Stage Scene
     *
     */
    class StageScene : public Scene
    {
      public:
        /**
         * @brief Construct a new Stage Scene
         *
         * @param game
         */
        StageScene(Game* game, const unsigned int level, const unsigned int score, LevelMode mode);
        /**
         * @brief Trigger on update if scene is active
         *
         * @param delta - time in milliseconds
         */
        virtual void update(const unsigned int delta) override;

      private:
        // timer until switch to next menu
        int untilNextSceneTimer = 0;
        // const
        const int sceneTimer = 2000;
        unsigned int stage = 0;
        unsigned int score = 0;
        LevelMode mode = LevelMode::Singleplayer;
    };
} // namespace bomberman

#endif // _BOMBERMAN_SCENES_STAGE_SCENE_H_
