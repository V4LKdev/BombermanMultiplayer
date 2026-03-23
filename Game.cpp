#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <chrono>
#include <csignal>

#include "Game.h"

#include "Net/NetClient.h"
#include "Scenes/LevelScene.h"
#include "Scenes/MenuScene.h"
#include "Sim/Movement.h"
#include "Util/Log.h"

namespace bomberman
{
    namespace
    {
        using GameClock = std::chrono::steady_clock;
        using SimDuration = std::chrono::duration<double>;

        volatile std::sig_atomic_t gShutdownSignalRequested = 0;

        constexpr SimDuration kSimStep = SimDuration{1.0 / static_cast<double>(sim::kTickRate)};
        constexpr auto kMaxFrameClamp = std::chrono::milliseconds(sim::kMaxFrameClampMs);
        constexpr Uint32 kSceneStepMs = 1000u / static_cast<Uint32>(sim::kTickRate);

        void handleShutdownSignal(int /*signal*/)
        {
            gShutdownSignalRequested = 1;
        }

        void installShutdownSignalHandlers()
        {
            std::signal(SIGINT, handleShutdownSignal);
            std::signal(SIGTERM, handleShutdownSignal);
        }
    }

    Game::Game(const std::string& windowName, const int width, const int height,
               net::NetClient* inNetClient, const uint16_t serverPort, const bool mute,
               const MultiplayerClientConfig multiplayerConfig)
        : windowWidth(width), windowHeight(height),
          netClient_(inNetClient), serverPort_(serverPort), mute_(mute), multiplayerConfig_(multiplayerConfig)
    {
        // Initialize SDL.
        if(SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            LOG_GAME_ERROR("SDL_Init failed: {}", SDL_GetError());
            return;
        }

        // Initialize SDL_ttf.
        if(TTF_Init() != 0)
        {
            LOG_GAME_ERROR("TTF_Init failed: {}", TTF_GetError());
            return;
        }

        // Initialize SDL_image.
        if(!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
        {
            LOG_GAME_ERROR("IMG_Init failed: {}", IMG_GetError());
            return;
        }

        // Initialize SDL_mixer.
        if(Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) == -1)
        {
            LOG_GAME_ERROR("Mix_OpenAudio failed: {}", Mix_GetError());
            return;
        }

        if (mute_)
        {
            Mix_VolumeMusic(0);
            Mix_Volume(-1, 0); // -1 applies to all channels
        }

        // Create window.
        window = SDL_CreateWindow(windowName.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  windowWidth, windowHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
        if(!window)
        {
            LOG_GAME_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
            return;
        }

        // Create renderer.
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if(renderer == nullptr)
        {
            LOG_GAME_ERROR("SDL_CreateRenderer failed: {}", SDL_GetError());
            return;
        }

        // Query renderer output size to account for high DPI.
        int w, h;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        windowWidth = w;
        windowHeight = h;

        assetManager = std::make_unique<AssetManager>();
        sceneManager = std::make_unique<SceneManager>();
        isInitialized = true;
    }

    Game::~Game()
    {
        // Destroy gameplay/asset owners before SDL subsystem shutdown.
        // AssetManager holds fonts/textures/sounds that must be released
        // while SDL_ttf/SDL_image/SDL_mixer are still alive.
        sceneManager.reset();
        assetManager.reset();

        // delete SDL2 C pointers
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        renderer = nullptr;
        window = nullptr;

        // SDL2 finish
        Mix_CloseAudio();
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
    }

    void Game::run()
    {
        if(isRunning)
        {
            return;
        }
        if(!isInitialized)
        {
            LOG_GAME_ERROR("Initialization failed, cannot run game loop");
            return;
        }

        isRunning = true;
        installShutdownSignalHandlers();
        // Initialize frame timing state for fixed-step simulation.
        lastTickTime = GameClock::now();
        accumulator = SimDuration{};

        // Load initial resources and scene.
        assetManager->load(renderer);
        sceneManager->addScene("menu", std::make_shared<MenuScene>(this));
        sceneManager->activateScene("menu");

        SDL_Event event;

        while(isRunning)
        {
            if(gShutdownSignalRequested != 0)
            {
                LOG_NET_CONN_INFO("Shutdown signal received - disconnecting multiplayer client before exit");
                disconnectNetClientIfActive();
                stop();
                continue;
            }

            // Process SDL events.
            while(SDL_PollEvent(&event))
            {
                if (event.type == SDL_WINDOWEVENT)
                {
                    if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                    {
                        handleWindowFocusChanged(true);
                    }
                    else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    {
                        handleWindowFocusChanged(false);
                    }
                }

                sceneManager->onEvent(event);
                if(event.type == SDL_QUIT)
                {
                    LOG_NET_CONN_INFO("SDL quit requested - disconnecting multiplayer client before exit");
                    disconnectNetClientIfActive();
                    stop();
                }
            }

            // Calculate frame delta with clamping.
            const auto currentTickTime = GameClock::now();
            auto frameDelta = currentTickTime - lastTickTime;
            lastTickTime = currentTickTime;

            if(frameDelta > kMaxFrameClamp)
            {
                frameDelta = kMaxFrameClamp;
            }

            // Accumulate frame time.
            accumulator += std::chrono::duration_cast<SimDuration>(frameDelta);

            // Drain incoming network events.
            if (netClient_ != nullptr)
            {
                netClient_->pumpNetwork(0);
            }

            // Process fixed simulation steps.
            int stepCount = 0;
            while(accumulator >= kSimStep && stepCount < sim::kMaxStepsPerFrame)
            {
                // Send input at the start of the sim tick so prediction and transport use the same tick.
                if (netClient_ != nullptr &&
                    netClient_->isConnected() &&
                    sceneManager->getCurrentScene() != nullptr &&
                    sceneManager->getCurrentScene()->wantsNetworkInputPolling())
                {
                    pollNetInput();
                    netClient_->flushOutgoing();
                }

                sceneManager->update(kSceneStepMs);
                accumulator -= kSimStep;
                ++stepCount;
            }

            // Log if the safety cap is hit.
            if(stepCount >= sim::kMaxStepsPerFrame)
            {
                const auto accumulatorMs = std::chrono::duration<double, std::milli>(accumulator).count();
                LOG_GAME_WARN("Exceeded max update steps ({}), accumulator={:.3f}ms", sim::kMaxStepsPerFrame, accumulatorMs);
            }

            // Render current frame.
            SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0x00);
            SDL_RenderClear(renderer);
            sceneManager->draw();
            SDL_RenderPresent(renderer);
        }
    }


