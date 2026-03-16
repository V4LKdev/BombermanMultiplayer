#ifndef _BOMBERMAN_GAME_H_
#define _BOMBERMAN_GAME_H_

#include <SDL.h>
#include <chrono>
#include <memory>

#include "Managers/AssetManager.h"
#include "Managers/SceneManager.h"
#include "Net/NetCommon.h"

namespace bomberman
{
    namespace net { class NetClient; }

    struct MultiplayerClientConfig
    {
        bool predictionEnabled = true;
        bool remoteSmoothingEnabled = true;
    };

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
             net::NetClient* inNetClient = nullptr, uint16_t serverPort = net::kDefaultServerPort, bool mute = false,
             MultiplayerClientConfig multiplayerConfig = {});

        /** @brief Releases runtime resources and shuts down SDL subsystems. */
        ~Game();

        /** @brief Runs the main loop until stop() is requested. */
        void run();

        /** @brief Requests the main loop to stop. */
        void stop();

        /** @brief Returns current window width in pixels. */
        [[nodiscard]]
        int getWindowWidth() const;

        /** @brief Returns current window height in pixels. */
        [[nodiscard]]
        int getWindowHeight() const;

        /** @brief Returns the SDL renderer. */
        [[nodiscard]]
        SDL_Renderer* getRenderer() const;

        /** @brief Returns the scene manager. */
        [[nodiscard]]
        SceneManager* getSceneManager() const;

        /** @brief Returns the asset manager. */
        [[nodiscard]]
        AssetManager* getAssetManager() const;

        /** @brief Returns the network client, or nullptr if not in multiplayer mode. */
        [[nodiscard]]
        net::NetClient* getNetClient() const;

        /**
         * @brief Fills `out` with the most recent server snapshot and returns true.
         *
         * Returns false if not in multiplayer mode, or not yet connected.
         */
        [[nodiscard]]
        bool tryGetLatestSnapshot(net::MsgSnapshot& out) const;

        /** @brief Returns the server port (from CLI or default). */
        [[nodiscard]]
        uint16_t getServerPort() const;

        /** @brief Returns the multiplayer client configuration used for this run. */
        [[nodiscard]]
        const MultiplayerClientConfig& getMultiplayerClientConfig() const;

        /** @brief Returns true when local multiplayer prediction is enabled. */
        [[nodiscard]]
        bool isPredictionEnabled() const;

        /** @brief Returns true when remote smoothing/interpolation is enabled. */
        [[nodiscard]]
        bool isRemoteSmoothingEnabled() const;

        /** @brief Disconnects the multiplayer client if one is active. */
        void disconnectNetClientIfActive(bool blockUntilComplete = true);

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
        std::chrono::steady_clock::time_point lastTickTime{};
        std::chrono::duration<double> accumulator{};

        net::NetClient* netClient_ = nullptr;           ///< Optional network client for multiplayer (not owned)
        uint16_t serverPort_ = net::kDefaultServerPort; ///< Server port from CLI or default.
        bool mute_ = false;                             ///< When true, all audio output is silenced at startup.
        bool hasKeyboardFocus_ = true;                  ///< True while the SDL window has keyboard focus.
        MultiplayerClientConfig multiplayerConfig_{};   ///< Startup config for client-side netcode behavior.


        /** @brief Updates internal focus state and clears local input when focus is lost. */
        void handleWindowFocusChanged(bool hasFocus);

        /** @brief Samples keyboard state, builds a button bitmask, and sends it to the server. */
        void pollNetInput();
    };
} // namespace bomberman

#endif // _BOMBERMAN_GAME_H_
