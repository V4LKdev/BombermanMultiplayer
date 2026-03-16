#include "Scenes/ConnectScene.h"

#include <algorithm>
#include <charconv>
#include <string>

#include "Game.h"
#include "StageScene.h"
#include "Net/NetClient.h"
#include "Net/NetCommon.h"

namespace
{
    constexpr std::size_t kMaxPlayerNameLen = bomberman::net::kPlayerNameMax - 1;
    constexpr std::size_t kMaxIpv4HostLen = 15; // e.g. 255.255.255.255
}

namespace bomberman
{
    ConnectScene::ConnectScene(Game* _game, uint16_t port) : Scene(_game), port_(port)
    {
        titleText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "ONLINE CONNECT");
        titleText->setSize(static_cast<int>(game->getWindowWidth() / 2.0f),
                           static_cast<int>(game->getWindowHeight() / 14.0f));
        titleText->setPosition(static_cast<int>(game->getWindowWidth() / 2.0f - titleText->getWidth() / 2.0f), 86);
        addObject(titleText);

        const int centerX = game->getWindowWidth() / 2;

        playerNameFieldW_ = 520;
        playerNameFieldX_ = centerX - (playerNameFieldW_ / 2);
        playerNameFieldY_ = 254;

        hostFieldW_ = 520;
        hostFieldX_ = centerX - (hostFieldW_ / 2);
        hostFieldY_ = 356;

