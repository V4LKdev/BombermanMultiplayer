#ifndef BOMBERMAN_NET_CLIENTDIAGNOSTICS_H
#define BOMBERMAN_NET_CLIENTDIAGNOSTICS_H

/**
 * @file ClientDiagnostics.h
 * @brief Client-side multiplayer diagnostics data model and recorder.
 * @ingroup net_client
 *
 * @details
 * This helper records one client multiplayer session from handshake through
 * disconnect. It keeps lightweight aggregates for reports, plus a bounded
 * recent-event buffer for debugging packet flow, authority intake, and
 * prediction behavior.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "Client/ClientPrediction.h"
#include "NetCommon.h"
#include "NetDiagConfig.h"
#include "NetDiagShared.h"

namespace bomberman::net
{
    enum class EConnectState : uint8_t;

    /** @brief Client session metadata captured progressively as a multiplayer connection runs. */
    struct ClientSessionConfig
    {
        uint16_t protocolVersion = 0;
        uint16_t clientTickRate = 0;
        bool predictionEnabled = false;
        bool remoteSmoothingEnabled = false;

        std::optional<uint8_t> assignedPlayerId{};
        std::optional<uint16_t> serverTickRate{};
    };

    /** @brief Sparse per-message aggregates retained for client diagnostics and tuning. */
    struct ClientKeyMessageAggregates
    {
        uint64_t snapshotRecv = 0;
        uint64_t correctionRecv = 0;
        uint64_t inputSent = 0;
        uint64_t gameplayEventRecv = 0;
    };

    /** @brief Aggregate client-side diagnostics counters for one multiplayer session. */
    struct ClientSessionSummary
    {
        bool enabled = true;
        bool active = false;

        uint64_t beginTimestampMs = 0;
        uint64_t endTimestampMs = 0;
        uint64_t durationMs = 0;

        EConnectState finalState;
        uint64_t connectedDurationMs = 0;
        uint64_t handshakeDurationMs = 0;

        uint32_t lastRttMs = 0;
        uint32_t lastRttVarianceMs = 0;
        uint32_t lastLossPermille = 0;
        uint64_t transportSamples = 0;
        uint64_t sampledRttMsTotal = 0;
        uint64_t sampledRttVarianceMsTotal = 0;
        uint64_t sampledLossPermilleTotal = 0;

        PredictionStats prediction{};
        bool predictionReachedActive = false;
        bool predictionEverRecovered = false;

        uint64_t packetsSent = 0;
        uint64_t packetsRecv = 0;
        uint64_t packetBytesSent = 0;
        uint64_t packetBytesRecv = 0;
        uint64_t packetsSentFailed = 0;
        uint64_t packetsRecvFailed = 0;
        uint64_t malformedPackets = 0;
        uint64_t maxInputSendGapMs = 0;

        uint64_t maxLobbySilenceMs = 0;
        uint64_t maxGameplaySilenceMs = 0;
        uint64_t staleSnapshotsIgnored = 0;
        uint64_t staleCorrectionsIgnored = 0;
        uint64_t brokenGameplayEventStreamIncidents = 0;
        uint64_t maxPendingGameplayEventDepth = 0;

        uint64_t recentEventsRecorded = 0;
        uint64_t recentEventsEvicted = 0;
    };

    /** @brief Client-side multiplayer diagnostics recorder owned by @ref NetClient. */
    class ClientDiagnostics
    {
    public:
        /** @brief Constructs a recorder with diagnostics enabled or disabled by default. */
        ClientDiagnostics(bool enabled = true);

        /** @brief Starts a fresh diagnostics session and captures static client/session configuration. */
        void beginSession(std::string_view ownerTag,
                          bool enabled,
                          bool predictionEnabled,
                          bool remoteSmoothingEnabled);
        /** @brief Finalizes the active diagnostics session and stamps its end time. */
        void endSession();

        /** @brief Records handshake success details from the authoritative Welcome message. */
        void recordWelcome(uint8_t assignedPlayerId,
                           uint16_t serverTickRate,
                           uint64_t handshakeDurationMs,
                           uint32_t transportPeerId);
        /** @brief Records the final connection state and how long the session stayed connected. */
        void recordFinalState(EConnectState finalState, uint64_t connectedDurationMs);

        /** @brief Records one outgoing packet attempt, including failures if the send was rejected. */
        void recordPacketSent(EMsgType type, uint8_t channelId, std::size_t bytes,
                              NetPacketResult result = NetPacketResult::Ok);
        /** @brief Records one incoming packet observation, including malformed or rejected packets. */
        void recordPacketRecv(EMsgType type, uint8_t channelId, std::size_t bytes,
                              NetPacketResult result = NetPacketResult::Ok);
        /** @brief Records one malformed inbound packet incident. */
        void recordMalformedPacket(uint8_t channelId, std::size_t bytes, std::string_view note = {});

        /** @brief Records one peer/session lifecycle event such as connect, accept, reject, or disconnect. */
        void recordPeerLifecycle(NetPeerLifecycleType type,
                                 std::optional<uint8_t> playerId,
                                 uint32_t transportPeerId,
                                 std::string_view note = {});

        /** @brief Samples live transport quality from ENet state. */
        void sampleTransport(uint32_t rttMs, uint32_t rttVarianceMs, uint32_t lossPermille);
        /** @brief Tracks the longest gap between local input sends. */
        void sampleInputSendGap(uint32_t gapMs);
        /** @brief Tracks lobby-message silence while the client is waiting in lobby flow. */
        void sampleLobbySilence(uint32_t lobbySilenceMs);
        /** @brief Tracks gameplay-message silence while the client is in-match. */
        void sampleGameplaySilence(uint32_t gameplaySilenceMs);
        /** @brief Records one stale snapshot rejected by the client. */
        void recordStaleSnapshotIgnored(uint32_t serverTick);
        /** @brief Records one stale correction rejected by the client. */
        void recordStaleCorrectionIgnored(uint32_t serverTick, uint32_t lastProcessedInputSeq);
        /** @brief Records a broken authoritative gameplay-event stream for the active match. */
        void recordBrokenGameplayEventStream(uint32_t matchId);
        /** @brief Tracks the deepest queued reliable gameplay-event backlog seen so far. */
        void samplePendingGameplayEventDepth(std::size_t depth);

        /** @brief Merges the latest prediction helper stats into the session summary. */
        void feedPredictionStats(const PredictionStats& stats, bool reachedActive, bool everRecovered);
        /** @brief Records one recent high-level diagnostics event in the bounded event buffer. */
        void recordEvent(const NetEvent& event);

        /** @brief Returns the current aggregate session summary. */
        [[nodiscard]]
        const ClientSessionSummary& summary() const { return summary_; }
        /** @brief Returns the static configuration captured for the current or last session. */
        [[nodiscard]]
        const ClientSessionConfig& config() const { return config_; }

        /** @brief Serializes the current diagnostics session to JSON. */
        [[nodiscard]]
        nlohmann::json toJson() const;
        /** @brief Writes the current diagnostics session as a JSON report file. */
        bool writeJsonReport(std::string_view filePath) const;

    private:
        static uint64_t nowMs();
        static uint64_t recentEventDedupeCooldownMs(const NetEvent& event);
        static std::string makeRecentEventSignature(const NetEvent& event);
        static bool isAlwaysEmitEvent(const NetEvent& event);
        static bool shouldEmitPacketEvent(NetPacketResult result);

        void recordEvent(NetEventType type, std::string_view note = {});
        void resetForNewSession(std::string_view ownerTag, bool enabled);
        void recordRecentEvent(NetEvent event);
        void pushRecentEvent(NetEvent event);

        bool enabled_ = true;
        bool sessionActive_ = false;

        std::string ownerTag_;
        ClientSessionConfig config_{};
        ClientSessionSummary summary_{};
        ClientKeyMessageAggregates keyMessages_{};

        std::array<NetEvent, kRecentEventCapacity> recentEvents_{};
        std::size_t recentStart_ = 0;
        std::size_t recentCount_ = 0;
        std::unordered_map<std::string, RecentEventRepeatState> recentEventRepeatState_;
    };
} // namespace bomberman::net

#endif // BOMBERMAN_NET_CLIENTDIAGNOSTICS_H
