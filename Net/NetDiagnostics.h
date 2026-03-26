#ifndef BOMBERMAN_NET_NETDIAGNOSTICS_H
#define BOMBERMAN_NET_NETDIAGNOSTICS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "NetCommon.h"
#include "NetDiagConfig.h"

/**
 * @file NetDiagnostics.h
 * @brief Session-local multiplayer diagnostics recorder and report model.
 *
 * The recorder stays lightweight:
 * - recent noteworthy events live in a fixed-size ring buffer
 * - transport and input-continuity state keep only the latest sample per gameplay player id
 * - aggregate counters summarize one diagnostics session
 * - reporting writes a simple text snapshot suitable for manual review
 */

namespace bomberman::net
{
    // ----- Recent-event model -----

    /** @brief High-level kinds of recent events captured during a diagnostics session. */
    enum class NetEventType : uint8_t
    {
        Unknown,
        SessionBegin,
        SessionEnd,
        PeerLifecycle,
        PacketSent,
        PacketRecv,
        Simulation
    };

    /** @brief Peer lifecycle milestones currently emitted by the server networking flow. */
    enum class NetPeerLifecycleType : uint8_t
    {
        TransportConnected,
        PlayerAccepted,
        PeerRejected,
        PeerDisconnected,
        TransportDisconnectedBeforeHandshake
    };

    /** @brief Packet travel direction recorded in a recent packet event. */
    enum class NetPacketDirection : uint8_t
    {
        Outgoing,
        Incoming
    };

    /** @brief Diagnostics classification for one packet attempt or receive path outcome. */
    enum class NetPacketResult : uint8_t
    {
        Ok,
        Dropped,
        Rejected,
        Malformed
    };

    /** @brief Simulation/input-timeline events worth retaining in recent-event history. */
    enum class NetSimulationEventType : uint8_t
    {
        Gap,
        BufferedDeadlineRecovery,
        RoundEnded
    };

    /** @brief Discrete diagnostics event stored in the recent-event ring buffer. */
    struct NetEvent
    {
        NetEventType type = NetEventType::Unknown;
        uint64_t timestampMs = 0; ///< Monotonic timestamp. Zero means "stamp on record".

        NetPacketDirection packetDirection = NetPacketDirection::Outgoing; ///< Valid for packet events.
        NetPacketResult packetResult = NetPacketResult::Ok; ///< Valid for packet events.
        NetPeerLifecycleType lifecycleType = NetPeerLifecycleType::TransportConnected; ///< Valid for peer lifecycle events.
        NetSimulationEventType simulationType = NetSimulationEventType::Gap; ///< Valid for simulation events.

        uint8_t peerId = 0xFF; ///< Gameplay peer id when known, otherwise 0xFF.
        uint8_t channelId = 0xFF; ///< Raw ENet channel id for packet events.
        uint8_t msgType = 0; ///< Raw @ref EMsgType value for packet events.

        uint32_t seq = 0; ///< Input sequence or other event-specific sequence value.
        uint32_t detailA = 0; ///< Event-specific numeric detail.
        uint32_t detailB = 0; ///< Event-specific numeric detail.

        std::string note; ///< Optional short human-readable note for reports.
    };

    // ----- Latest per-player-id state -----

    /** @brief Latest sampled transport health values for a single gameplay player id. */
    struct NetPeerTransportSample
    {
        uint8_t peerId = 0xFF;
        uint64_t timestampMs = 0;
        uint32_t rttMs = 0;
        uint32_t rttVarianceMs = 0;
        uint32_t packetLossPermille = 0;
        uint32_t queuedReliable = 0;
        uint32_t queuedUnreliable = 0;
    };

    /** @brief Aggregate input-continuity facts for one authoritative gameplay player id. */
    struct NetPeerContinuitySummary
    {
        uint8_t peerId = 0xFF;
        uint64_t timestampMs = 0;
        uint64_t directDeadlineConsumes = 0;
        uint64_t simulationGaps = 0;
        uint64_t bufferedDeadlineRecoveries = 0;
        uint32_t lastReceivedInputSeq = 0;
        uint32_t lastProcessedInputSeq = 0;
    };

    /**
     * @brief Aggregate counters and timing collected across one diagnostics session.
     */
    struct NetSessionSummary
    {
        bool enabled = true; ///< True when diagnostics were enabled for the session.
        bool active = false; ///< True while the session is still open.

        uint64_t beginTimestampMs = 0;
        uint64_t endTimestampMs = 0;
        uint64_t durationMs = 0;

        uint64_t tickCount = 0;

        uint64_t recentEventsRecorded = 0; ///< Number of events written to the recent-event ring.
        uint64_t recentEventsEvicted = 0; ///< Number of older recent events evicted from the fixed-size ring.

