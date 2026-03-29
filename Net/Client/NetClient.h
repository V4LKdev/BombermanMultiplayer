/**
 * @file NetClient.h
 * @brief Client-side multiplayer connection lifecycle and packet endpoint.
 * @ingroup net_client
 */

#ifndef BOMBERMAN_NET_NETCLIENT_H
#define BOMBERMAN_NET_NETCLIENT_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "Net/NetCommon.h"

namespace bomberman::net
{
    class ClientDiagnostics;

    // ----- Connection state -----

    /** @brief Client connection lifecycle state. */
    enum class EConnectState : uint8_t
    {
        Disconnected,    ///< Not connected and holding no transport resources.
        Connecting,      ///< ENet connect in progress, waiting for a CONNECT event.
        Handshaking,     ///< Transport connected and Hello sent, awaiting Welcome or Reject.
        Connected,       ///< Session accepted and ready for lobby flow or the next round-start message.
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
     * A session is considered connected once `Welcome` has been received successfully.
     */
    class NetClient
    {
    public:
        /** @brief Live multiplayer/network HUD state updated during gameplay. */
        struct ClientLiveStats
        {
            uint32_t rttMs = 0;
            uint32_t rttVarianceMs = 0;
            uint32_t lossPermille = 0;
            uint32_t lastSnapshotTick = 0;
            uint32_t lastCorrectionTick = 0;
            uint32_t snapshotAgeMs = 0;
            uint32_t gameplaySilenceMs = 0;

            bool predictionActive = false;
            bool recoveryActive = false;
            uint32_t correctionCount = 0;
            uint32_t mismatchCount = 0;
            uint32_t lastCorrectionDeltaQ = 0;
            uint32_t maxPendingInputDepth = 0;
        };

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
         * `Connected` state means the server accepted the session and the
         * client may enter lobby flow immediately.
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
        std::optional<uint32_t> sendInput(uint8_t buttons);

        /**
         * @brief Sends an authoritative lobby ready-state request for the local accepted seat.
         *
         * The server remains the source of truth; successful sends are reflected
         * back through later `LobbyState` updates.
         *
         * @return `true` if the request was queued successfully.
         */
        [[nodiscard]]
        bool sendLobbyReady(bool ready);

        /**
         * @brief Acknowledges that the gameplay scene for `matchId` has been constructed locally.
         *
         * This is a low-frequency reliable control message used only during the
         * authoritative round-start handoff.
         *
         * @return `true` if the request was queued successfully.
         */
        [[nodiscard]]
        bool sendMatchLoaded(uint32_t matchId);

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

        /** @brief Configures client diagnostics behavior for future connect sessions. */
        void setDiagnosticsConfig(bool enabled, bool predictionEnabled, bool remoteSmoothingEnabled);

        /** @brief Returns the client diagnostics recorder. */
        [[nodiscard]]
        ClientDiagnostics& clientDiagnostics();
        /** @brief Returns the client diagnostics recorder. */
        [[nodiscard]]
        const ClientDiagnostics& clientDiagnostics() const;

        /** @brief Updates the live transport portion of the multiplayer HUD state. */
        void updateLiveTransportStats(uint32_t rttMs,
                                      uint32_t rttVarianceMs,
                                      uint32_t lossPermille,
                                      uint32_t lastSnapshotTick,
                                      uint32_t lastCorrectionTick,
                                      uint32_t snapshotAgeMs,
                                      uint32_t gameplaySilenceMs);

        /** @brief Updates the live prediction portion of the multiplayer HUD state. */
        void updateLivePredictionStats(bool predictionActive,
                                       bool recoveryActive,
                                       uint32_t correctionCount,
                                       uint32_t mismatchCount,
                                       uint32_t lastCorrectionDeltaQ,
                                       uint32_t maxPendingInputDepth);

        /** @brief Returns the current live multiplayer HUD state. */
        [[nodiscard]]
        const ClientLiveStats& liveStats() const;

        /** @brief Returns the server-assigned player id, or @ref NetClient::kInvalidPlayerId before connect. */
        [[nodiscard]]
        uint8_t playerId() const { return playerId_; }

        /** @brief Sentinel value indicating no player id has been assigned yet. */
        static constexpr uint8_t kInvalidPlayerId = 0xFF;

        /** @brief Returns negotiated server tick rate. Valid only after a successful handshake. */
        [[nodiscard]]
        uint16_t serverTickRate() const { return serverTickRate_; }

        /**
         * @brief Copies the newest cached lobby state for the current session.
         *
         * Returns `false` until at least one valid authoritative lobby-state
         * message has been received for the current session.
         */
        [[nodiscard]]
        bool tryGetLatestLobbyState(MsgLobbyState& out) const;

        /**
         * @brief Returns milliseconds since the last authoritative lobby-state update.
         *
         * If no lobby-state update has arrived yet for the current session, the
         * timer runs from handshake completion instead.
         */
        [[nodiscard]]
        uint32_t lobbySilenceMs() const;

        /**
         * @brief Consumes the newest unhandled round-start `LevelInfo` for the current session.
         *
         * Returns `false` until a newer `LevelInfo` arrives. Once returned
         * successfully, the same cached message is not returned again until a
         * later `LevelInfo` replaces it.
         */
        [[nodiscard]]
        bool consumePendingLevelInfo(MsgLevelInfo& out);

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
         * @brief One reliable gameplay event dequeued in original receive order.
         *
         * Gameplay-reliable packets share one ENet channel, so preserving this
         * order matters across message types rather than only within each type.
         */
        struct GameplayEvent
        {
            enum class EType : uint8_t
            {
                BombPlaced,
                ExplosionResolved
            };

