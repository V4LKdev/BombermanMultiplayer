#include <string>

#include "Game.h"
#include "Net/Client/NetClient.h"
#include "Scenes/LevelScene.h"
#include "Scenes/LevelSceneFactory.h"
#include "Scenes/MenuScene.h"
#include "Scenes/StageScene.h"
#include "Util/Log.h"

namespace bomberman
{
    StageScene::StageScene(Game* _game, const unsigned int level, const unsigned int _score,
                           const LevelMode _mode)
        : Scene(_game), stage(level), score(_score), mode(_mode)
    {
        // stage text
        auto text = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(),
                                           "STAGE " + std::to_string(level));
        text->setSize(static_cast<int>(game->getWindowWidth() / 3.0f),
                      static_cast<int>(game->getWindowHeight() / 20.0f));
        text->setPosition(static_cast<int>(game->getWindowWidth() / 2.0f - text->getWidth() / 2.0f),
                          static_cast<int>(game->getWindowHeight() / 2.0f - text->getHeight() / 2.0f));
        addObject(text);
    }

    void StageScene::update(const unsigned int delta)
    {
        if(mode == LevelMode::Multiplayer)
        {
            net::NetClient* netClient = game->getNetClient();
            if(netClient == nullptr || netClient->connectState() != net::EConnectState::Connected)
            {
                const auto stateName = netClient ? net::connectStateName(netClient->connectState()) : std::string_view("NoClient");
                LOG_NET_CONN_WARN("Multiplayer stage aborted because client is no longer connected (state={})", stateName);
                game->getSceneManager()->activateScene("menu");
                game->getSceneManager()->removeScene("stage");
                return;
            }
        }

        untilNextSceneTimer += delta;
        if(untilNextSceneTimer >= sceneTimer)
        {
            untilNextSceneTimer = 0;

            std::optional<uint32_t> mapSeed = std::nullopt;
            if (mode == LevelMode::Multiplayer)
            {
                net::NetClient* netClient = game->getNetClient();
                if (netClient && netClient->isConnected())
                {
                    uint32_t seed = 0;
                    if (!netClient->tryGetMapSeed(seed))
                    {
                        LOG_NET_CONN_WARN("Multiplayer stage is connected but still missing LevelInfo/map seed - waiting");
                        return;
                    }
                    mapSeed = seed;
                }
            }

            game->getSceneManager()->addScene("level", createLevelScene(game, stage, score, mode, mapSeed));
            game->getSceneManager()->activateScene("level");
            game->getSceneManager()->removeScene("stage");
        }
    }
} // namespace bomberman
