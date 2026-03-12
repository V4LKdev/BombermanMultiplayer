#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <chrono>

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

        constexpr SimDuration kSimStep = SimDuration{1.0 / static_cast<double>(sim::kTickRate)};
        constexpr auto kMaxFrameClamp = std::chrono::milliseconds(sim::kMaxFrameClampMs);
        constexpr Uint32 kSceneStepMs = 1000u / static_cast<Uint32>(sim::kTickRate);
    }

    Game::Game(const std::string& windowName, const int width, const int height,
               net::NetClient* inNetClient, const uint16_t serverPort, const bool mute)
        : windowWidth(width), windowHeight(height), netClient_(inNetClient), serverPort_(serverPort), mute_(mute)
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
            // Process SDL events.
            while(SDL_PollEvent(&event))
            {
                sceneManager->onEvent(event);
                if(event.type == SDL_QUIT)
                {
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
                netClient_->pump(0);
            }

            // Process fixed simulation steps.
            int stepCount = 0;
            while(accumulator >= kSimStep && stepCount < sim::kMaxStepsPerFrame)
            {
                sceneManager->update(kSceneStepMs);
                accumulator -= kSimStep;
                ++stepCount;

                // Send input once per simulation tick.
                if (netClient_ != nullptr && netClient_->isConnected())
                {
                    pollNetInput();

                    // Refresh debug overlay snapshot unconditionally.
                    net::MsgSnapshot latestSnapshot{};
                    if (netClient_->tryGetLatestSnapshot(latestSnapshot))
                    {
                        debugSnapshot_ = latestSnapshot;
                        debugSnapshotValid_ = true;
                    }
                }
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
            drawNetDebugOverlay();
            SDL_RenderPresent(renderer);
        }
    }

    void Game::drawNetDebugOverlay()
    {
        if (!debugSnapshotValid_ || !netClient_)
            return;

        // Only draw when a LevelScene is active.
        auto* levelScene = dynamic_cast<LevelScene*>(sceneManager->getCurrentScene());
        if (!levelScene)
            return;

        const LevelScene::FieldTransform ft = levelScene->getFieldTransform();
        const SDL_Rect camera = levelScene->getCamera();

        // Color palette per playerId.
        static constexpr struct { uint8_t r, g, b; } kPlayerColors[] = {
            {0xFF, 0x22, 0x22},  // Red
            {0x22, 0xFF, 0x22},  // Green
            {0x22, 0x88, 0xFF},  // Blue
            {0xFF, 0xFF, 0x22},  // Yellow
        };

        constexpr int kDotSize = 8;

        for (uint8_t i = 0; i < debugSnapshot_.playerCount; ++i)
        {
            const auto& p = debugSnapshot_.players[i];

            // Convert tile-Q8 → screen pixels using the same transform the renderer uses.
            const int screenX = sim::tileQToScreen(p.xQ, ft.fieldX, ft.scaledTile, camera.x);
            const int screenY = sim::tileQToScreen(p.yQ, ft.fieldY, ft.scaledTile, camera.y);

            const int colorIdx = p.playerId % 4;
            SDL_SetRenderDrawColor(renderer,
                                   kPlayerColors[colorIdx].r,
                                   kPlayerColors[colorIdx].g,
                                   kPlayerColors[colorIdx].b, 0xFF);

            const SDL_Rect dot = { screenX - kDotSize / 2, screenY - kDotSize / 2, kDotSize, kDotSize };
            SDL_RenderFillRect(renderer, &dot);
        }
    }

    void Game::pollNetInput()
    {
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

        netClient_->sendInput(buttons);
    }

    void Game::stop()
    {
        isRunning = false;
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

} // namespace bomberman