/**
 * @file LobbyScene.h
 * @brief Passive multiplayer lobby scene shown after a successful session accept.
 */

#ifndef BOMBERMAN_SCENES_LOBBYSCENE_H
#define BOMBERMAN_SCENES_LOBBYSCENE_H

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "Entities/Text.h"
#include "Net/NetCommon.h"
#include "Scenes/Scene.h"

namespace bomberman
{
    /**
     * @brief Read-only multiplayer lobby scene backed by authoritative server seat state.
     *
     * This scene intentionally does not own any match-start or ready-toggle
     * interactions yet. It renders the current accepted seats and local
     * connection state while the server remains in the passive lobby phase.
     */
    class LobbyScene final : public Scene
    {
      public:
        struct SeatRowWidgets
        {
            std::shared_ptr<Text> seatText = nullptr;
            std::shared_ptr<Text> localTagText = nullptr;
            std::shared_ptr<Text> nameText = nullptr;
            std::shared_ptr<Text> winsText = nullptr;
            std::shared_ptr<Text> stateText = nullptr;
        };

        explicit LobbyScene(Game* game);

        void onEvent(const SDL_Event& event) override;
        void update(unsigned int delta) override;

      private:
        void setStatus(std::string_view message, SDL_Color color);
        void rebuildLobbyPresentation(const net::MsgLobbyState& lobbyState, uint8_t localPlayerId);
        void returnToMenu(bool disconnectClient, std::string_view reason);

        std::shared_ptr<Text> titleText_ = nullptr;
        std::shared_ptr<Text> statusText_ = nullptr;
        std::shared_ptr<Text> helpText_ = nullptr;
        std::shared_ptr<Text> seatHeaderText_ = nullptr;
        std::shared_ptr<Text> playerHeaderText_ = nullptr;
        std::shared_ptr<Text> winsHeaderText_ = nullptr;
        std::shared_ptr<Text> stateHeaderText_ = nullptr;
        std::array<SeatRowWidgets, net::kMaxPlayers> seatRows_{};

        net::MsgLobbyState lastRenderedLobbyState_{};
        std::optional<bool> pendingReadyState_{};
        uint32_t lastReadyRequestMs_ = 0;
        bool hasRenderedLobbyState_ = false;
        bool returningToMenu_ = false;
    };
} // namespace bomberman

#endif // BOMBERMAN_SCENES_LOBBYSCENE_H
