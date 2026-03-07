#ifndef _BOMBERMAN_SCENES_CONNECT_SCENE_H_
#define _BOMBERMAN_SCENES_CONNECT_SCENE_H_

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "Entities/Text.h"
#include "Net/NetClient.h"
#include "Scenes/Scene.h"

namespace bomberman
{
    /**
     * @brief Scene for connecting to a multiplayer server, allowing the user to input player name and server IP address.
     */
    class ConnectScene : public Scene
    {
      public:
        /**
         * @brief Constructs the connect scene UI scaffold.
         *
         * @param game Owning game runtime.
         * @param port Server port displayed as read-only info (not user-editable).
         */
        ConnectScene(Game* game, uint16_t port);

        virtual void onEnter() override;
        virtual void onExit() override;
        virtual void onEvent(const SDL_Event& event) override;
        virtual void update(unsigned int delta) override;

      private:
        // Default placeholder values, also used as fallback when fields are empty at connect time.
        static constexpr std::string_view kDefaultPlayerName = "Player";
        static constexpr std::string_view kDefaultHost       = "127.0.0.1";

        enum class FocusField : uint8_t
        {
            PlayerName = 0,
            Host = 1,
            ConnectButton = 2
        };

        void refreshFormText();
        void advanceFocus(int direction);
        void appendSanitizedText(std::string& target, std::string_view chunk, bool isHostField);

        /** @brief Returns the pixel width of the given text using the form font. */
        [[nodiscard]]
        int measureTextWidth(std::string_view text) const;
        [[nodiscard]]
        bool fitsFieldWidth(std::string_view text, bool isHostField) const;

        /** @brief Returns true if @p ip is a syntactically valid dotted-decimal IPv4 address. */
        [[nodiscard]]
        static bool isValidIPv4(std::string_view ip);

        void recenterValueText();

        /** @brief Returns the player name to use, falling back to kDefaultPlayerName if the field is empty. */
        [[nodiscard]]
        std::string_view effectivePlayerName() const;
        /** @brief Returns the host to use, falling back to kDefaultHost if the field is empty. */
        [[nodiscard]]
        std::string_view effectiveHost() const;

        /**
         * @brief Validates inputs and kicks off an async connect via NetClient.
         *
         * No-ops if already connecting/connected. Sets status text on validation failure.
         */
        void tryStartConnect();

        /**
         * @brief Sets connectStateText content and color in one call.
         *
         * @param message  Text to display.
         * @param color    Text color.
         */
        void setStatusText(std::string_view message, SDL_Color color);

        std::shared_ptr<Text> titleText          = nullptr;
        std::shared_ptr<Text> playerNameLabelText = nullptr;
        std::shared_ptr<Text> playerNameValueText = nullptr;
        std::shared_ptr<Text> hostLabelText       = nullptr;
        std::shared_ptr<Text> hostValueText       = nullptr;
        std::shared_ptr<Text> portValueText       = nullptr;
        std::shared_ptr<Text> connectButtonText   = nullptr;
        std::shared_ptr<Text> connectStateText    = nullptr;

        FocusField focusField_ = FocusField::PlayerName;
        std::string playerName_;
        std::string host_;
        bool playerNameTouched_ = false; ///< True once the player name field has received its first keystroke.
        bool hostTouched_       = false; ///< True once the host field has received its first keystroke.
        uint16_t port_ = 0; ///< Read-only server port, displayed for info only.

        bool enteredOnlineFlow_ = false;

        int playerNameFieldX_ = 0;
        int playerNameFieldY_ = 0;
        int playerNameFieldW_ = 0;
        int hostFieldX_ = 0;
        int hostFieldY_ = 0;
        int hostFieldW_ = 0;

        net::EConnectState lastConnectState_ = net::EConnectState::Disconnected; ///< Cached state to avoid redundant status text updates.
    };
} // namespace bomberman

#endif // _BOMBERMAN_SCENES_CONNECT_SCENE_H_
