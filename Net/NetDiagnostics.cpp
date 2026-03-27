/**
 * @file NetDiagnostics.cpp
 * @brief Implementation of the lightweight multiplayer diagnostics recorder.
 */

#include "NetDiagnostics.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace bomberman::net
{
    namespace
    {
        // =============================================================================================================
        // ===== Local string helpers ==================================================================================
        // =============================================================================================================

        /** @brief Returns a stable printable label for one recent-event kind. */
        constexpr const char* toString(NetEventType type)
        {
            switch (type)
            {
                case NetEventType::Unknown:       return "Unknown";
                case NetEventType::SessionBegin:  return "SessionBegin";
                case NetEventType::SessionEnd:    return "SessionEnd";
                case NetEventType::PeerLifecycle: return "PeerLifecycle";
                case NetEventType::PacketSent:    return "PacketSent";
                case NetEventType::PacketRecv:    return "PacketRecv";
                case NetEventType::Simulation:    return "Simulation";
                case NetEventType::Flow:          return "Flow";
                default:                          return "Unknown";
            }
        }

        /** @brief Returns a stable printable label for one peer lifecycle event. */
        constexpr const char* toString(NetPeerLifecycleType type)
        {
            switch (type)
            {
                case NetPeerLifecycleType::TransportConnected:                return "TransportConnected";
                case NetPeerLifecycleType::PlayerAccepted:                    return "PlayerAccepted";
                case NetPeerLifecycleType::PeerRejected:                      return "PeerRejected";
                case NetPeerLifecycleType::PeerDisconnected:                  return "PeerDisconnected";
                case NetPeerLifecycleType::TransportDisconnectedBeforeHandshake:
                    return "TransportDisconnectedBeforeHandshake";
                default:
                    return "Unknown";
            }
        }

        /** @brief Returns a stable printable label for one packet outcome. */
        constexpr const char* toString(NetPacketResult result)
        {
            switch (result)
            {
                case NetPacketResult::Ok:        return "Ok";
                case NetPacketResult::Dropped:   return "Dropped";
                case NetPacketResult::Rejected:  return "Rejected";
                case NetPacketResult::Malformed: return "Malformed";
                default:                         return "Unknown";
            }
        }

        /** @brief Returns a stable printable label for one simulation continuity event. */
        constexpr const char* toString(NetSimulationEventType type)
        {
            switch (type)
            {
                case NetSimulationEventType::Gap:                      return "Gap";
                case NetSimulationEventType::BufferedDeadlineRecovery: return "BufferedDeadlineRecovery";
                case NetSimulationEventType::RoundEnded:               return "RoundEnded";
                default:                                               return "Unknown";
            }
        }

        void incrementKeyMessageSent(ServerKeyMessageAggregates& keyMessages, const EMsgType type)
        {
            switch (type)
            {
                case EMsgType::Snapshot:
                    keyMessages.snapshotSent++;
                    break;
                case EMsgType::Correction:
                    keyMessages.correctionSent++;
                    break;
                case EMsgType::BombPlaced:
                case EMsgType::ExplosionResolved:
                    keyMessages.gameplayEventSent++;
                    break;
                default:
                    break;
            }
        }

        void incrementKeyMessageRecv(ServerKeyMessageAggregates& keyMessages, const EMsgType type)
        {
            if (type == EMsgType::Input)
                keyMessages.inputRecv++;
        }
    }

    // =================================================================================================================
    // ===== Lifecycle =================================================================================================
    // =================================================================================================================

    NetDiagnostics::NetDiagnostics(bool enabled)
        : enabled_(enabled)
    {
        summary_.enabled = enabled;
    }

    void NetDiagnostics::beginSession(std::string_view ownerTag, bool enabled)
    {
        resetForNewSession(ownerTag, enabled);

        if (!enabled_)
            return;

        recordEvent(NetEventType::SessionBegin, "session started");
    }

    void NetDiagnostics::endSession()
    {
        if (!sessionActive_)
            return;

        if (enabled_)
            recordEvent(NetEventType::SessionEnd, "session ended");

        summary_.active = false;
        summary_.endTimestampMs = nowMs();
        summary_.durationMs = summary_.endTimestampMs - summary_.beginTimestampMs;
        sessionActive_ = false;
    }

    // =================================================================================================================
    // ===== Event and sample recording ================================================================================
    // =================================================================================================================

    void NetDiagnostics::recordEvent(NetEventType type, std::string_view note)
    {
        NetEvent event{};
        event.type = type;
        event.note = note;
        recordEvent(event);
    }

    void NetDiagnostics::recordEvent(const NetEvent& event)
    {
        if (!enabled_ || !sessionActive_)
            return;

        NetEvent stamped = event;
        if (stamped.timestampMs == 0)
            stamped.timestampMs = nowMs();

        recordRecentEvent(std::move(stamped));
    }

    void NetDiagnostics::recordPacketSent(const EMsgType type,
                                          const uint8_t peerId,
                                          const uint8_t channelId,
                                          const std::size_t bytes,
                                          const NetPacketResult result)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.packetsSent++;
        summary_.packetBytesSent += bytes;

        if (result == NetPacketResult::Ok)
        {
            incrementKeyMessageSent(keyMessages_, type);
        }
        else
        {
            summary_.packetsSentFailed++;
        }

        if (!shouldEmitPacketEvent(result))
            return;

        NetEvent event{};
        event.type = NetEventType::PacketSent;
        event.timestampMs = nowMs();
        event.packetDirection = NetPacketDirection::Outgoing;
        event.packetResult = result;
        event.peerId = peerId;
        event.channelId = channelId;
        event.msgType = static_cast<uint8_t>(type);
        event.detailA = static_cast<uint32_t>(bytes);
        recordRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordPacketRecv(const EMsgType type,
                                          const uint8_t peerId,
                                          const uint8_t channelId,
                                          const std::size_t bytes,
                                          const NetPacketResult result)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.packetsRecv++;
        summary_.packetBytesRecv += bytes;

        if (result == NetPacketResult::Ok)
        {
            incrementKeyMessageRecv(keyMessages_, type);
        }
        else
        {
            summary_.packetsRecvFailed++;
        }

        if (!shouldEmitPacketEvent(result))
            return;

        NetEvent event{};
        event.type = NetEventType::PacketRecv;
        event.timestampMs = nowMs();
        event.packetDirection = NetPacketDirection::Incoming;
        event.packetResult = result;
        event.peerId = peerId;
        event.channelId = channelId;
        event.msgType = static_cast<uint8_t>(type);
        event.detailA = static_cast<uint32_t>(bytes);
        recordRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordMalformedPacketRecv(const uint8_t peerId,
                                                   const uint8_t channelId,
                                                   const std::size_t bytes,
                                                   std::string_view note)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.packetsRecv++;
        summary_.packetBytesRecv += bytes;
        summary_.packetsRecvFailed++;
        summary_.malformedPacketsRecv++;

        NetEvent event{};
        event.type = NetEventType::PacketRecv;
        event.timestampMs = nowMs();
        event.packetDirection = NetPacketDirection::Incoming;
        event.packetResult = NetPacketResult::Malformed;
        event.peerId = peerId;
        event.channelId = channelId;
        event.detailA = static_cast<uint32_t>(bytes);
        event.note = note;
        recordRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordPeerLifecycle(const NetPeerLifecycleType type,
                                             const uint8_t peerId,
                                             const uint32_t transportPeerId,
                                             std::string_view note)
    {
        if (!enabled_ || !sessionActive_)
            return;

        NetEvent event{};
        event.type = NetEventType::PeerLifecycle;
        event.timestampMs = nowMs();
        event.lifecycleType = type;
        event.peerId = peerId;
        event.detailA = transportPeerId;
        event.note = note;
        recordRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordInputPacketReceived()
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.inputPacketsReceived++;
    }

    void NetDiagnostics::recordInputPacketFullyStale(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.inputPacketsFullyStale += count;
    }

    void NetDiagnostics::recordInputEntriesTooLate(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.inputEntriesTooLate += count;
    }

    void NetDiagnostics::recordInputEntriesTooLateDirect(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.inputEntriesTooLateDirect += count;
    }

    void NetDiagnostics::recordInputEntriesTooLateBuffered(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.inputEntriesTooLateBuffered += count;
    }

    void NetDiagnostics::recordInputEntriesTooFarAhead(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.inputEntriesTooFarAhead += count;
    }

    void NetDiagnostics::recordSimulationGap(const uint8_t peerId,
                                             const uint32_t inputSeq,
                                             const uint8_t heldButtons,
                                             const uint32_t serverTick)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.simulationGaps++;
        auto& peerSummary = peerContinuitySummaries_[peerId];
        peerSummary.peerId = peerId;
        peerSummary.timestampMs = nowMs();
        peerSummary.simulationGaps++;
        peerSummary.lastProcessedInputSeq = inputSeq;

        NetEvent event{};
        event.type = NetEventType::Simulation;
        event.timestampMs = nowMs();
        event.simulationType = NetSimulationEventType::Gap;
        event.peerId = peerId;
        event.seq = inputSeq;
        event.detailA = heldButtons;
        event.detailB = serverTick;
        recordRecentEvent(std::move(event));
    }

    // =================================================================================================================
    // ===== Simulation continuity events ==============================================================================
    // =================================================================================================================

    void NetDiagnostics::recordDirectDeadlineConsume(const uint8_t peerId, const uint32_t inputSeq)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.directDeadlineConsumes++;
        auto& peerSummary = peerContinuitySummaries_[peerId];
        peerSummary.peerId = peerId;
        peerSummary.timestampMs = nowMs();
        peerSummary.directDeadlineConsumes++;
        peerSummary.lastProcessedInputSeq = inputSeq;
    }

    void NetDiagnostics::recordBufferedDeadlineRecovery(const uint8_t peerId,
                                                        const uint32_t inputSeq,
                                                        const uint32_t serverTick)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.bufferedDeadlineRecoveries++;
        auto& peerSummary = peerContinuitySummaries_[peerId];
        peerSummary.peerId = peerId;
        peerSummary.timestampMs = nowMs();
        peerSummary.bufferedDeadlineRecoveries++;
        peerSummary.lastProcessedInputSeq = inputSeq;

        NetEvent event{};
        event.type = NetEventType::Simulation;
        event.timestampMs = nowMs();
        event.simulationType = NetSimulationEventType::BufferedDeadlineRecovery;
        event.peerId = peerId;
        event.seq = inputSeq;
        event.detailB = serverTick;
        recordRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordBombPlaced()
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.bombsPlaced++;
    }

    void NetDiagnostics::recordBricksDestroyed(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.bricksDestroyed += count;
    }

    void NetDiagnostics::recordRoundEnded(const std::optional<uint8_t> winnerPlayerId,
                                          const bool endedInDraw,
                                          const uint32_t serverTick)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.roundsEnded++;
        if (endedInDraw)
        {
            summary_.roundsDrawn++;
        }
        else if (winnerPlayerId.has_value() && winnerPlayerId.value() < kMaxPlayers)
        {
            summary_.roundWinsByPlayerId[winnerPlayerId.value()]++;
        }

        NetEvent event{};
        event.type = NetEventType::Simulation;
        event.timestampMs = nowMs();
        event.simulationType = NetSimulationEventType::RoundEnded;
        event.peerId = winnerPlayerId.value_or(0xFF);
        event.detailA = endedInDraw ? 1u : 0u;
        event.detailB = serverTick;
        recordRecentEvent(std::move(event));
    }

    void NetDiagnostics::samplePeerTransport(const uint8_t peerId,
                                             const uint32_t rttMs,
                                             const uint32_t rttVarianceMs,
                                             const uint32_t packetLossPermille,
                                             const uint32_t queuedReliable,
                                             const uint32_t queuedUnreliable)
    {
        if (!enabled_ || !sessionActive_)
            return;

        NetPeerTransportSample sample{};
        sample.peerId = peerId;
        sample.timestampMs = nowMs();
        sample.rttMs = rttMs;
        sample.rttVarianceMs = rttVarianceMs;
        sample.packetLossPermille = packetLossPermille;
        sample.queuedReliable = queuedReliable;
        sample.queuedUnreliable = queuedUnreliable;

        latestPeerSamples_[peerId] = sample;
    }

    void NetDiagnostics::samplePeerInputContinuity(const uint8_t peerId,
                                                   const uint32_t lastReceivedInputSeq,
                                                   const uint32_t lastProcessedInputSeq)
    {
        if (!enabled_ || !sessionActive_)
            return;

        auto& peerSummary = peerContinuitySummaries_[peerId];
        peerSummary.peerId = peerId;
        peerSummary.timestampMs = nowMs();
        peerSummary.lastReceivedInputSeq = lastReceivedInputSeq;
        peerSummary.lastProcessedInputSeq = lastProcessedInputSeq;
    }

    void NetDiagnostics::recordServerFlowState(const std::string_view stateName,
                                               const bool idle,
                                               const uint32_t serverTick,
                                               const uint32_t matchId)
    {
        if (!enabled_ || !sessionActive_)
            return;

        const bool stateChanged = serverFlowState_ != stateName;
        const bool idleChanged = serverIdle_ != idle;
        if (!stateChanged && !idleChanged)
            return;

        serverFlowState_.assign(stateName.begin(), stateName.end());
        serverIdle_ = idle;

        if (stateChanged)
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.detailA = matchId;
            event.detailB = serverTick;
            event.note = std::string("server flow: ") + serverFlowState_;
            recordRecentEvent(std::move(event));
        }

        if (idleChanged)
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.detailA = matchId;
            event.detailB = serverTick;
            event.note = idle ? "server idle" : "server active";
            recordRecentEvent(std::move(event));
        }
    }

    void NetDiagnostics::recordSessionConfig(const ServerSessionConfig config)
    {
        config_ = config;
    }

    // =================================================================================================================
    // ===== Session maintenance and reporting =========================================================================
    // =================================================================================================================

    void NetDiagnostics::advanceTick()
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.tickCount++;
    }

    nlohmann::json NetDiagnostics::toJson() const
    {
        const uint64_t reportEndMs = summary_.active ? nowMs() : summary_.endTimestampMs;
        const uint64_t reportDurationMs =
            (reportEndMs >= summary_.beginTimestampMs) ? (reportEndMs - summary_.beginTimestampMs) : 0;

        nlohmann::json roundWins = nlohmann::json::array();
        for (const uint64_t wins : summary_.roundWinsByPlayerId)
            roundWins.push_back(wins);

        nlohmann::json continuity = nlohmann::json::array();
        std::vector<uint8_t> continuityIds;
        continuityIds.reserve(peerContinuitySummaries_.size());
        for (const auto& [peerId, summary] : peerContinuitySummaries_)
        {
            (void)summary;
            continuityIds.push_back(peerId);
        }
        std::sort(continuityIds.begin(), continuityIds.end());
        for (const uint8_t peerId : continuityIds)
        {
            const auto& peerSummary = peerContinuitySummaries_.at(peerId);
            const uint32_t pendingInputs =
                (peerSummary.lastReceivedInputSeq > peerSummary.lastProcessedInputSeq)
                    ? (peerSummary.lastReceivedInputSeq - peerSummary.lastProcessedInputSeq)
                    : 0u;

            continuity.push_back({
                {"player_id", peerId},
                {"ts_ms", peerSummary.timestampMs},
                {"direct_deadline_consumes", peerSummary.directDeadlineConsumes},
                {"gaps", peerSummary.simulationGaps},
                {"buffered_deadline_recoveries", peerSummary.bufferedDeadlineRecoveries},
                {"last_recv_seq", peerSummary.lastReceivedInputSeq},
                {"last_processed_seq", peerSummary.lastProcessedInputSeq},
                {"pending_inputs", pendingInputs}
            });
        }

        nlohmann::json transportSamples = nlohmann::json::array();
        std::vector<uint8_t> transportIds;
        transportIds.reserve(latestPeerSamples_.size());
        for (const auto& [peerId, sample] : latestPeerSamples_)
        {
            (void)sample;
            transportIds.push_back(peerId);
        }
        std::sort(transportIds.begin(), transportIds.end());
        for (const uint8_t peerId : transportIds)
        {
            const auto& sample = latestPeerSamples_.at(peerId);
            transportSamples.push_back({
                {"player_id", peerId},
                {"ts_ms", sample.timestampMs},
                {"rtt_ms", sample.rttMs},
                {"rtt_var_ms", sample.rttVarianceMs},
                {"loss_permille", sample.packetLossPermille},
                {"q_rel", sample.queuedReliable},
                {"q_unrel", sample.queuedUnreliable}
            });
        }

        nlohmann::json recentEvents = nlohmann::json::array();
        for (std::size_t i = 0; i < recentCount_; ++i)
        {
            const auto idx = (recentStart_ + i) % kRecentEventCapacity;
            const auto& event = recentEvents_[idx];
            recentEvents.push_back({
                {"ts_ms", event.timestampMs},
                {"type", static_cast<uint8_t>(event.type)},
                {"packet_direction", static_cast<uint8_t>(event.packetDirection)},
                {"packet_result", static_cast<uint8_t>(event.packetResult)},
                {"lifecycle_type", static_cast<uint8_t>(event.lifecycleType)},
                {"simulation_type", static_cast<uint8_t>(event.simulationType)},
                {"player_id", event.peerId == 0xFF ? nlohmann::json(nullptr) : nlohmann::json(event.peerId)},
                {"channel_id", event.channelId == 0xFF ? nlohmann::json(nullptr) : nlohmann::json(event.channelId)},
                {"msg_type", event.msgType == 0 ? nlohmann::json(nullptr) : nlohmann::json(event.msgType)},
                {"seq", event.seq},
                {"detail_a", event.detailA},
                {"detail_b", event.detailB},
                {"note", event.note}
            });
        }

        return {
            {"report", "net_diagnostics_report"},
            {"report_version", kDiagnosticsReportVersion},
            {"session_owner", ownerTag_},
            {"config", {
                {"protocol_version", config_.protocolVersion},
                {"tick_rate", config_.tickRate},
                {"input_lead_ticks", config_.inputLeadTicks},
                {"snapshot_interval_ticks", config_.snapshotIntervalTicks},
                {"brick_spawn_randomize", config_.brickSpawnRandomize},
                {"powerups_per_round", config_.powerupsPerRound},
                {"max_players", config_.maxPlayers},
                {"powers_enabled", config_.powersEnabled}
            }},
            {"session", {
                {"active", summary_.active},
                {"begin_ms", summary_.beginTimestampMs},
                {"end_ms", reportEndMs},
                {"duration_ms", reportDurationMs},
                {"ticks", summary_.tickCount},
                {"recent_events_recorded", summary_.recentEventsRecorded},
                {"recent_events_evicted", summary_.recentEventsEvicted}
            }},
            {"server_flow", {
                {"state", serverFlowState_},
                {"idle", serverIdle_}
            }},
            {"packets", {
                {"sent_attempts", summary_.packetsSent},
                {"recv_attempts", summary_.packetsRecv},
                {"bytes_sent", summary_.packetBytesSent},
                {"bytes_recv", summary_.packetBytesRecv},
                {"sent_failed", summary_.packetsSentFailed},
                {"recv_failed", summary_.packetsRecvFailed},
                {"malformed_recv", summary_.malformedPacketsRecv}
            }},
            {"key_messages", {
                {"snapshot_sent", keyMessages_.snapshotSent},
                {"correction_sent", keyMessages_.correctionSent},
                {"input_recv", keyMessages_.inputRecv},
                {"gameplay_event_sent", keyMessages_.gameplayEventSent}
            }},
            {"input_stream", {
                {"input_packets_received", summary_.inputPacketsReceived},
                {"input_packets_arrived_fully_stale", summary_.inputPacketsFullyStale},
                {"already_processed_input_entries_received", summary_.inputEntriesTooLate},
                {"already_processed_input_entries_received_direct", summary_.inputEntriesTooLateDirect},
                {"already_processed_input_entries_received_buffered", summary_.inputEntriesTooLateBuffered},
                {"input_entries_rejected_too_far_ahead", summary_.inputEntriesTooFarAhead}
            }},
            {"simulation_continuity", {
                {"direct_deadline_consumes", summary_.directDeadlineConsumes},
                {"simulation_gaps", summary_.simulationGaps},
                {"buffered_deadline_recoveries", summary_.bufferedDeadlineRecoveries},
                {"bombs_placed", summary_.bombsPlaced},
                {"bricks_destroyed", summary_.bricksDestroyed},
                {"rounds_ended", summary_.roundsEnded},
                {"rounds_drawn", summary_.roundsDrawn},
                {"round_wins_by_player_id", std::move(roundWins)}
            }},
            {"per_player_input_continuity", std::move(continuity)},
            {"transport_samples", std::move(transportSamples)},
            {"recent_events", std::move(recentEvents)}
        };
    }

    bool NetDiagnostics::writeJsonReport(const std::string_view filePath) const
    {
        if (filePath.empty())
            return false;

        if (!summary_.enabled)
            return false;

        std::ofstream out{std::string(filePath)};
        if (!out)
            return false;

        out << toJson().dump(2) << '\n';
        return out.good();
    }

    // =================================================================================================================
    // ===== Internal helpers ==========================================================================================
    // =================================================================================================================

    uint64_t NetDiagnostics::nowMs()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    uint64_t NetDiagnostics::recentEventDedupeCooldownMs(const NetEvent& /*event*/)
    {
        return kRecentEventDedupeCooldownMs;
    }

    std::string NetDiagnostics::makeRecentEventSignature(const NetEvent& event)
    {
        std::ostringstream out;
        out << static_cast<int>(event.type)
            << '|' << static_cast<int>(event.peerId)
            << '|' << static_cast<int>(event.channelId)
            << '|' << static_cast<int>(event.msgType)
            << '|' << static_cast<int>(event.packetDirection)
            << '|' << static_cast<int>(event.packetResult)
            << '|' << static_cast<int>(event.lifecycleType)
            << '|' << static_cast<int>(event.simulationType)
            << '|';

        if (event.type == NetEventType::Simulation)
        {
            if (event.simulationType == NetSimulationEventType::RoundEnded)
            {
                out << event.detailA
                    << '|' << event.detailB
                    << '|' << event.note;
            }
            else
            {
                // Coalesce repeated gap/recovery storms by semantic class rather than exact seq/tick.
                out << event.note;
            }
        }
        else
        {
            if (event.type == NetEventType::Flow)
            {
                out << event.seq
                    << '|' << event.detailA
                    << '|' << event.detailB
                    << '|' << event.note;
            }
            else
            {
                out << event.note;
            }
        }
        return out.str();
    }

    bool NetDiagnostics::isAlwaysEmitEvent(const NetEvent& event)
    {
        return event.type == NetEventType::SessionBegin
            || event.type == NetEventType::SessionEnd
            || event.type == NetEventType::PeerLifecycle;
    }

    bool NetDiagnostics::shouldEmitPacketEvent(const NetPacketResult result)
    {
        return result != NetPacketResult::Ok;
    }

    void NetDiagnostics::resetForNewSession(const std::string_view ownerTag, const bool enabled)
    {
        enabled_ = enabled;
        sessionActive_ = true;

        ownerTag_.assign(ownerTag.begin(), ownerTag.end());

        config_ = {};
        summary_ = {};
        keyMessages_ = {};
        serverFlowState_.clear();
        serverIdle_ = false;
        summary_.enabled = enabled_;
        summary_.active = true;
        summary_.beginTimestampMs = nowMs();

        recentStart_ = 0;
        recentCount_ = 0;
        latestPeerSamples_.clear();
        peerContinuitySummaries_.clear();
        recentEventRepeatState_.clear();
    }

    void NetDiagnostics::recordRecentEvent(NetEvent event)
    {
        if (event.timestampMs == 0)
            event.timestampMs = nowMs();

        if (isAlwaysEmitEvent(event))
        {
            pushRecentEvent(std::move(event));
            return;
        }

        const std::string signature = makeRecentEventSignature(event);
        auto& repeatState = recentEventRepeatState_[signature];
        const uint64_t cooldownMs = recentEventDedupeCooldownMs(event);

        if (repeatState.lastEmittedTimestampMs != 0
            && (event.timestampMs - repeatState.lastEmittedTimestampMs) < cooldownMs)
        {
            return;
        }

        repeatState.lastEmittedTimestampMs = event.timestampMs;
        pushRecentEvent(std::move(event));
    }

    void NetDiagnostics::pushRecentEvent(NetEvent event)
    {
        if (recentCount_ < kRecentEventCapacity)
        {
            const auto idx = (recentStart_ + recentCount_) % kRecentEventCapacity;
            recentEvents_[idx] = std::move(event);
            recentCount_++;
        }
        else
        {
            recentEvents_[recentStart_] = std::move(event);
            recentStart_ = (recentStart_ + 1) % kRecentEventCapacity;
            summary_.recentEventsEvicted++;
        }

        summary_.recentEventsRecorded++;
    }

} // namespace bomberman::net
