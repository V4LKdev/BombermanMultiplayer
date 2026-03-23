#ifndef _BOMBERMAN_SCENES_SCENE_H_
#define _BOMBERMAN_SCENES_SCENE_H_

#include <SDL.h>
#include <vector>

#include "Entities/Object.h"

namespace bomberman
{
    class Game;
    /**
     * @brief Scene base class
     *
     */
    class Scene
    {
      public:
        /**
         * @brief Construct a new Scene object
         *
         * @param game - pointer to game
         */
        Scene(Game* game);
        /**
         * @brief Destroy the Scene object
         *
         */
        virtual ~Scene();
        /**
         * @brief Add object to scene for drawing
         *
         * @param object
         */
        void addObject(std::shared_ptr<Object> object);
        /**
         * @brief Add object to scene for drawing to specific position
         *
         * @param object
         * @param position - position where to insert object
         */
        void insertObject(std::shared_ptr<Object> object, int position);
        /**
         * @brief Remove object from scene
         *
         * @param object
         */
        void removeObject(std::shared_ptr<Object> object);
        /**
         * @brief Set the Camera object to specific position
         *
         * @param x - position x
         * @param y - position y
         */
        void setCamera(const int x, const int y);
        /**
         * @brief Called when scene become activated
         *
         */
        virtual void onEnter();
        /**
         * @brief CAlled when scene become deactivated
         *
         */
        virtual void onExit();
        /**
         * @brief Trigger on SDL2 event if scene is active
         *
         * @param event - SDL2 event
         */
        virtual void onEvent(const SDL_Event& event);
        /**
         * @brief Trigger on update if scene is active
         *
         * @param delta - time in milliseconds
         */
        virtual void update(const unsigned int delta);
        /**
         * @brief Draw objects on the screen
         *
         */
        void draw() const;

        /**
         * @brief Returns the current camera position.
         */
        [[nodiscard]]
        SDL_Rect getCamera() const { return camera; }

        /**
         * @brief Returns true when this scene wants Game to poll and send multiplayer input every sim tick.
         */
        [[nodiscard]]
        virtual bool wantsNetworkInputPolling() const { return false; }

        /**
         * @brief Called after Game samples and queues a local input for multiplayer scenes.
         *
         * `inputSeq` is the local sequence assigned by NetClient. Scenes that do not care
         * about local prediction can ignore this hook.
         */
        virtual void onNetInputQueued(uint32_t inputSeq, uint8_t buttons);

      protected:
        Game* game = nullptr; // pointer to game for use in all scenes

      private:
        std::vector<std::shared_ptr<Object>> objects; // objects to update and draw
        SDL_Rect camera;                              // camera position
    };
} // namespace bomberman

#endif // _BOMBERMAN_SCENES_SCENE_H_
