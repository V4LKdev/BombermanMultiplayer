#ifndef _BOMBERMAN_GAME_H_
#define _BOMBERMAN_GAME_H_

#include <SDL.h>
#include <memory>

#include "Managers/AssetManager.h"
#include "Managers/SceneManager.h"

namespace bomberman
{
    namespace net { class NetClient; }

    class Game
    {
      public:
        /**
         * @brief Create Game
         *
         * @param windowName - name of window
         * @param windowWidth - width of window
         * @param windowHeight - height of window
         * @param inNetClient - optional network client reference for multiplayer support
         */
        Game(const std::string& windowName, const int windowWidth, const int windowHeight, net::NetClient* inNetClient = nullptr);
        /**
         * @brief Destroy Game
         *
         */
        ~Game();
        /**
         * @brief Run game loop
         *
         */
        void run();
        /**
         * @brief Stop game loop
         *
         */
        void stop();
        /**
         * @brief Get the Window Width
         *
         * @return int - window width
         */
        int getWindowWidth() const;
        /**
         * @brief Get the Window Height
         *
         * @return int - window height
         */
        int getWindowHeight() const;
        /**
         * @brief Get SDL2 Renderer
         *
         * @return SDL_Renderer* - SDL2 renderer
         */
        SDL_Renderer* getRenderer() const;
        /**
         * @brief Get Scene Manager reference
         *
         * @return SceneManager* - scene manager reference
         */
        SceneManager* getSceneManager() const;
        /**
         * @brief Get Asset Manager reference
         *
         * @return AssetManager* - asset manager reference
         */
        AssetManager* getAssetManager() const;

      private:
        // SDL2 C pointers
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;

        std::unique_ptr<SceneManager> sceneManager = nullptr; // scene manager
        std::unique_ptr<AssetManager> assetManager = nullptr; // asset manager

        // screen parameters
        int windowWidth = 0;
        int windowHeight = 0;

        bool isRunning = false;      // game loop status
        bool isInitialized = false;  // SDL init status
        Uint32 lastTickTime = 0;  // last time for delta calculation
        Uint32 accumulatorMs = 0; // accumulator for fixed timestep

        net::NetClient* netClient = nullptr;
    };
} // namespace bomberman

#endif // _BOMBERMAN_GAME_H_
