/**
 * @file LobbyScene.cpp
 * @brief Passive multiplayer lobby scene implementation.
 */

#include "Scenes/LobbyScene.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "Game.h"
#include "Net/NetClient.h"
#include "Util/Log.h"

namespace bomberman
{
    namespace
    {
        constexpr int kTitleY = 72;
        constexpr int kStatusY = 126;
        constexpr int kHelpY = 520;
        constexpr int kHeaderY = 182;
        constexpr int kSeatStartY = 220;
        constexpr int kSeatRowStep = 48;
        constexpr int kTableWidth = 700;
        constexpr int kSeatColumnOffset = 0;
        constexpr int kLocalTagColumnOffset = 74;
        constexpr int kNameColumnOffset = 146;
        constexpr int kWinsColumnOffset = 470;
        constexpr int kStateColumnOffset = 566;

        [[nodiscard]]
        bool lobbyStatesEqual(const net::MsgLobbyState& lhs, const net::MsgLobbyState& rhs)
        {
            return std::memcmp(&lhs, &rhs, sizeof(net::MsgLobbyState)) == 0;
        }

        [[nodiscard]]
        int tableLeftX(const Game& game)
        {
            return std::max(56, game.getWindowWidth() / 2 - kTableWidth / 2);
        }

        [[nodiscard]]
        int rowY(const uint8_t playerId)
        {
            return kSeatStartY + static_cast<int>(playerId) * kSeatRowStep;
        }

        void setTextContent(const std::shared_ptr<Text>& text, const std::string& content, const int x, const int y)
        {
            text->setText(content);
            text->fitToContent();
            text->setPosition(x, y);
        }

        void setSeatRowColor(const LobbyScene::SeatRowWidgets& row, const SDL_Color color)
        {
            row.seatText->setColor(color);
            row.localTagText->setColor(color);
            row.nameText->setColor(color);
            row.winsText->setColor(color);
            row.stateText->setColor(color);
        }

        [[nodiscard]]
        std::string seatLabel(const uint8_t playerId)
        {
            return "P" + std::to_string(static_cast<unsigned int>(playerId) + 1u);
        }

        [[nodiscard]]
        std::string seatDisplayName(const net::MsgLobbyState::SeatEntry& seat)
        {
            if (!net::lobbySeatIsOccupied(seat))
                return "Open seat";

            const std::string_view displayName = net::lobbySeatName(seat).empty()
                ? std::string_view("Player")
                : net::lobbySeatName(seat);
            return std::string(displayName);
        }

        [[nodiscard]]
        std::string seatWinsText(const net::MsgLobbyState::SeatEntry& seat)
        {
            if (!net::lobbySeatIsOccupied(seat))
                return "-";

            return std::to_string(static_cast<unsigned int>(seat.wins));
        }

        [[nodiscard]]
        std::string seatStateText(const net::MsgLobbyState::SeatEntry& seat)
        {
            if (!net::lobbySeatIsOccupied(seat))
                return "-";

            return net::lobbySeatIsReady(seat) ? "READY" : "NOT READY";
        }

        [[nodiscard]]
        std::string localSeatTag(const uint8_t playerId,
                                 const net::MsgLobbyState::SeatEntry& seat,
                                 const uint8_t localPlayerId)
        {
            if (!net::lobbySeatIsOccupied(seat) || playerId != localPlayerId)
                return "";

            return "YOU";
        }

        [[nodiscard]]
        SDL_Color seatRowColor(const uint8_t playerId,
                               const net::MsgLobbyState::SeatEntry& seat,
                               const uint8_t localPlayerId)
        {
            if (!net::lobbySeatIsOccupied(seat))
                return SDL_Color{0x90, 0x90, 0x90, 0xFF};

            if (playerId == localPlayerId)
                return SDL_Color{0x66, 0xD6, 0xFF, 0xFF};

            return SDL_Color{0xF0, 0xF0, 0xF0, 0xFF};
        }

        void populateSeatRow(const LobbyScene::SeatRowWidgets& row,
                             const int leftX,
                             const uint8_t playerId,
                             const net::MsgLobbyState::SeatEntry& seat,
                             const uint8_t localPlayerId)
        {
            const int y = rowY(playerId);
            setTextContent(row.seatText, seatLabel(playerId), leftX + kSeatColumnOffset, y);
            setTextContent(row.localTagText, localSeatTag(playerId, seat, localPlayerId), leftX + kLocalTagColumnOffset, y);
            setTextContent(row.nameText, seatDisplayName(seat), leftX + kNameColumnOffset, y);
            setTextContent(row.winsText, seatWinsText(seat), leftX + kWinsColumnOffset, y);
            setTextContent(row.stateText, seatStateText(seat), leftX + kStateColumnOffset, y);
            setSeatRowColor(row, seatRowColor(playerId, seat, localPlayerId));
        }
    } // namespace

