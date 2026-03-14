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
 *   3. Maintains aggregate counters for packet attempts and input anomalies by type.
 *   4. Dumps a human-readable session report to disk on demand.
 */


namespace bomberman::net
{
    /** @brief High-level kinds of diagnostics events captured during a network session. */
    enum class NetEventType : uint8_t
    {
        SessionBegin,
        SessionEnd,
        PacketSent,
        PacketRecv,
        InputAnomaly,
        Note
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

    /** @brief Categories of input anomalies tracked by diagnostics. */
    enum class NetInputAnomalyType : uint8_t
    {
        OutOfOrder,
        Duplicate,
        Gap,
        Count
    };

    /** @brief Discrete diagnostics event stored in the recent-event ring buffer. */
    struct NetEvent
    {
        NetEventType type = NetEventType::Note;
        uint64_t timestampMs = 0;

        NetPacketDirection packetDirection = NetPacketDirection::Outgoing;
        NetPacketResult packetResult = NetPacketResult::Ok;
        NetInputAnomalyType anomalyType = NetInputAnomalyType::OutOfOrder;

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

        uint64_t eventsRecorded = 0; ///< Number of events that were recorded in the recent-event ring.
        uint64_t eventsDropped = 0; ///< Number of events that were evicted from the recent-event ring.

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

        uint64_t staleInputBatches = 0; ///< Fully stale input batches whose newest command was already consumed on arrival.

        uint64_t inputEntriesReceivedTotal = 0; ///< Total input command entries seen inside incoming input batches.
        uint64_t inputEntriesAccepted = 0;      ///< Input command entries accepted and stored for future consumption.
        uint64_t inputEntriesRedundant = 0;     ///< Already-consumed input entries resent intentionally in newer batches.
        uint64_t inputAnomalyCount = 0;

        std::array<uint64_t, static_cast<std::size_t>(NetInputAnomalyType::Count)> anomaliesByType{}; ///< Breakdown of input anomalies by type.
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

        /** @brief Convenience overload for simple note-style events. */
        void recordEvent(NetEventType type, std::string_view note = {});

        /** @brief Records an outgoing packet attempt for a gameplay peer and updates summary counters. */
        void recordPacketSent(EMsgType type, uint8_t peerId, uint8_t channelId, std::size_t bytes, NetPacketResult result = NetPacketResult::Ok);

        /** @brief Records an incoming packet attempt for a gameplay peer and updates summary counters. */
        void recordPacketRecv(EMsgType type, uint8_t peerId, uint8_t channelId, std::size_t bytes, NetPacketResult result = NetPacketResult::Ok);

        /** @brief Records an incoming packet that failed before typed dispatch, such as a malformed header. */
        void recordMalformedPacketRecv(uint8_t peerId, uint8_t channelId, std::size_t bytes, std::string_view note = {});

        /** @brief Records an input anomaly for a gameplay peer and increments per-type anomaly counters. */
        void recordInputAnomaly(NetInputAnomalyType type, uint8_t peerId, uint32_t inputSeq, uint8_t buttons, std::string_view note = {});

        /** @brief Records how many input entries were seen in an incoming batch. */
        void recordInputEntriesReceived(uint32_t count);

        /** @brief Records how many input entries were accepted for buffering. */
        void recordInputEntriesAccepted(uint32_t count);

        /** @brief Records redundant already-consumed input entries seen in a resent batch. */
        void recordInputEntriesRedundant(uint32_t count);

        /** @brief Records a fully stale input batch whose newest sequence was already consumed. */
        void recordStaleInputBatch(uint8_t peerId, uint32_t highestSeq, uint8_t count);

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

        /** @brief Returns whether an input anomaly should be stored in recent-event history. */
        static bool shouldEmitInputAnomalyEvent(NetInputAnomalyType type);

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

        static uint64_t nowMs();

        /** @brief Resets all per-session state before a new session begins. */
        void resetForNewSession(std::string_view ownerTag, bool enabled);

        /** @brief Inserts an event into the recent-event ring buffer. */
        void pushRecentEvent(NetEvent event);

        // ---- State ----

        bool enabled_ = true;        ///< Early-out gate for all recording calls.
        bool sessionActive_ = false; ///< True while a session is active.

        std::string ownerTag_;       ///< Human-readable owner label for the current session.
        NetSessionSummary summary_{}; ///< Aggregate totals for the current session.

        std::array<NetEvent, kRecentEventCapacity> recentEvents_{}; ///< Fixed-size recent event storage.
        std::size_t recentStart_ = 0;                               ///< Ring buffer head index.
        std::size_t recentCount_ = 0;                               ///< Number of valid entries in the ring.

        std::unordered_map<uint8_t, NetPeerSample> latestPeerSamples_;
        std::array<PacketAggregate, 256> packetAggregates_{};       ///< Per-message packet aggregates by raw msg type.
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETDIAGNOSTICS_H
