/**
 * @file ConnectScene.h
 * @brief Multiplayer connect scene interface for the pre-game join flow.
 */

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
     * @brief Pre-game multiplayer connect scene.
     *
     * Owns the user-facing form for player name and server host/address entry,
     * displays async connection progress/failure status from `NetClient`,
     * and transitions into the multiplayer lobby flow once the session is ready.
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

        /** @brief Enables SDL text input while the connect form is active. */
        void onEnter() override;
        /** @brief Disables SDL text input when leaving the connect form. */
        void onExit() override;
        /** @brief Handles field editing, connect submission, and local leave/cancel input. */
        void onEvent(const SDL_Event& event) override;
        /** @brief Polls `NetClient` connection state and updates status/scene flow. */
        void update(unsigned int delta) override;

      private:
        // Placeholder text also reused as fallback connect values when a field is left empty.
        static constexpr std::string_view kDefaultPlayerName = "Player";
        static constexpr std::string_view kDefaultHost       = "127.0.0.1";

        /** @brief Keyboard focus target within the three-step connect form. */
        enum class FocusField : uint8_t
        {
            PlayerName = 0,
            Host = 1,
            ConnectButton = 2
        };

        // ----- Scene Flow and Status -----

        /** @brief Validates inputs and kicks off an async connect via NetClient.
         *
         * No-ops if already connecting/connected. Sets status text on validation failure.
         */
        void tryStartConnect();

        /**
         * @brief Sets the connect-status text content and color in one call.
         *
         * @param message Text to display.
         * @param color Text color.
         */
        void setConnectStatus(std::string_view message, SDL_Color color);

        /** @brief Returns the player name to use, falling back to kDefaultPlayerName if the field is empty. */
        [[nodiscard]]
        std::string_view effectivePlayerName() const;
        /** @brief Returns the host to use, falling back to kDefaultHost if the field is empty. */
        [[nodiscard]]
        std::string_view effectiveHost() const;
        /** @brief Restores the idle status text only when the underlying client is actually disconnected. */
        void restoreIdleStatusAfterHostEdit();

        // ----- Form Helpers -----

        /** @brief Refreshes field text, placeholder rendering, and focus highlighting. */
        void refreshFieldPresentation();
        /** @brief Moves keyboard focus through the form controls with wraparound. */
        void cycleFocus(int direction);
        /** @brief Filters and appends user text for either the player-name or host/address field. */
        void appendSanitizedFieldText(std::string& target, std::string_view chunk, bool isHostField);

        /** @brief Returns the pixel width of the given text using the form font. */
        [[nodiscard]]
        int measureTextWidth(std::string_view text) const;
        [[nodiscard]]
        bool fitsFieldWidth(std::string_view text, bool isHostField) const;

        /** @brief Returns true if @p host is a syntactically valid IPv4 address or hostname. */
        [[nodiscard]]
        static bool isValidHost(std::string_view host);

        /** @brief Recenters editable field values inside their fixed-width form slots. */
        void recenterFieldValues();

        // ----- UI Objects -----

        std::shared_ptr<Text> titleText_ = nullptr;
        std::shared_ptr<Text> playerNameLabelText_ = nullptr;
        std::shared_ptr<Text> playerNameValueText_ = nullptr;
        std::shared_ptr<Text> hostLabelText_ = nullptr;
        std::shared_ptr<Text> hostValueText_ = nullptr;
        std::shared_ptr<Text> portValueText_ = nullptr;
        std::shared_ptr<Text> connectButtonText_ = nullptr;
        std::shared_ptr<Text> statusText_ = nullptr;

        // ----- Scene and Form State -----

        FocusField focusedField_ = FocusField::PlayerName;
        std::string playerName_;
        std::string host_;
        bool playerNameTouched_ = false; ///< True once the player name field has received its first keystroke.
        bool hostTouched_       = false; ///< True once the host field has received its first keystroke.
        uint16_t port_ = 0; ///< Read-only server port, displayed for info only.
        bool transitionedToLobby_ = false; ///< Guards the one-time scene handoff after a successful connection.
        net::EConnectState lastConnectState_ = net::EConnectState::Disconnected; ///< Cached state to avoid redundant status text updates.

        // ----- Cached Layout -----

        int playerNameFieldX_ = 0; ///< Left edge of the player-name value area.
        int playerNameFieldY_ = 0; ///< Top edge of the player-name value area.
        int playerNameFieldW_ = 0; ///< Width of the player-name value area.
        int hostFieldX_ = 0;       ///< Left edge of the host value area.
        int hostFieldY_ = 0;       ///< Top edge of the host value area.
        int hostFieldW_ = 0;       ///< Width of the host value area.
    };
} // namespace bomberman

#endif // _BOMBERMAN_SCENES_CONNECT_SCENE_H_