        uint64_t packetsSent = 0; ///< Outgoing packet attempts recorded, including failed queue attempts.
        uint64_t packetsRecv = 0; ///< Incoming packet attempts recorded, including rejected or malformed packets.
        uint64_t packetBytesSent = 0; ///< Outgoing bytes counted across all recorded send attempts.
        uint64_t packetBytesRecv = 0; ///< Incoming bytes counted across all recorded receive attempts.
        uint64_t packetsSentFailed = 0; ///< Failed outgoing attempts, subset of @ref packetsSent.
        uint64_t packetsRecvFailed = 0; ///< Failed incoming attempts, subset of @ref packetsRecv.
        uint64_t malformedPacketsRecv = 0; ///< Incoming packets rejected before a typed payload could be dispatched.
        uint64_t malformedPacketBytesRecv = 0; ///< Bytes carried by malformed incoming packets.

        uint64_t inputPacketsReceived = 0;   ///< Total input packets received and parsed successfully.
        uint64_t inputPacketsFullyStale = 0; ///< Input packets whose newest sequence was already consumed on arrival.
        uint64_t inputEntriesTooLate = 0;    ///< Input entries that arrived after their sequence had already been processed.
        uint64_t inputEntriesTooLateDirect = 0; ///< Late entries that were the newest/direct command of their received batch.
        uint64_t inputEntriesTooLateBuffered = 0; ///< Late entries that arrived only as buffered redundant batch history.
        uint64_t inputEntriesTooFarAhead = 0; ///< Input entries rejected for being too far ahead of the accepted receive window.

        uint64_t directDeadlineConsumes = 0; ///< Times the exact next input sequence was present at consume time with a direct/newest batch entry.
        uint64_t simulationGaps = 0; ///< Times a consume deadline was reached without the exact input, so previous buttons were reused.
        uint64_t bufferedDeadlineRecoveries = 0; ///< Times redundant batch history supplied the exact sequence by deadline without the direct/newest entry also being present.
        uint64_t bombsPlaced = 0; ///< Authoritative bomb placements accepted by the server simulation.
        uint64_t bricksDestroyed = 0; ///< Total bricks destroyed by authoritative explosions.
        uint64_t roundsEnded = 0; ///< Total rounds that reached a server-side end state.
        uint64_t roundsDrawn = 0; ///< Total rounds that ended with no surviving player.
        uint64_t helloRejectsGameInProgress = 0; ///< Hello packets rejected because the current round could not be bootstrapped cleanly.
        std::array<uint64_t, kMaxPlayers> roundWinsByPlayerId{}; ///< Round wins keyed by authoritative player id.
    };

    /**
     * @brief Session-local recorder for recent multiplayer diagnostics and aggregate counters.
     *
     * Recording is manual and call-site driven. The class does not inspect ENet
     * state or gameplay state on its own; it stores what the owning network flow
     * explicitly reports to it.
     */
    class NetDiagnostics
    {
    public:
        // ---- Lifecycle ----

        /** @brief Creates a diagnostics recorder with the given default enabled state. */
        NetDiagnostics(bool enabled = true);

        /**
         * @brief Starts a new diagnostics session and clears previous session state.
         * @param ownerTag Short human-readable label for the owner of this recorder.
         * @param enabled Whether diagnostics recording should be enabled for this session.
         */
        void beginSession(std::string_view ownerTag, bool enabled = true);

        /** @brief Ends the current diagnostics session and finalizes duration fields. */
        void endSession();

        // ---- Recent-event extension seam ----

        /**
         * @brief Records a fully populated recent event.
         *
         * This is the lowest-level extension point for future diagnostics work.
         * If `event.timestampMs` is zero, the recorder stamps it with the
         * current monotonic time.
         */
        void recordEvent(const NetEvent& event);

        // ---- Packet accounting ----

        /** @brief Records one outgoing packet attempt and updates packet summary totals. */
        void recordPacketSent(EMsgType type, uint8_t peerId, uint8_t channelId, std::size_t bytes, NetPacketResult result = NetPacketResult::Ok);

        /** @brief Records one incoming packet attempt and updates packet summary totals. */
        void recordPacketRecv(EMsgType type, uint8_t peerId, uint8_t channelId, std::size_t bytes, NetPacketResult result = NetPacketResult::Ok);

        /** @brief Records an incoming packet that failed before typed dispatch, such as a malformed header. */
        void recordMalformedPacketRecv(uint8_t peerId, uint8_t channelId, std::size_t bytes, std::string_view note = {});

        /** @brief Records a structured peer lifecycle event. */
        void recordPeerLifecycle(NetPeerLifecycleType type, uint8_t peerId, uint32_t transportPeerId, std::string_view note = {});

        // ---- Input stream and simulation continuity ----

        /** @brief Records one input packet that was received and parsed successfully. */
        void recordInputPacketReceived();

        /** @brief Records a fully stale input packet whose newest sequence was already consumed. */
        void recordInputPacketFullyStale(uint32_t count = 1);

        /** @brief Records input entries that arrived after their sequence had already been processed. */
        void recordInputEntriesTooLate(uint32_t count);

