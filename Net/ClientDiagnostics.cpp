/**
 * @file ClientDiagnostics.cpp
 * @brief Client-side multiplayer diagnostics recorder and report writer.
 * @ingroup net_client
 */

#include "ClientDiagnostics.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>

#include "Client/NetClient.h"

namespace bomberman::net
{
    namespace
    {
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

        constexpr const char* toString(NetPeerLifecycleType type)
        {
            switch (type)
            {
                case NetPeerLifecycleType::TransportConnected: return "TransportConnected";
                case NetPeerLifecycleType::PlayerAccepted: return "PlayerAccepted";
                case NetPeerLifecycleType::PeerRejected: return "PeerRejected";
                case NetPeerLifecycleType::PeerDisconnected: return "PeerDisconnected";
                case NetPeerLifecycleType::TransportDisconnectedBeforeHandshake:
                    return "TransportDisconnectedBeforeHandshake";
                default:
                    return "Unknown";
            }
        }

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

        constexpr const char* toString(EConnectState state)
        {
            switch (state)
            {
                case EConnectState::Disconnected:    return "Disconnected";
                case EConnectState::Connecting:      return "Connecting";
                case EConnectState::Handshaking:     return "Handshaking";
                case EConnectState::Connected:       return "Connected";
                case EConnectState::Disconnecting:   return "Disconnecting";
                case EConnectState::FailedResolve:   return "FailedResolve";
                case EConnectState::FailedConnect:   return "FailedConnect";
                case EConnectState::FailedHandshake: return "FailedHandshake";
                case EConnectState::FailedProtocol:  return "FailedProtocol";
                case EConnectState::FailedInit:      return "FailedInit";
                default:                             return "Unknown";
            }
        }

        void incrementKeyMessageSent(ClientKeyMessageAggregates& keyMessages, const EMsgType type)
        {
            if (type == EMsgType::Input)
                keyMessages.inputSent++;
        }

        void incrementKeyMessageRecv(ClientKeyMessageAggregates& keyMessages, const EMsgType type)
        {
            switch (type)
            {
                case EMsgType::Snapshot:
                    keyMessages.snapshotRecv++;
                    break;
                case EMsgType::Correction:
                    keyMessages.correctionRecv++;
                    break;
                case EMsgType::BombPlaced:
                case EMsgType::ExplosionResolved:
                    keyMessages.gameplayEventRecv++;
                    break;
                default:
                    break;
            }
        }

        void mergePredictionStats(PredictionStats& into, const PredictionStats& add)
        {
            into.localInputsApplied += add.localInputsApplied;
            into.localInputsDeferred += add.localInputsDeferred;
            into.rejectedLocalInputs += add.rejectedLocalInputs;

            into.correctionsApplied += add.correctionsApplied;
            into.correctionsWithRetainedPredictedState += add.correctionsWithRetainedPredictedState;
            into.correctionsMismatched += add.correctionsMismatched;

            into.totalCorrectionDeltaQ += add.totalCorrectionDeltaQ;
            into.maxCorrectionDeltaQ = std::max(into.maxCorrectionDeltaQ, add.maxCorrectionDeltaQ);

            into.totalReplayedInputs += add.totalReplayedInputs;
            into.maxReplayedInputs = std::max(into.maxReplayedInputs, add.maxReplayedInputs);

            into.replayTruncations += add.replayTruncations;
            into.totalMissingInputHistory += add.totalMissingInputHistory;
            into.maxMissingInputHistory = std::max(into.maxMissingInputHistory, add.maxMissingInputHistory);
            into.recoveryActivations += add.recoveryActivations;
            into.recoveryResolutions += add.recoveryResolutions;
        }
    } // namespace

    ClientDiagnostics::ClientDiagnostics(bool enabled)
        : enabled_(enabled)
    {
        summary_.enabled = enabled;
        summary_.finalState = EConnectState::Disconnected;
    }

