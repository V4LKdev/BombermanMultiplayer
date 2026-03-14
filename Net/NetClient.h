#ifndef BOMBERMAN_NET_NETCLIENT_H
#define BOMBERMAN_NET_NETCLIENT_H

#include <memory>
#include <optional>
#include <string_view>
#include <string>

#include "NetCommon.h"

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Client Connection State ===================================================================================
    // =================================================================================================================

    /**
     * @brief Represents the current state of the client connection lifecycle.
     */
    enum class EConnectState : uint8_t
    {
        Disconnected,       ///< Not connected, no resources held.
        Connecting,         ///< ENet connect in progress, waiting for CONNECT event.
        Handshaking,        ///< Transport connected, Hello sent.
        Connected,          ///< Fully handshake-complete, session ready.
        FailedResolve,      ///< Could not resolve host address.
        FailedConnect,      ///< ENet connect attempt timed out.
        FailedHandshake,    ///< Handshake timed out or was rejected.
        FailedProtocol,     ///< Protocol version mismatch.
        FailedInit,         ///< ENet or host creation failure.
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
        // ===== Connection Lifecycle and Management ===================================================================
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

        /** @brief Disconnects from the server and releases connection resources. */
        void disconnect();

        /**
         * @brief Aborts an in-progress beginConnect() attempt.
         *
         * Safe to call in any state. Releases resources and transitions to Disconnected
         * if Connecting or Handshaking; no-op otherwise.
         */
        void cancelConnect();

        // =============================================================================================================
        // ===== Runtime Gameplay Interface ============================================================================
        // =============================================================================================================

        /**
         * @brief Processes ENet events for this client host.
         *
         * @param timeoutMs Maximum wait time in milliseconds. Defaults to non-blocking.
         */
        void pump(uint16_t timeoutMs = 0);

        /**
         * @brief Records a button bitmask for the current tick and sends a batched input packet.
         *
         * @param buttons Button bitmask (kInput* flags).
         */
        void sendInput(uint8_t buttons);

        /** @brief Returns true when an active session is connected. */
        [[nodiscard]]
        bool isConnected() const { return state_ == EConnectState::Connected; }

        /** @brief Returns the current connection state. */
        [[nodiscard]]
        EConnectState connectState() const { return state_; }

        /** @brief Returns the last explicit server reject reason, if any. */
        [[nodiscard]]
        const std::optional<MsgReject::EReason>& lastRejectReason() const { return lastRejectReason_; }

        /** @brief Returns server-assigned player id, or kInvalidPlayerId before connect. */
        [[nodiscard]]
        uint8_t playerId() const { return playerId_; }

        /** @brief Sentinel value indicating no player id has been assigned yet. */
        static constexpr uint8_t kInvalidPlayerId = 0xFF;

        /** @brief Returns negotiated server tick rate. Valid only after successful connect. */
        [[nodiscard]]
        uint16_t serverTickRate() const { return serverTickRate_; }

        /** @brief Returns the most recently received snapshot from the server, if any. */
        [[nodiscard]]
        bool tryGetLatestSnapshot(MsgSnapshot& out) const;

        /** @brief Returns the server tick of the last received snapshot. */
        [[nodiscard]]
        uint32_t lastSnapshotTick() const;

        /** @brief Populates `outSeed` and returns true if a LevelInfo has been received. */
        [[nodiscard]]
        bool tryGetMapSeed(uint32_t& outSeed) const;

    private:
        // =============================================================================================================
        // ===== Internal State and Helpers ============================================================================
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

        uint8_t playerId_ = kInvalidPlayerId; ///< Assigned by server during handshake [0, kMaxPlayers).
        uint16_t serverTickRate_ = 0;   ///< Received from server during handshake.
        std::optional<MsgReject::EReason> lastRejectReason_; ///< Set only when a Reject payload is received.

        bool initializeENet();
        void shutdownENet();

        // ---- Protocol handlers ----
        void handleWelcome(const uint8_t* payload, std::size_t payloadSize);
        void handleReject(const uint8_t* payload, std::size_t payloadSize);
        void handleLevelInfo(const uint8_t* payload, std::size_t payloadSize);
        void handleSnapshot(const uint8_t* payload, std::size_t payloadSize);

        // ---- pump() sub-helpers ----

        bool checkConnectTimeouts();
        /** @brief Handles an ENet CONNECT event (sends Hello, transitions to Handshaking). Returns true if pump should return early. */
        bool handleEnetConnect();
        /** @brief Handles an ENet RECEIVE event. Returns true if pump should return early (failure state). */
        bool handleEnetReceive(const uint8_t* data, std::size_t dataLength, uint8_t channelID);
        /** @brief Handles remote disconnect (ENet DISCONNECT event or server-initiated). */
        void handleEnetDisconnect();

        // ---- Resource teardown ----

        /** @brief Sends a polite disconnect request and drains events until server ack or iteration cap. */
        void drainGracefulDisconnect();

        /** @brief Destroys ENet peer/host transport resources without modifying logical state. */
        void destroyTransport();

        /** @brief Resets connection state shared by disconnect paths. */
        void resetState();
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETCLIENT_H
