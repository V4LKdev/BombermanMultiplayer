/**
 * @file ConnectScene.cpp
 * @brief Multiplayer connect scene implementation.
 */

#include "Scenes/ConnectScene.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>

#include "Game.h"
#include "LobbyScene.h"
#include "Net/Client/NetClient.h"
#include "Net/NetCommon.h"

namespace
{
    constexpr std::size_t kMaxPlayerNameLen = bomberman::net::kPlayerNameMax - 1;
    constexpr std::size_t kMaxHostLen = 253; // RFC hostname max length; field width clamps practical UI length first.

    [[nodiscard]]
    constexpr bool isAsciiAlphaNum(const char c)
    {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9');
    }

    [[nodiscard]]
    bool isValidHostname(const std::string_view host)
    {
        if (host.empty() || host.size() > kMaxHostLen)
            return false;

        std::size_t labelLength = 0;
        for (std::size_t i = 0; i < host.size(); ++i)
        {
            const char c = host[i];
            if (c == '.')
            {
                if (labelLength == 0 || !isAsciiAlphaNum(host[i - 1]))
                    return false;

                labelLength = 0;
                continue;
            }

            if (labelLength == 0 && !isAsciiAlphaNum(c))
                return false;
            if (!(isAsciiAlphaNum(c) || c == '-'))
                return false;

            ++labelLength;
            if (labelLength > 63)
                return false;
        }

        return labelLength > 0 && isAsciiAlphaNum(host.back());
    }
}

namespace bomberman
{
    // =================================================================================================================
    // ===== Construction and Scene Hooks ==============================================================================
    // =================================================================================================================

    ConnectScene::ConnectScene(Game* _game, uint16_t port) : Scene(_game), port_(port)
    {
        titleText_ = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "ONLINE CONNECT");
        titleText_->setSize(static_cast<int>(game->getWindowWidth() / 2.0f),
                            static_cast<int>(game->getWindowHeight() / 14.0f));
        titleText_->setPosition(static_cast<int>(game->getWindowWidth() / 2.0f - titleText_->getWidth() / 2.0f), 86);
        addObject(titleText_);

        const int centerX = game->getWindowWidth() / 2;

        playerNameFieldW_ = 520;
        playerNameFieldX_ = centerX - (playerNameFieldW_ / 2);
        playerNameFieldY_ = 254;

        hostFieldW_ = 520;
        hostFieldX_ = centerX - (hostFieldW_ / 2);
        hostFieldY_ = 356;

