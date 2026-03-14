#include "Scenes/MultiplayerLevelScene.h"

#include "Game.h"
#include "Net/NetClient.h"

namespace bomberman
{
    MultiplayerLevelScene::MultiplayerLevelScene(Game* game, const unsigned int stage,
                                                 const unsigned int prevScore,
                                                 std::optional<uint32_t> mapSeed)
        : LevelScene(game, stage, prevScore, mapSeed)
    {
        initializeLevelWorld(mapSeed);
    }

    void MultiplayerLevelScene::updateLevel(const unsigned int delta)
    {
        net::MsgSnapshot snapshot{};
        if(game->tryGetLatestSnapshot(snapshot))
        {
            applySnapshot(snapshot);
        }
        Scene::update(delta);
        updateCamera();
    }

    void MultiplayerLevelScene::applySnapshot(const net::MsgSnapshot& snapshot)
    {
        if(!player)
            return;

        if(snapshot.serverTick <= lastAppliedSnapshotTick_)
            return;

        const net::NetClient* netClient = game->getNetClient();
        if(!netClient)
            return;

        const uint8_t localId = netClient->playerId();
        for(uint8_t i = 0; i < snapshot.playerCount; ++i)
        {
            if(snapshot.players[i].playerId != localId)
                continue;

            playerPos_.xQ = snapshot.players[i].xQ;
            playerPos_.yQ = snapshot.players[i].yQ;
            syncPlayerSpriteToSimPosition();

            lastAppliedSnapshotTick_ = snapshot.serverTick;
            break;
        }
    }
} // namespace bomberman