        // --- Player name ---
        playerNameLabelText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "PLAYER NAME");
        playerNameLabelText->setSize(220, 26);
        playerNameLabelText->setPosition(centerX - 110, 220);
        addObject(playerNameLabelText);

        playerNameValueText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "");
        playerNameValueText->setPosition(playerNameFieldX_, playerNameFieldY_);
        addObject(playerNameValueText);

        // --- Server IP ---
        hostLabelText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "SERVER IP");
        hostLabelText->setSize(220, 26);
        hostLabelText->setPosition(centerX - 110, 322);
        addObject(hostLabelText);

        hostValueText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "");
        hostValueText->setPosition(hostFieldX_, hostFieldY_);
        addObject(hostValueText);

        // --- Port (read-only) ---
        constexpr int kPortOffsetY = 38;
        portValueText = std::make_shared<Text>(game->getAssetManager()->getFont(16), game->getRenderer(),
                                               "/" + std::to_string(port_));
        portValueText->fitToContent();
        portValueText->setPosition(centerX - portValueText->getWidth() / 2, hostFieldY_ + kPortOffsetY);
        addObject(portValueText);

        // --- Connect button ---
        connectButtonText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "CONNECT");
        connectButtonText->setSize(220, 34);
        connectButtonText->setPosition(centerX - 110, 456);
        addObject(connectButtonText);

        // --- Status ---
        connectStateText = std::make_shared<Text>(game->getAssetManager()->getFont(), game->getRenderer(), "");
        connectStateText->setSize(220, 20);
        connectStateText->setPosition(centerX - 110, 498);
        addObject(connectStateText);

        refreshFormText();
        setStatusText("Not connected", {150, 150, 150, 255});
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
            if (focusField_ == FocusField::PlayerName)
            {
                if (!playerNameTouched_) { playerName_.clear(); playerNameTouched_ = true; }
                appendSanitizedText(playerName_, event.text.text, false);
                refreshFormText();
            }
            else if (focusField_ == FocusField::Host)
            {
                if (!hostTouched_) { host_.clear(); hostTouched_ = true; }
                appendSanitizedText(host_, event.text.text, true);
                // Clear stale failure message when user edits; don't touch it while a connect is in progress.
                if (lastConnectState_ == net::EConnectState::Disconnected)
                    setStatusText("Not connected", {150, 150, 150, 255});
                refreshFormText();
            }
            return;
        }

        if (event.type != SDL_KEYDOWN || event.key.repeat != 0)
            return;

        switch (event.key.keysym.scancode)
        {
            case SDL_SCANCODE_ESCAPE:
            {
                // Cancel any in-progress connection attempt before leaving.
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
                advanceFocus(+1);
                refreshFormText();
                return;

            case SDL_SCANCODE_UP:
                advanceFocus(-1);
                refreshFormText();
                return;

            case SDL_SCANCODE_BACKSPACE:
                if (focusField_ == FocusField::PlayerName)
                {
                    if (!playerName_.empty())
                        playerName_.pop_back();
                    if (playerName_.empty()) playerNameTouched_ = false;
                    refreshFormText();
                }
                else if (focusField_ == FocusField::Host)
                {
                    if (!host_.empty())
                        host_.pop_back();
                    if (host_.empty()) hostTouched_ = false;
                    // Clear validation error as the user edits.
                    if (lastConnectState_ == net::EConnectState::Disconnected)
                        setStatusText("Not connected", {150, 150, 150, 255});
                    refreshFormText();
                }
                return;

            case SDL_SCANCODE_RETURN:
                if (focusField_ == FocusField::ConnectButton)
                    tryStartConnect();
                else
                {
                    // RETURN advances focus from a text field.
                    advanceFocus(+1);
                    refreshFormText();
                }
                return;

            case SDL_SCANCODE_SPACE:
                if (focusField_ == FocusField::ConnectButton)
                    tryStartConnect();
                // On text fields, SPACE is intentionally not handled here —
                // SDL_TEXTINPUT fires separately and inserts the space character.
                return;

            default:
                return;
        }
    }

    void ConnectScene::refreshFormText()
    {
        constexpr SDL_Color kLabelColor   {200, 200, 200, 255};
        constexpr SDL_Color kValueColor   {255, 255, 255, 255};
        constexpr SDL_Color kFocusColor   { 66, 134, 244, 255};
        constexpr SDL_Color kHintColor    {150, 150, 150, 255};
        constexpr SDL_Color kReadOnlyColor{180, 180, 180, 255};

        playerNameLabelText->setColor(kLabelColor);
        hostLabelText->setColor(kLabelColor);
        portValueText->setColor(kReadOnlyColor);
        // Status text color is managed by setStatusText; don't override it here.

        const bool showNamePlaceholder = playerName_.empty();
        const bool showHostPlaceholder = host_.empty();

        playerNameValueText->setText(showNamePlaceholder ? std::string(kDefaultPlayerName) : playerName_);
        hostValueText->setText(showHostPlaceholder ? std::string(kDefaultHost) : host_);
        recenterValueText();

        playerNameValueText->setColor(focusField_ == FocusField::PlayerName ? kFocusColor :
            (showNamePlaceholder ? kHintColor : kValueColor));
        hostValueText->setColor(focusField_ == FocusField::Host ? kFocusColor :
            (showHostPlaceholder ? kHintColor : kValueColor));
        connectButtonText->setColor(focusField_ == FocusField::ConnectButton ? kFocusColor : kValueColor);
    }

    void ConnectScene::advanceFocus(const int direction)
    {
        // Reset touched flag so re-entering an empty field shows the placeholder again.
        if (focusField_ == FocusField::PlayerName) playerNameTouched_ = false;
        else if (focusField_ == FocusField::Host)  hostTouched_ = false;

        int idx = static_cast<int>(focusField_);
        idx = (idx + direction + 3) % 3;
        focusField_ = static_cast<FocusField>(idx);
    }

    void ConnectScene::appendSanitizedText(std::string& target, const std::string_view chunk, const bool isHostField)
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
            if (target.size() >= kMaxIpv4HostLen)
                break;

            if (c < 32 || c == 127)
                continue;

            if (!((c >= '0' && c <= '9') || c == '.'))
                continue;

            target.push_back(c);
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

    bool ConnectScene::isValidIPv4(std::string_view ip)
    {
        int octetCount = 0;
        while (!ip.empty())
        {
            // Find next dot or end
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

    void ConnectScene::recenterValueText()
    {
        playerNameValueText->fitToContent();
        hostValueText->fitToContent();

        const int nameX = playerNameFieldX_ + ((playerNameFieldW_ - playerNameValueText->getWidth()) / 2);
        const int hostX = hostFieldX_ + ((hostFieldW_ - hostValueText->getWidth()) / 2);

        // Clamp to field left edge when content width exceeds the field box.
        playerNameValueText->setPosition(std::max(playerNameFieldX_, nameX), playerNameFieldY_);
        hostValueText->setPosition(std::max(hostFieldX_, hostX), hostFieldY_);
    }

    // =================================================================================================================
    // ==== Connection logic and status updates ========================================================================
    // =================================================================================================================

    std::string_view ConnectScene::effectivePlayerName() const
    {
        return playerName_.empty() ? kDefaultPlayerName : std::string_view(playerName_);
    }

    std::string_view ConnectScene::effectiveHost() const
    {
        return host_.empty() ? kDefaultHost : std::string_view(host_);
    }

    void ConnectScene::setStatusText(const std::string_view message, const SDL_Color color)
    {
        connectStateText->setText(std::string(message));
        connectStateText->setColor(color);
    }

    void ConnectScene::tryStartConnect()
    {
        net::NetClient* client = game->getNetClient();
        if (!client)
        {
            setStatusText("No network client", {220, 80, 80, 255});
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
        if (!isValidIPv4(host))
        {
            setStatusText("Invalid IP address", {220, 80, 80, 255});
            return;
        }

        client->beginConnect(std::string(host), port_, effectivePlayerName());
        // Status text will be updated on the next update() tick once state changes.
    }

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
                setStatusText("Not connected", {150, 150, 150, 255});
                break;
            case net::EConnectState::Connecting:
                setStatusText("Connecting...", {180, 180, 60, 255});
                break;
            case net::EConnectState::Handshaking:
                setStatusText("Handshaking...", {180, 180, 60, 255});
                break;
            case net::EConnectState::Connected:
                setStatusText("Connected!", {80, 220, 80, 255});

                if (!enteredOnlineFlow_)
                {
                    enteredOnlineFlow_ = true;
                    game->getSceneManager()->addScene("stage", std::make_shared<StageScene>(game, 1, 0, LevelMode::Multiplayer));
                    game->getSceneManager()->activateScene("stage");
                    game->getSceneManager()->removeScene("connect");
                }
                break;
            case net::EConnectState::Disconnecting:
                setStatusText("Disconnecting...", {180, 180, 60, 255});
                break;
            case net::EConnectState::FailedResolve:
                setStatusText("Could not resolve host", {220, 80, 80, 255});
                break;
            case net::EConnectState::FailedConnect:
                setStatusText("Connection timed out", {220, 80, 80, 255});
                break;
            case net::EConnectState::FailedHandshake:
                if (const auto& rejectReason = client->lastRejectReason(); rejectReason.has_value())
                {
                    switch (*rejectReason)
                    {
                        case net::MsgReject::EReason::ServerFull:
                            setStatusText("Server is full", {220, 80, 80, 255});
                            break;
                        case net::MsgReject::EReason::Banned:
                            setStatusText("Access denied", {220, 80, 80, 255});
                            break;
                        case net::MsgReject::EReason::Other:
                        default:
                            setStatusText("Connection rejected", {220, 80, 80, 255});
                            break;
                    }
                }
                else
                {
                    setStatusText("Handshake failed", {220, 80, 80, 255});
                }
                break;
            case net::EConnectState::FailedProtocol:
                setStatusText("Protocol version mismatch", {220, 80, 80, 255});
                break;
            case net::EConnectState::FailedInit:
                setStatusText("Network init failed", {220, 80, 80, 255});
                break;
        }
    }
} // namespace bomberman
