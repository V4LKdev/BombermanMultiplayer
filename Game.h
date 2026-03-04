#ifndef _BOMBERMAN_GAME_H_
#define _BOMBERMAN_GAME_H_

#include <SDL.h>
#include <memory>

#include "Managers/AssetManager.h"
#include "Managers/SceneManager.h"
#include "Net/NetCommon.h"

namespace bomberman
{
    namespace net { class NetClient; }

    class Game
    {
      public:
        /**
         * @brief Constructs game runtime and initializes SDL subsystems.
         *
         * @param windowName Window title.
         * @param windowWidth Initial window width.
         * @param windowHeight Initial window height.
         * @param inNetClient Optional multiplayer client (not owned).
         */
        Game(const std::string& windowName, int windowWidth, int windowHeight,
             net::NetClient* inNetClient = nullptr, uint16_t serverPort = net::kDefaultServerPort);

        /** @brief Releases runtime resources and shuts down SDL subsystems. */
        ~Game();

        /** @brief Runs the main loop until stop() is requested. */
        void run();

        /** @brief Requests the main loop to stop. */
        void stop();

        /**
         * @brief Returns current window width in pixels.
         */
        [[nodiscard]]
        int getWindowWidth() const;

        /**
         * @brief Returns current window height in pixels.
         */
        [[nodiscard]]
        int getWindowHeight() const;

        /**
         * @brief Returns SDL renderer pointer.
         */
        [[nodiscard]]
        SDL_Renderer* getRenderer() const;

        /**
         * @brief Returns scene manager pointer.
         */
        [[nodiscard]]
        SceneManager* getSceneManager() const;

        /**
         * @brief Returns asset manager pointer.
         */
        [[nodiscard]]
        AssetManager* getAssetManager() const;

        /**
         * @brief Returns pointer to optional network client (may be nullptr if not in multiplayer mode).
         */
        [[nodiscard]]
        net::NetClient* getNetClient() const;

        /**
         * @brief Returns the server port to connect to (from CLI or default).
         */
        [[nodiscard]]
        uint16_t getServerPort() const;

      private:
        // SDL pointers.
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;

        std::unique_ptr<SceneManager> sceneManager = nullptr;
        std::unique_ptr<AssetManager> assetManager = nullptr;

        // Screen parameters.
        int windowWidth = 0;
        int windowHeight = 0;

        bool isRunning = false;
        bool isInitialized = false;
        Uint32 lastTickTime = 0;
        Uint32 accumulatorMs = 0;

        net::NetClient* netClient_ = nullptr; ///< Optional network client for multiplayer (not owned)
        uint16_t serverPort_ = net::kDefaultServerPort; ///< Server port from CLI or default.
        bool previousBombHeld_ = false;        ///< Previous bomb key state for edge detection.
        uint16_t bombCommandId_ = 0;           ///< Monotonically increasing bomb command id.

        /** @brief Samples keyboard state, builds a MsgInput, and sends it to the server. */
        void pollNetInput(uint32_t clientTick);
    };
} // namespace bomberman

#endif // _BOMBERMAN_GAME_H_
