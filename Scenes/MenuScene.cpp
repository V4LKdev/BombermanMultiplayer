#include <string>

#include "Entities/Sprite.h"
#include "Game.h"
#include "Scenes/ConnectScene.h"
#include "Scenes/GameOverScene.h"
#include "Scenes/MenuScene.h"
#include "Scenes/StageScene.h"

namespace bomberman
{
    // ++increment for menu id
    MenuItem& operator++(MenuItem& c)
    {
        using type = typename std::underlying_type<MenuItem>::type;
        c = static_cast<MenuItem>(static_cast<type>(c) + 1);
        if(c == MenuItem::Last)
            c = static_cast<MenuItem>(static_cast<type>(MenuItem::None) + 1);
        return c;
    }

    // --decrement for menu id
    MenuItem& operator--(MenuItem& c)
    {
        using type = typename std::underlying_type<MenuItem>::type;
        c = static_cast<MenuItem>(static_cast<type>(c) - 1);
        if(c == MenuItem::None)
            c = static_cast<MenuItem>(static_cast<type>(MenuItem::Last) - 1);
        return c;
    }

    // increment++ for menu id
    MenuItem operator++(MenuItem& c, int)
    {
        MenuItem result = c;
        ++c;
        return result;
    }

    // decrement-- for menu id
    MenuItem operator--(MenuItem& c, int)
    {
        MenuItem result = c;
        --c;
        return result;
    }

    MenuScene::MenuScene(Game* _game) : Scene(_game)
    {
        // Fixed layout tuned for 800x600 menu composition.
        const int bgW = 500;
        const int bgH = 306;
        const int bgX = (game->getWindowWidth() - bgW) / 2;
        const int bgY = 20;
        auto background = std::make_shared<Sprite>(game->getAssetManager()->getTexture(Texture::MenuBack), game->getRenderer());
        background->setPosition(bgX, bgY);
        background->setSize(bgW, bgH);
        addObject(background);

        // Buttons: per-label widths, all centered on the same axis.
        const int startW = 190;
        const int onlineW = 220;
        const int exitW = 160;
        const int itemH = 40;
        const int center = game->getWindowWidth() / 2;
        const int startX = center - (startW / 2);
        const int onlineX = center - (onlineW / 2);
        const int exitX = center - (exitW / 2);
        const int startTop = 342;
        const int onlineTop = 424;
        const int exitTop = 506;

        // START
        startText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "START");
        startText->setColor(colorSelected);
        startText->setSize(startW, itemH);
        startText->setPosition(startX, startTop);
        addObject(startText);

        // ONLINE
        onlineText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "ONLINE");
        onlineText->setColor(colorStandard);
        onlineText->setSize(onlineW, itemH);
        onlineText->setPosition(onlineX, onlineTop);
        addObject(onlineText);

        // EXIT
        exitText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "EXIT");
        exitText->setColor(colorStandard);
        exitText->setSize(exitW, itemH);
        exitText->setPosition(exitX, exitTop);
        addObject(exitText);

        game->getSceneManager()->addScene("gameover", std::make_shared<GameOverScene>(game));
        // menu music
        menuMusic = std::make_shared<Music>(game->getAssetManager()->getMusic(MusicEnum::MainMenu));
    }

    void MenuScene::onEnter()
    {
        menuMusic->play();
    }

    void MenuScene::onExit()
    {
        menuMusic->stop();
    }

    void MenuScene::onEvent(const SDL_Event& event)
    {
        Scene::onEvent(event);
        if(event.type == SDL_KEYDOWN)
        {
            switch(event.key.keysym.scancode)
            {
                // we should select next menu item
                case SDL_SCANCODE_S:
                case SDL_SCANCODE_DOWN:
                    currentSelectedMenu++;
                    onMenuItemSelect();
                    break;
                // we should select prev menu item
                case SDL_SCANCODE_W:
                case SDL_SCANCODE_UP:
                    currentSelectedMenu--;
                    onMenuItemSelect();
                    break;
                // enter in menu item
                case SDL_SCANCODE_SPACE:
                case SDL_SCANCODE_RETURN:
                    onMenuItemPress();
                    break;
                default:
                    break;
            }
        }
    }

    void MenuScene::onMenuItemSelect()
    {
        // reset menu items color
        startText->setColor(colorStandard);
        exitText->setColor(colorStandard);
        onlineText->setColor(colorStandard);
        // change color of selected menu item
        switch(currentSelectedMenu)
        {
            case MenuItem::Start:
                startText->setColor(colorSelected);
                break;
            case MenuItem::Exit:
                exitText->setColor(colorSelected);
                break;
            case MenuItem::Online:
                onlineText->setColor(colorSelected);
                break;
            default:
                break;
        }
    }

    void MenuScene::onMenuItemPress()
    {
        switch(currentSelectedMenu)
        {
            case MenuItem::Start:
                // go to level scene
                game->getSceneManager()->addScene("stage", std::make_shared<StageScene>(game, 1, 0, LevelMode::Singleplayer));
                game->getSceneManager()->activateScene("stage");
                break;
            case MenuItem::Exit:
                // stop game loop
                game->stop();
                break;
            case MenuItem::Online:
                game->getSceneManager()->addScene("connect", std::make_shared<ConnectScene>(game, game->getServerPort()));
                game->getSceneManager()->activateScene("connect");
                break;
            default:
                break;
        }
    }
} // namespace bomberman