    void ClientDiagnostics::beginSession(const std::string_view ownerTag,
                                         const bool enabled,
                                         const bool predictionEnabled,
                                         const bool remoteSmoothingEnabled)
    {
        resetForNewSession(ownerTag, enabled);
        config_.protocolVersion = kProtocolVersion;
        config_.clientTickRate = sim::kTickRate;
        config_.predictionEnabled = predictionEnabled;
        config_.remoteSmoothingEnabled = remoteSmoothingEnabled;

        if (!enabled_)
            return;

        recordEvent(NetEventType::SessionBegin, "session started");
    }

    void ClientDiagnostics::endSession()
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

    void ClientDiagnostics::recordWelcome(const uint8_t assignedPlayerId,
                                          const uint16_t serverTickRate,
                                          const uint64_t handshakeDurationMs,
                                          const uint32_t transportPeerId)
    {
        if (!enabled_ || !sessionActive_)
            return;

        config_.assignedPlayerId = assignedPlayerId;
        config_.serverTickRate = serverTickRate;
        summary_.handshakeDurationMs = handshakeDurationMs;

        recordPeerLifecycle(NetPeerLifecycleType::PlayerAccepted,
                            assignedPlayerId,
                            transportPeerId,
                            "welcome accepted");
    }

    void ClientDiagnostics::recordFinalState(const EConnectState finalState, const uint64_t connectedDurationMs)
    {
        summary_.finalState = finalState;
        summary_.connectedDurationMs = connectedDurationMs;
    }

    void ClientDiagnostics::recordPacketSent(const EMsgType type,
                                             const uint8_t channelId,
                                             const std::size_t bytes,
                                             const NetPacketResult result)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.packetsSent++;
        summary_.packetBytesSent += bytes;
        if (result == NetPacketResult::Ok)
            incrementKeyMessageSent(keyMessages_, type);
        else
            summary_.packetsSentFailed++;

        if (!shouldEmitPacketEvent(result))
            return;

