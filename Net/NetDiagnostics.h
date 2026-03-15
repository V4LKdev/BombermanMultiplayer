#ifndef BOMBERMAN_NET_NETDIAGNOSTICS_H
#define BOMBERMAN_NET_NETDIAGNOSTICS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "NetCommon.h"
#include "NetDiagConfig.h"

/**
 * @brief Diagnostics recorder for high-level network events, peer transport health samples, and aggregate counters.
 *
 * It does 4 things:
 *   1. Captures discrete events in a recent-event ring buffer.
 *   2. Stores the latest sampled transport health values for each peer.
 *   3. Maintains layered aggregate counters for packet, input-stream, and simulation facts.
 *   4. Dumps a human-readable session report to disk on demand.
 */


namespace bomberman::net
{
    /** @brief High-level kinds of diagnostics events captured during a network session. */
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

    enum class NetPeerLifecycleType : uint8_t
    {
        TransportConnected,
        PlayerAccepted,
        PeerRejected,
        PeerDisconnected,
        TransportDisconnectedBeforeHandshake
    };

    enum class NetPacketDirection : uint8_t
    {
        Outgoing,
        Incoming
    };

    enum class NetPacketResult : uint8_t
    {
        Ok,
        Dropped,
        Rejected,
        Malformed
    };

    enum class NetSimulationEventType : uint8_t
    {
        Gap,
        BufferedRecovery
    };

    /** @brief Discrete diagnostics event stored in the recent-event ring buffer. */
    struct NetEvent
    {
        NetEventType type = NetEventType::Unknown;
        uint64_t timestampMs = 0;

        NetPacketDirection packetDirection = NetPacketDirection::Outgoing;
        NetPacketResult packetResult = NetPacketResult::Ok;
        NetPeerLifecycleType lifecycleType = NetPeerLifecycleType::TransportConnected;
        NetSimulationEventType simulationType = NetSimulationEventType::Gap;

        uint8_t peerId = 0xFF;
        uint8_t channelId = 0xFF;
        uint8_t msgType = 0;

        uint32_t seq = 0;
        // Generic value fields that can be used for different purposes depending on the event type.
        uint32_t valueA = 0;
        uint32_t valueB = 0;

        std::string note;
    };

    /** @brief Latest sampled transport health values for a single peer. */
    struct NetPeerSample
    {
        uint8_t peerId = 0xFF;
        uint64_t timestampMs = 0;
        uint32_t rttMs = 0;
        uint32_t rttVarianceMs = 0;
        uint32_t packetLossPermille = 0;
        uint32_t queuedReliable = 0;
        uint32_t queuedUnreliable = 0;
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

        // Attempt counters: incremented for every send/recv record call, including failed results.
        uint64_t packetsSent = 0;
        uint64_t packetsRecv = 0;
        uint64_t packetBytesSent = 0;
        uint64_t packetBytesRecv = 0;
        // Failed attempts by direction (subset of packetsSent/packetsRecv).
        uint64_t packetsSentFailed = 0;
        uint64_t packetsRecvFailed = 0;
        uint64_t malformedPacketsRecv = 0; ///< Incoming packets rejected before a typed payload could be dispatched.
        uint64_t malformedPacketBytesRecv = 0; ///< Bytes carried by malformed incoming packets.

        uint64_t inputBatchesReceived = 0; ///< Total input batches received and parsed successfully.
        uint64_t inputBatchesFullyStale = 0; ///< Batches whose newest entry was already consumed on arrival.
        uint64_t inputEntriesReceivedTotal = 0; ///< Total input command entries seen inside incoming input batches.
        uint64_t inputEntriesAccepted = 0;      ///< Input command entries accepted and stored for future consumption.
        uint64_t inputEntriesRedundant = 0;     ///< Already-consumed input entries that were harmlessly redundant on arrival.
        uint64_t inputEntriesRejectedOutsideWindow = 0; ///< Input entries rejected for being beyond the accepted receive window.

        uint64_t simulationGaps = 0; ///< Times the simulation had to fall back because exact input was unavailable.
        uint64_t bufferedInputRecoveries = 0; ///< Times buffered input avoided a simulation gap.
    };


    /**
     * @brief Reusable diagnostics recorder shared by client- and server-side networking code.
     *
     * The class owns recent events, latest peer samples, packet aggregates, and high-level session counters.
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

        // ---- Event and sample recording ----

        /**
         * @brief Records a fully populated event structure.
         *
         * If event.timestampMs is zero, the implementation will stamp it with the
         * current monotonic time.
         */
        void recordEvent(const NetEvent& event);

