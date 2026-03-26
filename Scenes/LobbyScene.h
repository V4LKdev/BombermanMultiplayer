/**
 * @file LobbyScene.h
 * @brief Multiplayer lobby scene shown after a successful session accept.
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
    namespace net { class NetClient; }

    /**
     * @brief Multiplayer lobby scene backed by authoritative server seat state.
     *
     * Renders the current accepted seats, lets the local client toggle its
     * authoritative ready state, and transitions into gameplay only after the
     * server sends a real match bootstrap.
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
        void handleReadyTogglePressed(net::NetClient& netClient);
        bool tryEnterPendingMatch(net::NetClient& netClient);
        void syncPendingReadyState(const net::MsgLobbyState& lobbyState, uint8_t localPlayerId);
        void refreshLobbyPresentationIfChanged(const net::MsgLobbyState& lobbyState, uint8_t localPlayerId);
        void setStatus(std::string_view message, SDL_Color color);
        void setCountdownText(std::string_view message, SDL_Color color);
        void rebuildLobbyPresentation(const net::MsgLobbyState& lobbyState, uint8_t localPlayerId);
        void returnToMenu(bool disconnectClient, std::string_view reason);

        std::shared_ptr<Text> titleText_ = nullptr;
        std::shared_ptr<Text> statusText_ = nullptr;
        std::shared_ptr<Text> countdownText_ = nullptr;
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
        bool lobbyStateStale_ = false;
        bool lastRenderedLobbyStale_ = false;
        bool returningToMenu_ = false;
    };
} // namespace bomberman

#endif // BOMBERMAN_SCENES_LOBBYSCENE_H