        playerNameLabelText_ = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "PLAYER NAME");
        playerNameLabelText_->setSize(220, 26);
        playerNameLabelText_->setPosition(centerX - 110, 220);
        addObject(playerNameLabelText_);

        playerNameValueText_ = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "");
        playerNameValueText_->setPosition(playerNameFieldX_, playerNameFieldY_);
        addObject(playerNameValueText_);

        hostLabelText_ = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "SERVER HOST");
        hostLabelText_->setSize(220, 26);
        hostLabelText_->setPosition(centerX - 110, 322);
        addObject(hostLabelText_);

        hostValueText_ = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "");
        hostValueText_->setPosition(hostFieldX_, hostFieldY_);
        addObject(hostValueText_);

        constexpr int kPortOffsetY = 38;
        portValueText_ = std::make_shared<Text>(game->getAssetManager()->getFont(16), game->getRenderer(),
                                                "/" + std::to_string(port_));
        portValueText_->fitToContent();
        portValueText_->setPosition(centerX - portValueText_->getWidth() / 2, hostFieldY_ + kPortOffsetY);
        addObject(portValueText_);

        connectButtonText_ = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "CONNECT");
        connectButtonText_->setSize(220, 34);
        connectButtonText_->setPosition(centerX - 110, 456);
        addObject(connectButtonText_);

        statusText_ = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "");
        statusText_->setSize(220, 20);
        statusText_->setPosition(centerX - 110, 498);
        addObject(statusText_);

        refreshFieldPresentation();
        setConnectStatus("Not connected", {150, 150, 150, 255});
    }

    void ConnectScene::onEnter()
    {
        SDL_StartTextInput();
    }

    void ConnectScene::onExit()
    {
        SDL_StopTextInput();
    }

    void ConnectScene::onEvent(const SDL_Event& event)
    {
        Scene::onEvent(event);

        if (event.type == SDL_TEXTINPUT)
        {
            if (focusedField_ == FocusField::PlayerName)
            {
                if (!playerNameTouched_) { playerName_.clear(); playerNameTouched_ = true; }
                appendSanitizedFieldText(playerName_, event.text.text, false);
                refreshFieldPresentation();
            }
            else if (focusedField_ == FocusField::Host)
            {
                if (!hostTouched_) { host_.clear(); hostTouched_ = true; }
                appendSanitizedFieldText(host_, event.text.text, true);
                restoreIdleStatusAfterHostEdit();
                refreshFieldPresentation();
            }
            return;
        }

        if (event.type != SDL_KEYDOWN || event.key.repeat != 0)
            return;

        switch (event.key.keysym.scancode)
        {
            case SDL_SCANCODE_ESCAPE:
            {
                // Local leave from the connect scene only needs to cancel in-flight connect/handshake work.
                net::NetClient* client = game->getNetClient();
                if (client)
                {
                    const auto state = client->connectState();
                    if (state == net::EConnectState::Connecting || state == net::EConnectState::Handshaking)
                        client->cancelConnect();
                }
                game->getSceneManager()->activateScene("menu");
                game->getSceneManager()->removeScene("connect");
                return;
            }

            case SDL_SCANCODE_TAB:
            case SDL_SCANCODE_DOWN:
                cycleFocus(+1);
                refreshFieldPresentation();
                return;

            case SDL_SCANCODE_UP:
                cycleFocus(-1);
                refreshFieldPresentation();
                return;

            case SDL_SCANCODE_BACKSPACE:
                if (focusedField_ == FocusField::PlayerName)
                {
                    if (!playerName_.empty())
                        playerName_.pop_back();
                    if (playerName_.empty()) playerNameTouched_ = false;
                    refreshFieldPresentation();
                }
                else if (focusedField_ == FocusField::Host)
                {
                    if (!host_.empty())
                        host_.pop_back();
                    if (host_.empty()) hostTouched_ = false;
                    restoreIdleStatusAfterHostEdit();
                    refreshFieldPresentation();
                }
                return;

            case SDL_SCANCODE_RETURN:
                if (focusedField_ == FocusField::ConnectButton)
                    tryStartConnect();
                else
                {
                    // RETURN advances focus from a text field.
                    cycleFocus(+1);
                    refreshFieldPresentation();
                }
                return;

            case SDL_SCANCODE_SPACE:
                if (focusedField_ == FocusField::ConnectButton)
                    tryStartConnect();
                // On text fields, SPACE is intentionally not handled here —
                // SDL_TEXTINPUT fires separately and inserts the space character.
                return;

            default:
                return;
        }
    }

    // =================================================================================================================
    // ===== Scene Status and Transition ===============================================================================
    // =================================================================================================================

    void ConnectScene::update(const unsigned int delta)
    {
        Scene::update(delta);

        net::NetClient* client = game->getNetClient();
        if (!client)
            return;

        const auto state = client->connectState();
        if (state == lastConnectState_)
            return;

        lastConnectState_ = state;

        switch (state)
        {
            case net::EConnectState::Disconnected:
                setConnectStatus("Not connected", {150, 150, 150, 255});
                break;
            case net::EConnectState::Connecting:
                setConnectStatus("Connecting...", {180, 180, 60, 255});
                break;
            case net::EConnectState::Handshaking:
                setConnectStatus("Handshaking...", {180, 180, 60, 255});
                break;
            case net::EConnectState::Connected:
                setConnectStatus("Connected!", {80, 220, 80, 255});

                if (!transitionedToLobby_)
                {
                    transitionedToLobby_ = true;
                    game->getSceneManager()->addScene(
                        "lobby",
                        std::make_shared<LobbyScene>(game));
                    game->getSceneManager()->activateScene("lobby");
                    game->getSceneManager()->removeScene("connect");
                }
                break;
            case net::EConnectState::Disconnecting:
                setConnectStatus("Disconnecting...", {180, 180, 60, 255});
                break;
            case net::EConnectState::FailedResolve:
                setConnectStatus("Could not resolve host", {220, 80, 80, 255});
                break;
            case net::EConnectState::FailedConnect:
                setConnectStatus("Could not connect", {220, 80, 80, 255});
                break;
            case net::EConnectState::FailedHandshake:
                if (const auto& rejectReason = client->lastRejectReason(); rejectReason.has_value())
                {
                    switch (*rejectReason)
                    {
                        case net::MsgReject::EReason::ServerFull:
                            setConnectStatus("Server is full", {220, 80, 80, 255});
                            break;
                        case net::MsgReject::EReason::Banned:
                            setConnectStatus("Access denied", {220, 80, 80, 255});
                            break;
                        case net::MsgReject::EReason::GameInProgress:
                            setConnectStatus("Match already running", {220, 80, 80, 255});
                            break;
                        case net::MsgReject::EReason::Other:
                        default:
                            setConnectStatus("Connection rejected", {220, 80, 80, 255});
                            break;
                    }
                }
                else
                {
                    setConnectStatus("Handshake failed", {220, 80, 80, 255});
                }
                break;
            case net::EConnectState::FailedProtocol:
                setConnectStatus("Protocol version mismatch", {220, 80, 80, 255});
                break;
            case net::EConnectState::FailedInit:
                setConnectStatus("Network init failed", {220, 80, 80, 255});
                break;
        }
    }

    void ConnectScene::tryStartConnect()
    {
        net::NetClient* client = game->getNetClient();
        if (!client)
        {
            setConnectStatus("No network client", {220, 80, 80, 255});
            return;
        }

        // Ignore if already mid-connect or connected.
        const auto state = client->connectState();
        if (state == net::EConnectState::Connecting ||
            state == net::EConnectState::Handshaking ||
            state == net::EConnectState::Disconnecting)
            return;
        if (state == net::EConnectState::Connected)
            return;

        const std::string_view host = effectiveHost();
        if (!isValidHost(host))
        {
            setConnectStatus("Invalid host/address", {220, 80, 80, 255});
            return;
        }

        client->beginConnect(std::string(host), port_, effectivePlayerName());
        // User-facing status is updated from the polled connection state in update().
    }

    void ConnectScene::setConnectStatus(const std::string_view message, const SDL_Color color)
    {
        statusText_->setText(std::string(message));
        statusText_->setColor(color);
    }

    std::string_view ConnectScene::effectivePlayerName() const
    {
        return playerName_.empty() ? kDefaultPlayerName : std::string_view(playerName_);
    }

    std::string_view ConnectScene::effectiveHost() const
    {
        return host_.empty() ? kDefaultHost : std::string_view(host_);
    }

    void ConnectScene::restoreIdleStatusAfterHostEdit()
    {
        const net::NetClient* client = game->getNetClient();
        if (client == nullptr ||
            client->connectState() == net::EConnectState::Disconnected ||
            net::isFailedState(client->connectState()))
        {
            setConnectStatus("Not connected", {150, 150, 150, 255});
        }
    }

    // =================================================================================================================
    // ===== Form Presentation and Input Helpers =======================================================================
    // =================================================================================================================

    void ConnectScene::refreshFieldPresentation()
    {
        constexpr SDL_Color kLabelColor {200, 200, 200, 255};
        constexpr SDL_Color kValueColor {255, 255, 255, 255};
        constexpr SDL_Color kFocusColor {66, 134, 244, 255};
        constexpr SDL_Color kHintColor {150, 150, 150, 255};
        constexpr SDL_Color kReadOnlyColor {180, 180, 180, 255};

        playerNameLabelText_->setColor(kLabelColor);
        hostLabelText_->setColor(kLabelColor);
        portValueText_->setColor(kReadOnlyColor);
        // Connection status color is owned by setConnectStatus().

        const bool showNamePlaceholder = playerName_.empty();
        const bool showHostPlaceholder = host_.empty();

        playerNameValueText_->setText(showNamePlaceholder ? std::string(kDefaultPlayerName) : playerName_);
        hostValueText_->setText(showHostPlaceholder ? std::string(kDefaultHost) : host_);
        recenterFieldValues();

        playerNameValueText_->setColor(
            focusedField_ == FocusField::PlayerName ? kFocusColor : (showNamePlaceholder ? kHintColor : kValueColor));
        hostValueText_->setColor(
            focusedField_ == FocusField::Host ? kFocusColor : (showHostPlaceholder ? kHintColor : kValueColor));
        connectButtonText_->setColor(focusedField_ == FocusField::ConnectButton ? kFocusColor : kValueColor);
    }

    void ConnectScene::cycleFocus(const int direction)
    {
        int index = static_cast<int>(focusedField_);
        index = (index + direction + 3) % 3;
        focusedField_ = static_cast<FocusField>(index);
    }

    void ConnectScene::appendSanitizedFieldText(std::string& target, const std::string_view chunk, const bool isHostField)
    {
        if (!isHostField)
        {
            for (const char c : chunk)
            {
                if (target.size() >= kMaxPlayerNameLen)
                    break;

                if (c < 32 || c == 127)
                    continue;

                const bool isAlphaNum = (c >= 'a' && c <= 'z') ||
                                        (c >= 'A' && c <= 'Z') ||
                                        (c >= '0' && c <= '9');
                const bool isAllowedPunct = (c == ' ' || c == '_' || c == '-');
                if (!isAlphaNum && !isAllowedPunct)
                    continue;

                std::string candidate = target;
                candidate.push_back(c);
                if (fitsFieldWidth(candidate, false))
                    target = std::move(candidate);
            }
            return;
        }

        for (const char c : chunk)
        {
            if (target.size() >= kMaxHostLen)
                break;

            if (c < 32 || c == 127)
                continue;

            if (!(isAsciiAlphaNum(c) || c == '.' || c == '-'))
                continue;

            std::string candidate = target;
            candidate.push_back(c);
            if (fitsFieldWidth(candidate, true))
                target = std::move(candidate);
        }
    }

    int ConnectScene::measureTextWidth(const std::string_view text) const
    {
        auto font = game->getAssetManager()->getFont();
        if (!font)
            return 0;

        int width = 0;
        int height = 0;
        const std::string safeText = text.empty() ? " " : std::string(text);
        TTF_SizeUTF8(font.get(), safeText.c_str(), &width, &height);
        return width;
    }

    bool ConnectScene::fitsFieldWidth(const std::string_view text, const bool isHostField) const
    {
        const int fieldWidth = isHostField ? hostFieldW_ : playerNameFieldW_;
        return measureTextWidth(text) <= fieldWidth;
    }

    bool ConnectScene::isValidHost(const std::string_view host)
    {
        if (host.empty())
            return false;

        if (host.find_first_not_of("0123456789.") == std::string_view::npos)
        {
            std::string_view ip = host;
            int octetCount = 0;
            while (!ip.empty())
            {
                const auto dotPos = ip.find('.');
                const std::string_view token = (dotPos == std::string_view::npos) ? ip : ip.substr(0, dotPos);

                if (token.empty() || token.size() > 3)
                    return false;

                int value = 0;
                const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
                if (ec != std::errc{} || ptr != token.data() + token.size())
                    return false;
                if (value < 0 || value > 255)
                    return false;

                ++octetCount;
                if (octetCount > 4)
                    return false;

                if (dotPos == std::string_view::npos)
                    break;
                ip = ip.substr(dotPos + 1);
            }

            return octetCount == 4;
        }

        return isValidHostname(host);
    }

    void ConnectScene::recenterFieldValues()
    {
        playerNameValueText_->fitToContent();
        hostValueText_->fitToContent();

        const int playerNameX = playerNameFieldX_ + ((playerNameFieldW_ - playerNameValueText_->getWidth()) / 2);
        const int hostX = hostFieldX_ + ((hostFieldW_ - hostValueText_->getWidth()) / 2);

        // Clamp to field left edge when content width exceeds the field box.
        playerNameValueText_->setPosition(std::max(playerNameFieldX_, playerNameX), playerNameFieldY_);
        hostValueText_->setPosition(std::max(hostFieldX_, hostX), hostFieldY_);
    }
} // namespace bomberman
