#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#include <iostream>

#include "Game.h"
#include "Scenes/LevelScene.h"
#include "Scenes/MenuScene.h"

namespace bomberman
{
    namespace
    {
        constexpr int kTargetSimHz = 60;
        constexpr Uint32 kSimStepMs = 1000u / static_cast<Uint32>(kTargetSimHz);
        constexpr Uint32 kMaxFrameClampMs = 250u;
        constexpr int kMaxStepsPerFrame = 8;
    }

    Game::Game(const std::string& windowName, const int width, const int height)
        : windowWidth(width), windowHeight(height)
    {
        // let's init SDL2
        if(SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            std::cout << "SDL_Init Error: " << SDL_GetError() << std::endl;
            return;
        }

        // let's init SDL2 TTF
        if(TTF_Init() != 0)
        {
            std::cout << "TTF_Init Error: " << TTF_GetError() << std::endl;
            return;
        }

        // let's init SDL2 Image
        if(!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
        {
            std::cout << "IMG_Init Error: " << IMG_GetError() << std::endl;
            return;
        }

        // let's init SDL2 Mixer
        if(Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) == -1)
        {
            std::cout << "Mix_OpenAudio Error: " << Mix_GetError() << std::endl;
            return;
        }

        // create a window
        window = SDL_CreateWindow(windowName.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  windowWidth, windowHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
        if(!window)
        {
            std::cout << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
            return;
        }

        // create a renderer for window
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if(renderer == nullptr)
        {
            std::cout << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
            return;
        }

        // we need new size due to possible high resolution on mac and ios
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
            std::cout << "Game::run - initialization failed, exiting." << std::endl;
            return;
        }

        isRunning = true;
        lastTickTime = SDL_GetTicks(); // initialize last tick time for frame delta calculation
        accumulatorMs = 0;

        // load assets
        assetManager->load(renderer);
        // create menu scene
        sceneManager->addScene("menu", std::make_shared<MenuScene>(this));
        sceneManager->activateScene("menu");

        SDL_Event event;

        while(isRunning)
        {
            // check SDL2 events
            while(SDL_PollEvent(&event))
            {
                // send event to current scene
                sceneManager->onEvent(event);
                // stop loop on quit
                if(event.type == SDL_QUIT)
                {
                    stop();
                }
            }

            // calculate frame delta with clamping
            Uint32 currentTickTime = SDL_GetTicks();
            Uint32 frameDeltaMs = currentTickTime - lastTickTime;
            lastTickTime = currentTickTime;

            // clamp frame delta
            if(frameDeltaMs > kMaxFrameClampMs)
            {
                frameDeltaMs = kMaxFrameClampMs;
            }

            // accumulate frame time
            accumulatorMs += frameDeltaMs;

            // process simulation steps
            int stepCount = 0;
            while(accumulatorMs >= kSimStepMs && stepCount < kMaxStepsPerFrame)
            {
                sceneManager->update(kSimStepMs);
                accumulatorMs -= kSimStepMs;
                ++stepCount;
            }

            // log if we hit the safety cap
            if(stepCount >= kMaxStepsPerFrame)
            {
                std::cout << "Warning: Exceeded max update steps (" << kMaxStepsPerFrame
                          << "). Accumulator: " << accumulatorMs << "ms" << std::endl;
            }

            // clear the screen
            SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0x00);
            SDL_RenderClear(renderer);
            // draw current scene
            sceneManager->draw();
            // flip the backbuffer
            SDL_RenderPresent(renderer);
        }
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
} // namespace bomberman
