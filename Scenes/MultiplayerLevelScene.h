#ifndef BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H
#define BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H

#include <optional>

#include "Net/NetCommon.h"
#include "Scenes/LevelScene.h"

namespace bomberman
{
    /**
     * @brief Multiplayer presentation scene driven by authoritative server snapshots.
     *
     * Intentionally minimal for now: only the local player sprite is updated from
     * snapshots while the rest of the future replicated gameplay remains stubbed.
     */
    class MultiplayerLevelScene final : public LevelScene
    {
      public:
        MultiplayerLevelScene(Game* game, unsigned int stage, unsigned int prevScore,
                              std::optional<uint32_t> mapSeed = std::nullopt);

      protected:
        virtual void updateLevel(unsigned int delta) override;
        [[nodiscard]]
        bool supportsPause() const { return false; }

      private:
        void applySnapshot(const net::MsgSnapshot& snapshot);

        uint32_t lastAppliedSnapshotTick_ = 0;
    };
} // namespace bomberman

#endif // BOMBERMAN_SCENES_MULTIPLAYER_LEVEL_SCENE_H