        /** @brief Convenience overload for simple events with an attached note string. */
        void recordEvent(NetEventType type, std::string_view note = {});

        /** @brief Records an outgoing packet attempt for a gameplay peer and updates summary counters. */
        void recordPacketSent(EMsgType type, uint8_t peerId, uint8_t channelId, std::size_t bytes, NetPacketResult result = NetPacketResult::Ok);

        /** @brief Records an incoming packet attempt for a gameplay peer and updates summary counters. */
        void recordPacketRecv(EMsgType type, uint8_t peerId, uint8_t channelId, std::size_t bytes, NetPacketResult result = NetPacketResult::Ok);

        /** @brief Records an incoming packet that failed before typed dispatch, such as a malformed header. */
        void recordMalformedPacketRecv(uint8_t peerId, uint8_t channelId, std::size_t bytes, std::string_view note = {});

        /** @brief Records a structured peer lifecycle event. */
        void recordPeerLifecycle(NetPeerLifecycleType type, uint8_t peerId, uint32_t transportPeerId, std::string_view note = {});

        /** @brief Records an input batch that was received and parsed successfully. */
        void recordInputBatchReceived(uint32_t entryCount);

        /** @brief Records a fully stale input batch whose newest sequence was already consumed. */
        void recordInputBatchFullyStale(uint32_t count = 1);

        /** @brief Records how many input entries were accepted for buffering. */
        void recordInputEntriesAccepted(uint32_t count);

        /** @brief Records redundant already-consumed input entries seen in a resent batch. */
        void recordInputEntriesRedundant(uint32_t count);

        /** @brief Records input entries rejected for being beyond the accepted receive window. */
        void recordInputEntriesRejectedOutsideWindow(uint32_t count);

        /** @brief Records a simulation gap that required a hold fallback for a gameplay peer. */
        void recordSimulationGap(uint8_t peerId, uint32_t inputSeq, uint8_t heldButtons, uint32_t serverTick);

        /** @brief Records a buffered-input recovery that avoided a simulation gap. */
        void recordBufferedInputRecovery(uint8_t peerId, uint32_t inputSeq, uint32_t serverTick);

        // ---- Peer transport health sampling ----

        /** @brief Stores the latest sampled transport health values for a peer. */
        void samplePeer(uint8_t peerId, uint32_t rttMs, uint32_t rttVarianceMs, uint32_t packetLossPermille, uint32_t queuedReliable, uint32_t queuedUnreliable);

        // ---- Reporting ----

        /** @brief Advances per-session tick bookkeeping. */
        void advanceTick();

        /** @brief Writes a temporary human-readable text session report to disk. */
        bool writeSessionReport(std::string_view filePath) const;

    private:
        // ---- Internal helpers ----

        /** @brief Returns whether a packet attempt should be stored in recent-event history. */
        static bool shouldEmitPacketEvent(NetPacketResult result);

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

        static uint64_t nowMs();
        static uint64_t recentEventDedupeCooldownMs(const NetEvent& event);
        static std::string makeRecentEventSignature(const NetEvent& event);
        static bool isAlwaysEmitEvent(const NetEvent& event);

        /** @brief Resets all per-session state before a new session begins. */
        void resetForNewSession(std::string_view ownerTag, bool enabled);

        /** @brief Inserts an event into the recent-event ring buffer. */
        void pushRecentEvent(NetEvent event);
        void recordRecentEvent(NetEvent event);

        // ---- State ----

        bool enabled_ = true;        ///< Early-out gate for all recording calls.
        bool sessionActive_ = false; ///< True while a session is active.

        std::string ownerTag_;       ///< Human-readable owner label for the current session.
        NetSessionSummary summary_{}; ///< Aggregate totals for the current session.

        std::array<NetEvent, kRecentEventCapacity> recentEvents_{}; ///< Fixed-size recent event storage.
        std::size_t recentStart_ = 0;                               ///< Ring buffer head index.
        std::size_t recentCount_ = 0;                               ///< Number of valid entries in the ring.

        std::unordered_map<uint8_t, NetPeerSample> latestPeerSamples_;
        std::unordered_map<std::string, RecentEventRepeatState> recentEventRepeatState_;
        std::array<PacketAggregate, 256> packetAggregates_{};       ///< Per-message packet aggregates by raw msg type.
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETDIAGNOSTICS_H