            [[nodiscard]]
            static GameplayEvent fromBombPlaced(const MsgBombPlaced& bombPlaced)
            {
                GameplayEvent event{};
                event.type = EType::BombPlaced;
                event.bombPlaced = bombPlaced;
                return event;
            }

            [[nodiscard]]
            static GameplayEvent fromExplosionResolved(const MsgExplosionResolved& explosionResolved)
            {
                GameplayEvent event{};
                event.type = EType::ExplosionResolved;
                event.explosionResolved = explosionResolved;
                return event;
            }

            EType type = EType::BombPlaced;
            MsgBombPlaced bombPlaced{};
            MsgExplosionResolved explosionResolved{};
        };

        /**
         * @brief Pops the oldest pending reliable gameplay event for the current session.
         *
         * Returns `false` when no pending event is queued or the reliable
         * gameplay-event stream has been invalidated for the current match.
         */
        [[nodiscard]]
        bool tryDequeueGameplayEvent(GameplayEvent& out);

        /**
         * @brief Returns true once the reliable gameplay-event stream can no longer be trusted.
         *
         * The current client policy is fail-fast: if reliable gameplay events
         * overflow locally, the active match should be abandoned instead of
         * continuing with potentially divergent world state.
         */
        [[nodiscard]]
        bool hasBrokenGameplayEventStream() const;

        /**
         * @brief Returns milliseconds since the last snapshot or correction.
         *
         * If gameplay traffic has not arrived yet for the current session, the
         * timer runs from handshake completion instead.
         */
        [[nodiscard]]
        uint32_t gameplaySilenceMs() const;

        /** @brief Copies the newest cached match-start timing edge for the current session. */
        [[nodiscard]]
        bool tryGetLatestMatchStart(MsgMatchStart& out) const;

        /** @brief Returns true after the server has explicitly started `matchId` for this session. */
        [[nodiscard]]
        bool hasMatchStarted(uint32_t matchId) const;

        /** @brief Returns true after the server has explicitly cancelled `matchId` back to the lobby. */
        [[nodiscard]]
        bool isMatchCancelled(uint32_t matchId) const;

        /** @brief Copies the newest cached authoritative match result for the current session. */
        [[nodiscard]]
        bool tryGetLatestMatchResult(MsgMatchResult& out) const;

        /**
         * @brief Returns the cached map seed from the current session's latest `LevelInfo`.
         *
         * The cache is cleared on disconnect or reset. In lobby-only states no
         * LevelInfo may have been received yet.
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
         * The struct holds both ENet transport resources and transient session state
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

        void finalizeDiagnosticsSession(EConnectState finalState);

        // ----- Disconnect helpers -----

        /** @brief Requests a graceful disconnect and drains until completion or timeout. */
        [[nodiscard]]
        bool drainGracefulDisconnect();

        /** @brief Starts a graceful disconnect if one is not already in progress. */
        void startGracefulDisconnect();

        /** @brief Destroys ENet peer and host resources without changing logical state. */
        void destroyTransport();

        /** @brief Clears the local per-match input sequence/history used for gameplay batching. */
        void resetLocalInputStream();

        /**
         * @brief Resets local client state that must restart from a fresh baseline on each new round start.
         *
         * This currently includes local input sequencing plus gameplay receive timers.
         */
        void resetLocalMatchBootstrapState();

        /** @brief Clears the current round-start/bootstrap session and local gameplay input state. */
        void resetCurrentMatchSession();

        /**
         * @brief Clears per-attempt and per-session data without changing connection state.
         * @param clearRejectReason When false, preserves the current reject reason for UI/reporting.
         */
        void resetSessionState(bool clearRejectReason = true);

        /**
         * @brief Tears down transport, clears stale session data, and enters a failure state.
         * @param failureState The failure state to enter, which must be a terminal `Failed*` state.
         * @param clearRejectReason When false, preserves the current reject reason for UI/reporting.
         */
        void failConnection(EConnectState failureState, bool clearRejectReason = true);

        /** @brief Finalizes the current session as disconnected and releases transport resources. */
        void transitionToDisconnected();

        /** @brief Resets per-session state shared by disconnect and failure paths. */
        void resetState();

        // ----- Protocol handlers -----

        void handleWelcome(const uint8_t* payload, std::size_t payloadSize);
        void handleReject(const uint8_t* payload, std::size_t payloadSize);
        void handleLevelInfo(const uint8_t* payload, std::size_t payloadSize);
        void handleLobbyState(const uint8_t* payload, std::size_t payloadSize);
        void handleMatchStart(const uint8_t* payload, std::size_t payloadSize);
        void handleMatchCancelled(const uint8_t* payload, std::size_t payloadSize);
        void handleMatchResult(const uint8_t* payload, std::size_t payloadSize);
        void handleSnapshot(const uint8_t* payload, std::size_t payloadSize);
        void handleCorrection(const uint8_t* payload, std::size_t payloadSize);
        void handleBombPlaced(const uint8_t* payload, std::size_t payloadSize);
        void handleExplosionResolved(const uint8_t* payload, std::size_t payloadSize);
        void enqueueGameplayEvent(const GameplayEvent& event);

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