        /** @brief Records late entries that were the newest/direct command of their received batch. */
        void recordInputEntriesTooLateDirect(uint32_t count);

        /** @brief Records late entries that arrived only as redundant buffered history. */
        void recordInputEntriesTooLateBuffered(uint32_t count);

        /** @brief Records input entries rejected for being too far ahead of the accepted receive window. */
        void recordInputEntriesTooFarAhead(uint32_t count);

        /** @brief Records a consume deadline miss that required reusing the previous buttons for a gameplay peer. */
        void recordSimulationGap(uint8_t peerId, uint32_t inputSeq, uint8_t heldButtons, uint32_t serverTick);

        /** @brief Records a direct consume where the exact seq was present by deadline with a direct/newest batch entry. */
        void recordDirectDeadlineConsume(uint8_t peerId, uint32_t inputSeq);

        /** @brief Records a buffered deadline recovery where redundant history supplied the exact seq before consume time. */
        void recordBufferedDeadlineRecovery(uint8_t peerId, uint32_t inputSeq, uint32_t serverTick);

        /** @brief Records one authoritative bomb placement accepted by the server simulation. */
        void recordBombPlaced();
        /** @brief Records bricks destroyed by one authoritative explosion resolution. */
        void recordBricksDestroyed(uint32_t count);
        /** @brief Records one authoritative round-end outcome. */
        void recordRoundEnded(std::optional<uint8_t> winnerPlayerId, bool endedInDraw, uint32_t serverTick);
        /** @brief Records a reject reason that should surface in diagnostics summaries. */
        void recordRejectReason(MsgReject::EReason reason);

        // ---- Latest per-player-id state sampling ----

        /** @brief Stores the latest sampled transport health values for a peer. */
        void samplePeerTransport(uint8_t peerId, uint32_t rttMs, uint32_t rttVarianceMs, uint32_t packetLossPermille, uint32_t queuedReliable, uint32_t queuedUnreliable);

        /** @brief Stores the latest input progression cursors for a peer. */
        void samplePeerInputContinuity(uint8_t peerId, uint32_t lastReceivedInputSeq, uint32_t lastProcessedInputSeq);

        // ---- Reporting ----

        /** @brief Advances per-session tick bookkeeping. */
        void advanceTick();

        /** @brief Writes a temporary human-readable text session report to disk. */
        bool writeSessionReport(std::string_view filePath) const;

    private:
        // ---- Internal helpers ----

        static uint64_t nowMs();
        static uint64_t recentEventDedupeCooldownMs(const NetEvent& event);
        static std::string makeRecentEventSignature(const NetEvent& event);
        static bool isAlwaysEmitEvent(const NetEvent& event);

        /** @brief Returns whether a packet attempt should be stored in recent-event history. */
        static bool shouldEmitPacketEvent(NetPacketResult result);

        /** @brief Convenience helper for recorder-owned events with no structured payload. */
        void recordEvent(NetEventType type, std::string_view note = {});

        /** @brief Global per-message packet aggregates. */
        struct PacketAggregate
        {
            uint64_t outgoingOk = 0;
            uint64_t outgoingFail = 0;
            uint64_t outgoingBytes = 0;

            uint64_t incomingOk = 0;
            uint64_t incomingFail = 0;
            uint64_t incomingBytes = 0;
        };

        struct RecentEventRepeatState
        {
            uint64_t lastEmittedTimestampMs = 0;
        };

        /** @brief Resets all per-session state before a new session begins. */
        void resetForNewSession(std::string_view ownerTag, bool enabled);

        /** @brief Applies recent-event dedupe policy before writing to the ring buffer. */
        void recordRecentEvent(NetEvent event);
        /** @brief Inserts an event into the fixed-size recent-event ring buffer. */
        void pushRecentEvent(NetEvent event);

        // ---- State ----

        bool enabled_ = true;        ///< Early-out gate for all recording calls.
        bool sessionActive_ = false; ///< True while a session is active.

        std::string ownerTag_;       ///< Human-readable owner label for the current session.
        NetSessionSummary summary_{}; ///< Aggregate totals for the current session.

        std::array<NetEvent, kRecentEventCapacity> recentEvents_{}; ///< Fixed-size recent event storage.
        std::size_t recentStart_ = 0;                               ///< Ring buffer head index.
        std::size_t recentCount_ = 0;                               ///< Number of valid entries in the ring.

        std::array<PacketAggregate, 256> packetAggregates_{};       ///< Per-message packet aggregates by raw msg type.
        std::unordered_map<uint8_t, NetPeerTransportSample> latestPeerSamples_; ///< Latest transport sample by gameplay peer id.
        std::unordered_map<uint8_t, NetPeerContinuitySummary> peerContinuitySummaries_; ///< Latest input continuity state by gameplay peer id.
        std::unordered_map<std::string, RecentEventRepeatState> recentEventRepeatState_; ///< Recent-event dedupe bookkeeping by signature.
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETDIAGNOSTICS_H
