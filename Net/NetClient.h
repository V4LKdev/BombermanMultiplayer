#ifndef BOMBERMAN_NET_NETCLIENT_H
#define BOMBERMAN_NET_NETCLIENT_H

#include <memory>
#include <string_view>
#include <string>

#include "NetCommon.h"

namespace bomberman::net
{
    // =================================================================================================================
    // Connection State
    // =================================================================================================================

    /**
     * @brief Represents the current state of the client connection lifecycle.
     *
     * Used as internal state and as external status for callers.
     */
    enum class EConnectState : uint8_t
    {
        Disconnected,       ///< Not connected, no resources held
        Connecting,         ///< ENet connect in progress, waiting for CONNECT event
        Handshaking,        ///< TCP-level connected, waiting for Welcome
        Connected,          ///< Fully connected and handshake complete
        FailedResolve,      ///< Could not resolve host address
        FailedConnect,      ///< ENet connect attempt timed out
        FailedHandshake,    ///< Handshake timed out or was rejected
        FailedProtocol,     ///< Protocol version mismatch
        FailedInit,         ///< ENet or host creation failure
    };

    /** @brief Returns true if the state represents a terminal failure. */
    [[nodiscard]] constexpr bool isFailedState(EConnectState s)
    {
        return s >= EConnectState::FailedResolve;
    }

    /** @brief Returns a human-readable label for a connection state. */
    constexpr std::string_view connectStateName(EConnectState s)
    {
        switch (s)
        {
            case EConnectState::Disconnected:    return "Disconnected";
            case EConnectState::Connecting:      return "Connecting";
            case EConnectState::Handshaking:     return "Handshaking";
            case EConnectState::Connected:       return "Connected";
            case EConnectState::FailedResolve:   return "FailedResolve";
            case EConnectState::FailedConnect:   return "FailedConnect";
            case EConnectState::FailedHandshake: return "FailedHandshake";
            case EConnectState::FailedProtocol:  return "FailedProtocol";
            case EConnectState::FailedInit:      return "FailedInit";
            default:                             return "Unknown";
        }
    }

    /**
     * @brief ENet-backed client connection and protocol endpoint.
     *
     * Owns the client-side ENet host/peer lifecycle and exposes
     * connect flow, receive pumping, and input sending for runtime gameplay.
     */
    class NetClient
    {
    public:
        NetClient();
        ~NetClient();

        /** @brief Non-copyable. */
        NetClient(const NetClient&) = delete;
        NetClient& operator=(const NetClient&) = delete;

        /** @brief Movable. */
        NetClient(NetClient&&) noexcept;
        NetClient& operator=(NetClient&&) noexcept;

        // =============================================================================================================
        // Connection Lifecycle
        // =============================================================================================================

        /**
         * @brief Initiates non-blocking connection to the server and starts handshake process.
         *
         * @param host Server hostname or IP address.
         * @param port Server port.
         * @param playerName Player name sent in the Hello payload.
         *
         * @note Use 'connectState()' to query connection progress.
         */
        void beginConnect(const std::string& host, uint16_t port, std::string_view playerName);

        /**
         * @brief Disconnects from the server and releases connection resources.
         */
        void disconnect();

        /**
         * @brief Aborts an in-progress beginConnect() attempt.
         *
         * Safe to call in any state. If called while Connecting or Handshaking,
         * releases resources and transitions to Disconnected.
         * Has no effect when already Connected or Disconnected.
         */
        void cancelConnect();

        // =============================================================================================================
        // Runtime I/O
        // =============================================================================================================

        /**
         * @brief Processes ENet events for this client host.
         *
         * @param timeoutMs Maximum wait time in milliseconds. Defaults to non-blocking.
         */
        void pump(uint16_t timeoutMs = 0);

        /**
         * @brief Sends one gameplay input packet to the server.
         *
         * @param input Input state payload.
         * @param clientTick Client tick written to packet header.
         */
        void sendInput(const MsgInput& input, uint32_t clientTick);

        /**
         * @brief Returns true when an active session is connected.
         */
        [[nodiscard]]
        bool isConnected() const { return state_ == EConnectState::Connected; }

        /**
         * @brief Returns the current connection state.
         */
        [[nodiscard]]
        EConnectState connectState() const { return state_; }

        /**
         * @brief Returns server-assigned client id from Welcome.
         *
         * Valid only after successful connect.
         */
        [[nodiscard]]
        uint32_t clientId() const { return clientId_; }

        /**
         * @brief Returns negotiated server tick rate from Welcome.
         *
         * Valid only after successful connect.
         */
        [[nodiscard]]
        uint16_t serverTickRate() const { return serverTickRate_; }

    private:
        // =============================================================================================================
        // Internals
        // =============================================================================================================

        /**
         * @brief Opaque ENet implementation detail.
         *
         * Keeps <enet/enet.h> out of this header. All ENet interaction
         * happens through the Impl pointer in NetClient.cpp.
         */
        struct Impl;
        std::unique_ptr<Impl> impl_;

        bool initialized_ = false;
        EConnectState state_ = EConnectState::Disconnected;

        uint32_t clientId_ = 0;         ///< Assigned by server during handshake
        uint16_t serverTickRate_ = 0;   ///< Received from server during handshake

        bool initializeENet();
        void shutdownENet();

        /** @brief Handles a validated Welcome payload from the dispatcher. */
        void handleWelcome(const uint8_t* payload, std::size_t payloadSize);

        /** @brief Handles remote disconnect without sending local disconnect request. */
        void handleRemoteDisconnect();

        /**
         * @brief Tears down ENet peer/host resources without touching logical state.
         *
         * Used by connection failure paths so state_ retains the failure reason.
         * Does NOT send a disconnect request to the remote peer.
         */
        void releaseResources();

        /** @brief Resets connection state shared by disconnect paths. */
        void resetState();
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETCLIENT_H
