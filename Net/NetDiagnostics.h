#ifndef BOMBERMAN_NET_NETDIAGNOSTICS_H
#define BOMBERMAN_NET_NETDIAGNOSTICS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "NetCommon.h"
#include "NetDiagConfig.h"
#include "NetDiagShared.h"

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
    /** @brief Static server-side session configuration captured into diagnostics output. */
    struct ServerSessionConfig
    {
        uint16_t protocolVersion = 0;
        uint16_t tickRate = 0;
        uint32_t inputLeadTicks = 0;
        uint32_t snapshotIntervalTicks = 0;
        uint32_t brickSpawnRandomize = 0;
        uint32_t powerupsPerRound = 0;
        uint8_t maxPlayers = 0;
        bool powersEnabled = true;
    };

    /** @brief Sparse per-message aggregates retained for server tuning. */
    struct ServerKeyMessageAggregates
    {
        uint64_t snapshotSent = 0;
        uint64_t correctionSent = 0;
        uint64_t inputRecv = 0;
        uint64_t gameplayEventSent = 0;
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

        uint64_t inputPacketsReceived = 0;   ///< Total input packets received and parsed successfully.
        uint64_t inputPacketsFullyStale = 0; ///< Input packets that arrived after their newest sequence had already been consumed.
        uint64_t inputEntriesTooLate = 0;    ///< Individual input entries received after their sequence had already been processed.
        uint64_t inputEntriesTooLateDirect = 0; ///< Already-processed entries that were also the newest/direct command of their received batch.
        uint64_t inputEntriesTooLateBuffered = 0; ///< Already-processed entries that arrived only via buffered redundant batch history.
        uint64_t inputEntriesTooFarAhead = 0; ///< Input entries rejected for arriving too far ahead of the accepted receive window.

        uint64_t directDeadlineConsumes = 0; ///< Times the exact next input sequence was present at consume time with a direct/newest batch entry.
        uint64_t simulationGaps = 0; ///< Times a consume deadline was reached without the exact input, so previous buttons were reused.
        uint64_t bufferedDeadlineRecoveries = 0; ///< Times redundant batch history supplied the exact sequence by deadline without the direct/newest entry also being present.
        uint64_t bombsPlaced = 0; ///< Authoritative bomb placements accepted by the server simulation.
        uint64_t bricksDestroyed = 0; ///< Total bricks destroyed by authoritative explosions.
        uint64_t roundsEnded = 0; ///< Total rounds that reached a server-side end state.
        uint64_t roundsDrawn = 0; ///< Total rounds that ended with no surviving player.
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

        // ---- Latest per-player-id state sampling ----

        /** @brief Stores the latest sampled transport health values for a peer. */
        void samplePeerTransport(uint8_t peerId, uint32_t rttMs, uint32_t rttVarianceMs, uint32_t packetLossPermille, uint32_t queuedReliable, uint32_t queuedUnreliable);

        /** @brief Stores the latest input progression cursors for a peer. */
        void samplePeerInputContinuity(uint8_t peerId, uint32_t lastReceivedInputSeq, uint32_t lastProcessedInputSeq);

        // ---- Reporting ----

        /** @brief Advances per-session tick bookkeeping. */
        void advanceTick();

        /** @brief Captures static server session config for later text/JSON output. */
        void recordSessionConfig(ServerSessionConfig config);

        /** @brief Serializes the current diagnostics state as JSON. */
        [[nodiscard]]
        nlohmann::json toJson() const;

        /** @brief Writes a machine-readable JSON session report to disk. */
        bool writeJsonReport(std::string_view filePath) const;

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
        ServerSessionConfig config_{}; ///< Static server config captured for the current session.
        NetSessionSummary summary_{}; ///< Aggregate totals for the current session.
        ServerKeyMessageAggregates keyMessages_{}; ///< Sparse per-message aggregates useful for tuning.

        std::array<NetEvent, kRecentEventCapacity> recentEvents_{}; ///< Fixed-size recent event storage.
        std::size_t recentStart_ = 0;                               ///< Ring buffer head index.
        std::size_t recentCount_ = 0;                               ///< Number of valid entries in the ring.

        std::unordered_map<uint8_t, NetPeerTransportSample> latestPeerSamples_; ///< Latest transport sample by gameplay peer id.
        std::unordered_map<uint8_t, NetPeerContinuitySummary> peerContinuitySummaries_; ///< Latest input continuity state by gameplay peer id.
        std::unordered_map<std::string, RecentEventRepeatState> recentEventRepeatState_; ///< Recent-event dedupe bookkeeping by signature.
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETDIAGNOSTICS_H
