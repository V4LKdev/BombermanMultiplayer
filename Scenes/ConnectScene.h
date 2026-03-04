#ifndef _BOMBERMAN_SCENES_CONNECT_SCENE_H_
#define _BOMBERMAN_SCENES_CONNECT_SCENE_H_

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "Entities/Text.h"
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

        /**
         * @brief Enables SDL text input while this scene is active.
         */
        virtual void onEnter() override;

        /**
         * @brief Disables SDL text input when leaving this scene.
         */
        virtual void onExit() override;

        /**
         * @brief Handles form focus and text input events.
         */
        virtual void onEvent(const SDL_Event& event) override;

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
        [[nodiscard]] int measureTextWidth(std::string_view text) const;
        [[nodiscard]] bool fitsFieldWidth(std::string_view text, bool isHostField) const;

        /** @brief Returns true if @p ip is a syntactically valid dotted-decimal IPv4 address. */
        [[nodiscard]] static bool isValidIPv4(std::string_view ip);

        void recenterValueText();

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

        int playerNameFieldX_ = 0;
        int playerNameFieldY_ = 0;
        int playerNameFieldW_ = 0;
        int hostFieldX_ = 0;
        int hostFieldY_ = 0;
        int hostFieldW_ = 0;
    };
} // namespace bomberman

#endif // _BOMBERMAN_SCENES_CONNECT_SCENE_H_