    void Game::pollNetInput()
    {
        if (!hasKeyboardFocus_)
        {
            if (const auto inputSeq = netClient_->sendInput(0);
                inputSeq.has_value() && sceneManager->getCurrentScene() != nullptr)
            {
                sceneManager->getCurrentScene()->onNetInputQueued(inputSeq.value(), 0);
            }
            return;
        }

        const Uint8* keys = SDL_GetKeyboardState(nullptr);

        const bool left  = keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A];
        const bool right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
        const bool up    = keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W];
        const bool down  = keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S];
        const bool bomb  = keys[SDL_SCANCODE_SPACE] != 0;

        // Build button bitmask with opposing-direction cancellation.
        uint8_t buttons = 0;
        if (up    && !down)  buttons |= net::kInputUp;
        if (down  && !up)    buttons |= net::kInputDown;
        if (left  && !right) buttons |= net::kInputLeft;
        if (right && !left)  buttons |= net::kInputRight;
        if (bomb)            buttons |= net::kInputBomb;

        if (const auto inputSeq = netClient_->sendInput(buttons);
            inputSeq.has_value() && sceneManager->getCurrentScene() != nullptr)
        {
            sceneManager->getCurrentScene()->onNetInputQueued(inputSeq.value(), buttons);
        }
    }

    void Game::handleWindowFocusChanged(const bool hasFocus)
    {
        hasKeyboardFocus_ = hasFocus;

        if (hasFocus)
            return;

        auto* levelScene = dynamic_cast<LevelScene*>(sceneManager->getCurrentScene());
        if (levelScene != nullptr && levelScene->usesEventDrivenLocalMovement())
            levelScene->clearLocalMovementInput();
    }

    void Game::stop()
    {
        isRunning = false;
    }

    void Game::disconnectNetClientIfActive(const bool blockUntilComplete)
    {
        if(netClient_ == nullptr)
            return;

        const net::EConnectState state = netClient_->connectState();
        const bool wasActive = (state != net::EConnectState::Disconnected);
        if(!wasActive)
            return;

        if (!blockUntilComplete)
        {
            netClient_->disconnectAsync();
            return;
        }

        const bool graceful = netClient_->disconnectBlocking();
        if (graceful)
        {
            LOG_NET_CONN_INFO("Multiplayer client disconnected gracefully during local shutdown/leave flow");
        }
        else
        {
            LOG_NET_CONN_WARN("Multiplayer client tore down locally before graceful disconnect completed");
        }
    }

    int Game::getWindowWidth() const
    {
        return windowWidth;
    }

    int Game::getWindowHeight() const
    {
        return windowHeight;
    }

    SDL_Renderer* Game::getRenderer() const
    {
        return renderer;
    }

    SceneManager* Game::getSceneManager() const
    {
        return sceneManager.get();
    }

    AssetManager* Game::getAssetManager() const
    {
        return assetManager.get();
    }

    net::NetClient* Game::getNetClient() const
    {
        return netClient_;
    }

    bool Game::tryGetLatestSnapshot(net::MsgSnapshot& out) const
    {
        if (!netClient_ || !netClient_->isConnected())
            return false;
        return netClient_->tryGetLatestSnapshot(out);
    }

    uint16_t Game::getServerPort() const
    {
        return serverPort_;
    }

    const MultiplayerClientConfig& Game::getMultiplayerClientConfig() const
    {
        return multiplayerConfig_;
    }

    bool Game::isPredictionEnabled() const
    {
        return multiplayerConfig_.predictionEnabled;
    }

    bool Game::isRemoteSmoothingEnabled() const
    {
        return multiplayerConfig_.remoteSmoothingEnabled;
    }

} // namespace bomberman
