#ifndef BOMBERMAN_NET_NETCLIENT_H
#define BOMBERMAN_NET_NETCLIENT_H

/**
 * @file NetClient.h
 * @brief Client-side multiplayer connection lifecycle and packet endpoint.
 */

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "NetCommon.h"

namespace bomberman::net
{
    // ----- Connection state -----

    /** @brief Client connection lifecycle state. */
    enum class EConnectState : uint8_t
    {
        Disconnected,    ///< Not connected and holding no transport resources.
        Connecting,      ///< ENet connect in progress, waiting for a CONNECT event.
        Handshaking,     ///< Transport connected and Hello sent, awaiting handshake completion.
        Connected,       ///< Full session ready after Welcome was received and processed successfully.
        Disconnecting,   ///< Graceful disconnect requested, awaiting ENet completion.
        FailedResolve,   ///< Host address could not be resolved.
        FailedConnect,   ///< Connect attempt timed out or transport failed before handshake.
        FailedHandshake, ///< Handshake timed out or was rejected for a non-protocol reason.
        FailedProtocol,  ///< Protocol version mismatch during handshake.
        FailedInit,      ///< ENet initialization or host creation failed.
    };

    /** @brief Returns true if the state represents a terminal failure. */
    [[nodiscard]] constexpr bool isFailedState(EConnectState state)
    {
        return state >= EConnectState::FailedResolve;
    }

    /** @brief Returns a human-readable label for a connection state. */
    constexpr std::string_view connectStateName(EConnectState state)
    {
        switch (state)
        {
            using enum EConnectState;

            case Disconnected:      return "Disconnected";
            case Connecting:        return "Connecting";
            case Handshaking:       return "Handshaking";
            case Connected:         return "Connected";
            case Disconnecting:     return "Disconnecting";
            case FailedResolve:     return "FailedResolve";
            case FailedConnect:     return "FailedConnect";
            case FailedHandshake:   return "FailedHandshake";
            case FailedProtocol:    return "FailedProtocol";
            case FailedInit:        return "FailedInit";
            default:                return "Unknown";
        }
    }

    /**
     * @brief ENet client connection and protocol endpoint.
     *
     * Owns the client-side ENet host and peer lifecycle, drives the async
     * connect and handshake flow, pumps incoming packets, caches the latest
     * session data, and sends runtime input batches during gameplay.
     *
     * A session is considered connected only after `Welcome` has been received successfully.
     */
    class NetClient
    {
    public:
        /** @brief Constructs an idle client with no active transport. */
        NetClient();

        /** @brief Disconnects locally if needed and releases ENet resources. */
        ~NetClient() noexcept;

        /** @brief Non-copyable. */
        NetClient(const NetClient&) = delete;
        NetClient& operator=(const NetClient&) = delete;

        /** @brief Non-movable. */
        NetClient(NetClient&&) = delete;
        NetClient& operator=(NetClient&&) = delete;

        // ----- Connection lifecycle -----

        /**
         * @brief Starts a non-blocking connect attempt.
         *
         * @param host Server hostname or IP address.
         * @param port Server port.
         * @param playerName Player name sent in the Hello payload.
         *
         * If transport setup succeeds, the client enters `Connecting`.
         * `Connected` state means the full handshake is completed.
         *
         * @note Use @ref NetClient::connectState to observe progress or failure.
         */
        void beginConnect(const std::string& host, uint16_t port, std::string_view playerName);

        /**
         * @brief Attempts a blocking graceful disconnect, then releases local transport resources.
         *
         * If called during `Connecting` or `Handshaking`, the in-progress
         * attempt is cancelled locally and this returns `false`.
         *
         * @warning This function may block for up to the disconnect timeout
         * while waiting for ENet disconnect completion.
         *
         * @return `true` if the remote disconnect handshake completed before local teardown.
         */
        bool disconnectBlocking();

        /**
         * @brief Starts a non-blocking graceful disconnect for an active session.
         *
         * If called during `Connecting` or `Handshaking`, the in-progress
         * attempt is cancelled locally and state returns to `Disconnected`.
         * Otherwise, connection state remains `Disconnecting` until
         * @ref NetClient::pumpNetwork observes completion or timeout.
         *
         * @warning Callers must continue pumping the client for completion or timeout handling.
         */
        void disconnectAsync();

        /**
         * @brief Cancels an in-progress connect or handshake attempt.
         *
         * Safe to call in any state. If the client is currently `Connecting` or
         * `Handshaking`, transport resources are released and state returns to
         * `Disconnected`. Otherwise, this is a no-op.
         */
        void cancelConnect();

        // ----- Runtime API -----

        /**
         * @brief Pumps ENet events for this client host.
         *
         * Evaluates connect and disconnect timeouts and dispatches any received protocol packets.
         *
         * @param timeoutMs Maximum wait in milliseconds. Defaults to non-blocking.
         */
        void pumpNetwork(uint16_t timeoutMs = 0);

        /**
         * @brief Records a button bitmask and queues a batched input packet.
         *
         * @param buttons Button bitmask (`kInput*` flags).
         * @return The assigned local input sequence, or `std::nullopt` if the
         * client is not currently connected.
         */
        [[nodiscard]]
        std::optional<uint32_t> sendInput(uint8_t buttons) const;

        /** @brief Flushes any queued outgoing ENet packets immediately. */
        void flushOutgoing() const;