        NetEvent event{};
        event.type = NetEventType::PacketSent;
        event.timestampMs = nowMs();
        event.packetDirection = NetPacketDirection::Outgoing;
        event.packetResult = result;
        event.channelId = channelId;
        event.msgType = static_cast<uint8_t>(type);
        event.detailA = static_cast<uint32_t>(bytes);
        recordRecentEvent(std::move(event));
    }

    void ClientDiagnostics::recordPacketRecv(const EMsgType type,
                                             const uint8_t channelId,
                                             const std::size_t bytes,
                                             const NetPacketResult result)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.packetsRecv++;
        summary_.packetBytesRecv += bytes;
        if (result == NetPacketResult::Ok)
            incrementKeyMessageRecv(keyMessages_, type);
        else
            summary_.packetsRecvFailed++;

        if (!shouldEmitPacketEvent(result))
            return;

        NetEvent event{};
        event.type = NetEventType::PacketRecv;
        event.timestampMs = nowMs();
        event.packetDirection = NetPacketDirection::Incoming;
        event.packetResult = result;
        event.channelId = channelId;
        event.msgType = static_cast<uint8_t>(type);
        event.detailA = static_cast<uint32_t>(bytes);
        recordRecentEvent(std::move(event));
    }

    void ClientDiagnostics::recordMalformedPacket(const uint8_t channelId,
                                                  const std::size_t bytes,
                                                  const std::string_view note)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.packetsRecv++;
        summary_.packetBytesRecv += bytes;
        summary_.packetsRecvFailed++;
        summary_.malformedPackets++;

        NetEvent event{};
        event.type = NetEventType::PacketRecv;
        event.timestampMs = nowMs();
        event.packetDirection = NetPacketDirection::Incoming;
        event.packetResult = NetPacketResult::Malformed;
        event.channelId = channelId;
        event.detailA = static_cast<uint32_t>(bytes);
        event.note = note;
        recordRecentEvent(std::move(event));
    }

    void ClientDiagnostics::recordPeerLifecycle(const NetPeerLifecycleType type,
                                                const std::optional<uint8_t> playerId,
                                                const uint32_t transportPeerId,
                                                const std::string_view note)
    {
        if (!enabled_ || !sessionActive_)
            return;

        NetEvent event{};
        event.type = NetEventType::PeerLifecycle;
        event.timestampMs = nowMs();
        event.lifecycleType = type;
        event.peerId = playerId.value_or(0xFF);
        event.detailA = transportPeerId;
        event.note = std::string(note);
        recordRecentEvent(std::move(event));
    }

    void ClientDiagnostics::sampleTransport(const uint32_t rttMs,
                                            const uint32_t rttVarianceMs,
                                            const uint32_t lossPermille)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.lastRttMs = rttMs;
        summary_.lastRttVarianceMs = rttVarianceMs;
        summary_.lastLossPermille = lossPermille;
        summary_.transportSamples++;
        summary_.sampledRttMsTotal += rttMs;
        summary_.sampledRttVarianceMsTotal += rttVarianceMs;
        summary_.sampledLossPermilleTotal += lossPermille;
    }

    void ClientDiagnostics::sampleInputSendGap(const uint32_t gapMs)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.maxInputSendGapMs = std::max(summary_.maxInputSendGapMs, static_cast<uint64_t>(gapMs));
    }

    void ClientDiagnostics::sampleLobbySilence(const uint32_t lobbySilenceMs)
    {
        summary_.maxLobbySilenceMs = std::max(summary_.maxLobbySilenceMs, static_cast<uint64_t>(lobbySilenceMs));
    }

    void ClientDiagnostics::sampleGameplaySilence(const uint32_t gameplaySilenceMs)
    {
        summary_.maxGameplaySilenceMs = std::max(summary_.maxGameplaySilenceMs, static_cast<uint64_t>(gameplaySilenceMs));
    }

    void ClientDiagnostics::recordStaleSnapshotIgnored(const uint32_t serverTick)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.staleSnapshotsIgnored++;

        NetEvent event{};
        event.type = NetEventType::Simulation;
        event.timestampMs = nowMs();
        event.simulationType = NetSimulationEventType::Gap;
        event.detailB = serverTick;
        event.note = "stale snapshot ignored";
        recordRecentEvent(std::move(event));
    }

    void ClientDiagnostics::recordStaleCorrectionIgnored(const uint32_t serverTick,
                                                         const uint32_t lastProcessedInputSeq)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.staleCorrectionsIgnored++;

        NetEvent event{};
        event.type = NetEventType::Simulation;
        event.timestampMs = nowMs();
        event.simulationType = NetSimulationEventType::Gap;
        event.seq = lastProcessedInputSeq;
        event.detailB = serverTick;
        event.note = "stale correction ignored";
        recordRecentEvent(std::move(event));
    }

    void ClientDiagnostics::recordBrokenGameplayEventStream(const uint32_t matchId)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.brokenGameplayEventStreamIncidents++;

        NetEvent event{};
        event.type = NetEventType::Simulation;
        event.timestampMs = nowMs();
        event.simulationType = NetSimulationEventType::Gap;
        event.detailA = matchId;
        event.note = "broken gameplay event stream";
        recordRecentEvent(std::move(event));
    }

    void ClientDiagnostics::samplePendingGameplayEventDepth(const std::size_t depth)
    {
        summary_.maxPendingGameplayEventDepth =
            std::max(summary_.maxPendingGameplayEventDepth, static_cast<uint64_t>(depth));
    }

    void ClientDiagnostics::feedPredictionStats(const PredictionStats& stats,
                                                const bool reachedActive,
                                                const bool everRecovered)
    {
        if (!enabled_ || !sessionActive_)
            return;

        mergePredictionStats(summary_.prediction, stats);
        summary_.predictionReachedActive = summary_.predictionReachedActive || reachedActive;
        summary_.predictionEverRecovered = summary_.predictionEverRecovered || everRecovered;
    }

    void ClientDiagnostics::recordEvent(const NetEvent& event)
    {
        if (!enabled_ || !sessionActive_)
            return;

        NetEvent stamped = event;
        if (stamped.timestampMs == 0)
            stamped.timestampMs = nowMs();

        recordRecentEvent(std::move(stamped));
    }

    nlohmann::json ClientDiagnostics::toJson() const
    {
        const uint64_t reportEndMs = summary_.active ? nowMs() : summary_.endTimestampMs;
        const uint64_t reportDurationMs =
            (reportEndMs >= summary_.beginTimestampMs) ? (reportEndMs - summary_.beginTimestampMs) : 0;
        const double avgRttMs =
            summary_.transportSamples > 0
                ? static_cast<double>(summary_.sampledRttMsTotal) / static_cast<double>(summary_.transportSamples)
                : 0.0;
        const double avgRttVarianceMs =
            summary_.transportSamples > 0
                ? static_cast<double>(summary_.sampledRttVarianceMsTotal) / static_cast<double>(summary_.transportSamples)
                : 0.0;
        const double avgLossPermille =
            summary_.transportSamples > 0
                ? static_cast<double>(summary_.sampledLossPermilleTotal) / static_cast<double>(summary_.transportSamples)
                : 0.0;

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
            {"report", "client_diagnostics_report"},
            {"report_version", kDiagnosticsReportVersion},
            {"session_owner", ownerTag_},
            {"config", {
                {"protocol_version", config_.protocolVersion},
                {"client_tick_rate", config_.clientTickRate},
                {"prediction_enabled", config_.predictionEnabled},
                {"remote_smoothing_enabled", config_.remoteSmoothingEnabled},
                {"assigned_player_id", config_.assignedPlayerId.has_value() ? nlohmann::json(*config_.assignedPlayerId) : nlohmann::json(nullptr)},
                {"server_tick_rate", config_.serverTickRate.has_value() ? nlohmann::json(*config_.serverTickRate) : nlohmann::json(nullptr)}
            }},
            {"session", {
                {"active", summary_.active},
                {"begin_ms", summary_.beginTimestampMs},
                {"end_ms", reportEndMs},
                {"duration_ms", reportDurationMs},
                {"final_state", toString(summary_.finalState)},
                {"connected_duration_ms", summary_.connectedDurationMs},
                {"handshake_duration_ms", summary_.handshakeDurationMs},
                {"recent_events_recorded", summary_.recentEventsRecorded},
                {"recent_events_evicted", summary_.recentEventsEvicted}
            }},
            {"transport", {
                {"samples", summary_.transportSamples},
                {"rtt_ms", summary_.lastRttMs},
                {"avg_rtt_ms", avgRttMs},
                {"rtt_var_ms", summary_.lastRttVarianceMs},
                {"avg_rtt_var_ms", avgRttVarianceMs},
                {"loss_permille", summary_.lastLossPermille},
                {"avg_loss_permille", avgLossPermille},
                {"max_lobby_silence_ms", summary_.maxLobbySilenceMs},
                {"max_gameplay_silence_ms", summary_.maxGameplaySilenceMs}
            }},
            {"prediction", {
                {"reached_active", summary_.predictionReachedActive},
                {"ever_recovered", summary_.predictionEverRecovered},
                {"local_inputs_applied", summary_.prediction.localInputsApplied},
                {"local_inputs_deferred", summary_.prediction.localInputsDeferred},
                {"rejected_local_inputs", summary_.prediction.rejectedLocalInputs},
                {"corrections_applied", summary_.prediction.correctionsApplied},
                {"corrections_with_retained_predicted_state", summary_.prediction.correctionsWithRetainedPredictedState},
                {"corrections_mismatched", summary_.prediction.correctionsMismatched},
                {"total_correction_delta_q", summary_.prediction.totalCorrectionDeltaQ},
                {"max_correction_delta_q", summary_.prediction.maxCorrectionDeltaQ},
                {"total_replayed_inputs", summary_.prediction.totalReplayedInputs},
                {"max_replayed_inputs", summary_.prediction.maxReplayedInputs},
                {"replay_truncations", summary_.prediction.replayTruncations},
                {"total_missing_input_history", summary_.prediction.totalMissingInputHistory},
                {"max_missing_input_history", summary_.prediction.maxMissingInputHistory},
                {"recovery_activations", summary_.prediction.recoveryActivations},
                {"recovery_resolutions", summary_.prediction.recoveryResolutions}
            }},
            {"packets", {
                {"sent_attempts", summary_.packetsSent},
                {"recv_attempts", summary_.packetsRecv},
                {"bytes_sent", summary_.packetBytesSent},
                {"bytes_recv", summary_.packetBytesRecv},
                {"sent_failed", summary_.packetsSentFailed},
                {"recv_failed", summary_.packetsRecvFailed},
                {"malformed_packets", summary_.malformedPackets},
                {"max_input_send_gap_ms", summary_.maxInputSendGapMs}
            }},
            {"key_messages", {
                {"snapshot_recv", keyMessages_.snapshotRecv},
                {"correction_recv", keyMessages_.correctionRecv},
                {"input_sent", keyMessages_.inputSent},
                {"gameplay_event_recv", keyMessages_.gameplayEventRecv}
            }},
            {"stream_health", {
                {"stale_snapshots_ignored", summary_.staleSnapshotsIgnored},
                {"stale_corrections_ignored", summary_.staleCorrectionsIgnored},
                {"broken_gameplay_event_stream_incidents", summary_.brokenGameplayEventStreamIncidents},
                {"max_pending_gameplay_event_depth", summary_.maxPendingGameplayEventDepth}
            }},
            {"recent_events", std::move(recentEvents)}
        };
    }

    bool ClientDiagnostics::writeJsonReport(const std::string_view filePath) const
    {
        if (filePath.empty() || !summary_.enabled)
            return false;

        std::ofstream out{std::string(filePath)};
        if (!out)
            return false;

        out << toJson().dump(2) << '\n';
        return out.good();
    }

    uint64_t ClientDiagnostics::nowMs()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    uint64_t ClientDiagnostics::recentEventDedupeCooldownMs(const NetEvent& /*event*/)
    {
        return kRecentEventDedupeCooldownMs;
    }

    std::string ClientDiagnostics::makeRecentEventSignature(const NetEvent& event)
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
        return out.str();
    }

    bool ClientDiagnostics::isAlwaysEmitEvent(const NetEvent& event)
    {
        return event.type == NetEventType::SessionBegin ||
               event.type == NetEventType::SessionEnd ||
               event.type == NetEventType::PeerLifecycle;
    }

    bool ClientDiagnostics::shouldEmitPacketEvent(const NetPacketResult result)
    {
        return result != NetPacketResult::Ok;
    }

    void ClientDiagnostics::recordEvent(const NetEventType type, const std::string_view note)
    {
        NetEvent event{};
        event.type = type;
        event.note = std::string(note);
        recordEvent(event);
    }

    void ClientDiagnostics::resetForNewSession(const std::string_view ownerTag, const bool enabled)
    {
        enabled_ = enabled;
        sessionActive_ = true;

        ownerTag_.assign(ownerTag.begin(), ownerTag.end());
        config_ = {};
        summary_ = {};
        summary_.enabled = enabled_;
        summary_.active = true;
        summary_.beginTimestampMs = nowMs();
        summary_.finalState = EConnectState::Disconnected;
        keyMessages_ = {};

        recentStart_ = 0;
        recentCount_ = 0;
        recentEventRepeatState_.clear();
    }

    void ClientDiagnostics::recordRecentEvent(NetEvent event)
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

        if (repeatState.lastEmittedTimestampMs != 0 &&
            (event.timestampMs - repeatState.lastEmittedTimestampMs) < cooldownMs)
        {
            return;
        }

        repeatState.lastEmittedTimestampMs = event.timestampMs;
        pushRecentEvent(std::move(event));
    }

    void ClientDiagnostics::pushRecentEvent(NetEvent event)
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