    LobbyScene::LobbyScene(Game* game) : Scene(game)
    {
        const int leftX = tableLeftX(*game);

        auto titleFont = game->getAssetManager()->getFont();
        titleText_ = std::make_shared<Text>(titleFont, game->getRenderer(), "ONLINE LOBBY");
        titleText_->fitToContent();
        titleText_->setPosition(game->getWindowWidth() / 2 - titleText_->getWidth() / 2, kTitleY);
        addObject(titleText_);

        auto bodyFont = game->getAssetManager()->getFont(18);
        auto headerFont = game->getAssetManager()->getFont(16);
        statusText_ = std::make_shared<Text>(bodyFont, game->getRenderer(), "Waiting for lobby...");
        statusText_->fitToContent();
        statusText_->setPosition(game->getWindowWidth() / 2 - statusText_->getWidth() / 2, kStatusY);
        statusText_->setColor(SDL_Color{0xFF, 0xD1, 0x66, 0xFF});
        addObject(statusText_);

        helpText_ = std::make_shared<Text>(bodyFont, game->getRenderer(), "ESC TO LEAVE");
        helpText_->fitToContent();
        helpText_->setColor(SDL_Color{0xB0, 0xB0, 0xB0, 0xFF});
        helpText_->setPosition(game->getWindowWidth() / 2 - helpText_->getWidth() / 2, kHelpY);
        addObject(helpText_);

        const SDL_Color headerColor{0xC8, 0xC8, 0xC8, 0xFF};
        seatHeaderText_ = std::make_shared<Text>(headerFont, game->getRenderer(), "SEAT");
        seatHeaderText_->fitToContent();
        seatHeaderText_->setColor(headerColor);
        seatHeaderText_->setPosition(leftX + kSeatColumnOffset, kHeaderY);
        addObject(seatHeaderText_);

        playerHeaderText_ = std::make_shared<Text>(headerFont, game->getRenderer(), "PLAYER");
        playerHeaderText_->fitToContent();
        playerHeaderText_->setColor(headerColor);
        playerHeaderText_->setPosition(leftX + kNameColumnOffset, kHeaderY);
        addObject(playerHeaderText_);

        winsHeaderText_ = std::make_shared<Text>(headerFont, game->getRenderer(), "WINS");
        winsHeaderText_->fitToContent();
        winsHeaderText_->setColor(headerColor);
        winsHeaderText_->setPosition(leftX + kWinsColumnOffset, kHeaderY);
        addObject(winsHeaderText_);

        stateHeaderText_ = std::make_shared<Text>(headerFont, game->getRenderer(), "STATE");
        stateHeaderText_->fitToContent();
        stateHeaderText_->setColor(headerColor);
        stateHeaderText_->setPosition(leftX + kStateColumnOffset, kHeaderY);
        addObject(stateHeaderText_);

        for (std::size_t i = 0; i < seatRows_.size(); ++i)
        {
            auto& row = seatRows_[i];
            row.seatText = std::make_shared<Text>(bodyFont, game->getRenderer(), "");
            row.localTagText = std::make_shared<Text>(bodyFont, game->getRenderer(), "");
            row.nameText = std::make_shared<Text>(bodyFont, game->getRenderer(), "");
            row.winsText = std::make_shared<Text>(bodyFont, game->getRenderer(), "");
            row.stateText = std::make_shared<Text>(bodyFont, game->getRenderer(), "");

            addObject(row.seatText);
            addObject(row.localTagText);
            addObject(row.nameText);
            addObject(row.winsText);
            addObject(row.stateText);
        }

        for (uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
        {
            const net::MsgLobbyState::SeatEntry emptySeat{};
            populateSeatRow(seatRows_[playerId], leftX, playerId, emptySeat, net::NetClient::kInvalidPlayerId);
        }
    }

    void LobbyScene::onEvent(const SDL_Event& event)
    {
        Scene::onEvent(event);

        if (event.type != SDL_KEYDOWN || event.key.repeat != 0)
            return;

        if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
        {
            returnToMenu(true, "LocalLeaveLobby");
        }
    }

    void LobbyScene::update(const unsigned int delta)
    {
        Scene::update(delta);

        if (returningToMenu_)
            return;

        net::NetClient* netClient = game->getNetClient();
        if (netClient == nullptr || netClient->connectState() != net::EConnectState::Connected)
        {
            const auto stateName =
                netClient ? net::connectStateName(netClient->connectState()) : std::string_view("NoClient");
            returnToMenu(false, stateName);
            return;
        }

        net::MsgLobbyState lobbyState{};
        if (!netClient->tryGetLatestLobbyState(lobbyState))
        {
            setStatus("Waiting for lobby...", SDL_Color{0xFF, 0xD1, 0x66, 0xFF});
            return;
        }

        if (!hasRenderedLobbyState_ || !lobbyStatesEqual(lastRenderedLobbyState_, lobbyState))
        {
            rebuildLobbyPresentation(lobbyState, netClient->playerId());
            lastRenderedLobbyState_ = lobbyState;
            hasRenderedLobbyState_ = true;
        }
    }

    void LobbyScene::setStatus(const std::string_view message, const SDL_Color color)
    {
        statusText_->setText(std::string(message));
        statusText_->fitToContent();
        statusText_->setColor(color);

        if (message.empty())
        {
            statusText_->setPosition(-4096, -4096);
            return;
        }

        statusText_->setPosition(game->getWindowWidth() / 2 - statusText_->getWidth() / 2, kStatusY);
    }

    void LobbyScene::rebuildLobbyPresentation(const net::MsgLobbyState& lobbyState, const uint8_t localPlayerId)
    {
        const int leftX = tableLeftX(*game);

        for (uint8_t playerId = 0; playerId < net::kMaxPlayers; ++playerId)
        {
            populateSeatRow(seatRows_[playerId], leftX, playerId, lobbyState.seats[playerId], localPlayerId);
        }

        setStatus("", SDL_Color{0x80, 0xDC, 0x80, 0xFF});
    }

    void LobbyScene::returnToMenu(const bool disconnectClient, const std::string_view reason)
    {
        if (returningToMenu_)
            return;

        returningToMenu_ = true;

        if (disconnectClient)
        {
            LOG_NET_CONN_INFO("Leaving lobby and disconnecting: {}", reason);
            game->disconnectNetClientIfActive(false);
        }
        else
        {
            LOG_NET_CONN_WARN("Lobby lost connection (state={}) - returning to menu", reason);
        }

        game->getSceneManager()->activateScene("menu");
        game->getSceneManager()->removeScene("lobby");
    }
} // namespace bomberman