        /** @brief Returns true when an active session is connected. */
        [[nodiscard]]
        bool isConnected() const { return state_ == EConnectState::Connected; }

        /** @brief Returns the current connection state. */
        [[nodiscard]]
        EConnectState connectState() const { return state_; }

        /** @brief Returns the last explicit server reject reason, if any. */
        [[nodiscard]]
        const std::optional<MsgReject::EReason>& lastRejectReason() const { return lastRejectReason_; }

        /** @brief Returns the server-assigned player id, or @ref NetClient::kInvalidPlayerId before connect. */
        [[nodiscard]]
        uint8_t playerId() const { return playerId_; }

        /** @brief Sentinel value indicating no player id has been assigned yet. */
        static constexpr uint8_t kInvalidPlayerId = 0xFF;

        /** @brief Returns negotiated server tick rate. Valid only after a successful handshake. */
        [[nodiscard]]
        uint16_t serverTickRate() const { return serverTickRate_; }

        /**
         * @brief Copies the newest cached snapshot for the current session.
         *
         * Returns `false` if no valid snapshot has been received since the most
         * recent connect.
         */
        [[nodiscard]]
        bool tryGetLatestSnapshot(MsgSnapshot& out) const;

        /** @brief Returns the server tick of the newest cached snapshot, or 0 if none is cached. */
        [[nodiscard]]
        uint32_t lastSnapshotTick() const;

        /**
         * @brief Copies the newest cached owner correction for the current session.
         *
         * Returns `false` if no valid correction has been received since the
         * most recent successful connect.
         */
        [[nodiscard]]
        bool tryGetLatestCorrection(MsgCorrection& out) const;

        /** @brief Returns the server tick of the newest cached correction, or 0 if none is cached. */
        [[nodiscard]]
        uint32_t lastCorrectionTick() const;

        /**
         * @brief Returns milliseconds since the last snapshot or correction.
         *
         * If gameplay traffic has not arrived yet for the current session, the
         * timer runs from handshake completion instead.
         */
        [[nodiscard]]
        uint32_t gameplaySilenceMs() const;

        /**
         * @brief Returns the cached map seed from the current session's `LevelInfo`.
         *
         * The cache is cleared on disconnect or reset.
         */
        [[nodiscard]]
        bool tryGetMapSeed(uint32_t& outSeed) const;

    private:
        // ----- Private state -----

        struct Impl;
        /**
         * @brief Opaque ENet implementation detail.
         *
         * Keeps `<enet/enet.h>` out of this header. All direct ENet interaction
         * lives in `NetClient.cpp`.
         *
         * The Struct holds both ENet transport resources and transient session state
         * that is cleared on disconnect or failure.
         * This includes the protocol packet caches, which are logically part of the session.
         */
        std::unique_ptr<Impl> impl_;

        bool initialized_ = false;
        EConnectState state_ = EConnectState::Disconnected;

        uint8_t playerId_ = kInvalidPlayerId; ///< Assigned by server during handshake [0, kMaxPlayers).
        uint16_t serverTickRate_ = 0; ///< Received from server during handshake.
        std::optional<MsgReject::EReason> lastRejectReason_; ///< Set only when a Reject payload is received.

        // ----- ENet lifecycle -----

        bool initializeENet();
        void shutdownENet();

        // ----- Disconnect helpers -----

        /** @brief Requests a graceful disconnect and drains until completion or timeout. */
        [[nodiscard]]
        bool drainGracefulDisconnect();

        /** @brief Starts a graceful disconnect if one is not already in progress. */
        void startGracefulDisconnect();

        /** @brief Destroys ENet peer and host resources without changing logical state. */
        void destroyTransport() const;

        /**
         * @brief Clears per-attempt and per-session data without changing connection state.
         * @param clearRejectReason When false, preserves the current reject reason for UI/reporting.
         */
        void clearSessionState(bool clearRejectReason = true);

        /**
         * @brief Tears down transport, clears stale session data, and enters a failure state.
         * @param failureState The failure state to enter, which must be a terminal `Failed*` state.
         * @param clearRejectReason When false, preserves the current reject reason for UI/reporting.
         */
        void failConnection(EConnectState failureState, bool clearRejectReason = true);

        /** @brief Resets per-session state shared by disconnect and failure paths. */
        void resetState();

        // ----- Protocol handlers -----

        void handleWelcome(const uint8_t* payload, std::size_t payloadSize);
        void handleReject(const uint8_t* payload, std::size_t payloadSize);
        void handleLevelInfo(const uint8_t* payload, std::size_t payloadSize);
        void handleSnapshot(const uint8_t* payload, std::size_t payloadSize) const;
        void handleCorrection(const uint8_t* payload, std::size_t payloadSize) const;

        // ----- pumpNetwork() helpers -----

        bool checkConnectTimeouts();
        bool checkDisconnectTimeout();

        /**
         * @brief Handles an ENet CONNECT event.
         * @return `true` if the caller should return immediately from @ref NetClient::pumpNetwork.
         */
        bool handleConnectEvent();

        /**
         * @brief Handles an ENet RECEIVE event.
         * @return `true` if the caller should return immediately from @ref NetClient::pumpNetwork.
         */
        bool handleReceiveEvent(const uint8_t* data, std::size_t dataLength, uint8_t channelID);

        /** @brief Handles a remote disconnect or transport timeout. */
        void handleDisconnectEvent();
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETCLIENT_H
