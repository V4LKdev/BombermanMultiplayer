#ifndef BOMBERMAN_NET_CLIENTDIAGNOSTICS_H
#define BOMBERMAN_NET_CLIENTDIAGNOSTICS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "ClientPrediction.h"
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
        ClientDiagnostics(bool enabled = true);

        void beginSession(std::string_view ownerTag,
                          bool enabled,
                          bool predictionEnabled,
                          bool remoteSmoothingEnabled);
        void endSession();

        void recordWelcome(uint8_t assignedPlayerId,
                           uint16_t serverTickRate,
                           uint64_t handshakeDurationMs,
                           uint32_t transportPeerId);
        void recordFinalState(EConnectState finalState, uint64_t connectedDurationMs);

        void recordPacketSent(EMsgType type, uint8_t channelId, std::size_t bytes,
                              NetPacketResult result = NetPacketResult::Ok);
        void recordPacketRecv(EMsgType type, uint8_t channelId, std::size_t bytes,
                              NetPacketResult result = NetPacketResult::Ok);
        void recordMalformedPacket(uint8_t channelId, std::size_t bytes, std::string_view note = {});

        void recordPeerLifecycle(NetPeerLifecycleType type,
                                 std::optional<uint8_t> playerId,
                                 uint32_t transportPeerId,
                                 std::string_view note = {});

        void sampleTransport(uint32_t rttMs, uint32_t rttVarianceMs, uint32_t lossPermille);
        void sampleInputSendGap(uint32_t gapMs);
        void sampleLobbySilence(uint32_t lobbySilenceMs);
        void sampleGameplaySilence(uint32_t gameplaySilenceMs);
        void recordStaleSnapshotIgnored(uint32_t serverTick);
        void recordStaleCorrectionIgnored(uint32_t serverTick, uint32_t lastProcessedInputSeq);
        void recordBrokenGameplayEventStream(uint32_t matchId);
        void samplePendingGameplayEventDepth(std::size_t depth);

        void feedPredictionStats(const PredictionStats& stats, bool reachedActive, bool everRecovered);
        void recordEvent(const NetEvent& event);

        [[nodiscard]]
        const ClientSessionSummary& summary() const { return summary_; }
        [[nodiscard]]
        const ClientSessionConfig& config() const { return config_; }

        [[nodiscard]]
        nlohmann::json toJson() const;
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
