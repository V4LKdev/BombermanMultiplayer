/**
 * @file NetDiagnostics.cpp
 * @brief Implementation of the lightweight multiplayer diagnostics recorder.
 */

#include "NetDiagnostics.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

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
                case NetSimulationEventType::Gap:              return "Gap";
                case NetSimulationEventType::BufferedRecovery: return "BufferedRecovery";
                default:                                       return "Unknown";
            }
        }

        /** @brief Formats one input bitmask using the same compact hex style as network logs. */
        std::string formatInputMask(const uint32_t mask)
        {
            std::ostringstream out;
            out << "0x"
                << std::uppercase << std::hex
                << std::setw(2) << std::setfill('0')
                << (mask & 0xFFu);
            return out.str();
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

        const auto index = static_cast<std::size_t>(static_cast<uint8_t>(type));
        auto& agg = packetAggregates_[index];

        summary_.packetsSent++;
        summary_.packetBytesSent += bytes;

        if (result == NetPacketResult::Ok)
        {
            agg.outgoingOk++;
            agg.outgoingBytes += bytes;
        }
        else
        {
            agg.outgoingFail++;
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

        const auto index = static_cast<std::size_t>(static_cast<uint8_t>(type));
        auto& agg = packetAggregates_[index];

        summary_.packetsRecv++;
        summary_.packetBytesRecv += bytes;

        if (result == NetPacketResult::Ok)
        {
            agg.incomingOk++;
            agg.incomingBytes += bytes;
        }
        else
        {
            agg.incomingFail++;
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
        summary_.malformedPacketBytesRecv += bytes;

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

    void NetDiagnostics::recordBufferedInputRecovery(const uint8_t peerId,
                                                     const uint32_t inputSeq,
                                                     const uint32_t serverTick)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.bufferedInputRecoveries++;
        auto& peerSummary = peerContinuitySummaries_[peerId];
        peerSummary.peerId = peerId;
        peerSummary.timestampMs = nowMs();
        peerSummary.bufferedInputRecoveries++;
        peerSummary.lastProcessedInputSeq = inputSeq;

        NetEvent event{};
        event.type = NetEventType::Simulation;
        event.timestampMs = nowMs();
        event.simulationType = NetSimulationEventType::BufferedRecovery;
        event.peerId = peerId;
        event.seq = inputSeq;
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

    // =================================================================================================================
    // ===== Session maintenance and reporting =========================================================================
    // =================================================================================================================

    void NetDiagnostics::advanceTick()
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.tickCount++;
    }

    bool NetDiagnostics::writeSessionReport(const std::string_view filePath) const
    {
        if (filePath.empty())
            return false;

        if (!summary_.enabled)
            return false;

        std::ofstream out{std::string(filePath)};
        if (!out)
            return false;

        const uint64_t reportEndMs = summary_.active ? nowMs() : summary_.endTimestampMs;
        const uint64_t reportDurationMs =
            (reportEndMs >= summary_.beginTimestampMs) ? (reportEndMs - summary_.beginTimestampMs) : 0;

        // Keep the report deliberately plain so it stays diffable and easy to scan during manual testing.
        const auto writeSectionHeader = [&out](const std::string_view title)
        {
            out << "--- " << title << " ---\n";
        };

        const auto writeKeyValue = [&out](const std::string_view key, const auto& value)
        {
            out << key << '=' << value << '\n';
        };

        out << "===== net_diagnostics_report =====\n";
        writeKeyValue("session_owner", ownerTag_);
        out << '\n';

        writeSectionHeader("Session summary");
        writeKeyValue("active", summary_.active ? 1 : 0);
        writeKeyValue("begin_ms", summary_.beginTimestampMs);
        writeKeyValue("end_ms", reportEndMs);
        writeKeyValue("duration_ms", reportDurationMs);
        writeKeyValue("ticks", summary_.tickCount);
        writeKeyValue("recent_events_recorded", summary_.recentEventsRecorded);
        writeKeyValue("recent_events_evicted", summary_.recentEventsEvicted);
        out << '\n';

        writeSectionHeader("Packet summary");
        writeKeyValue("packets_sent_attempts", summary_.packetsSent);
        writeKeyValue("packets_recv_attempts", summary_.packetsRecv);
        writeKeyValue("bytes_sent", summary_.packetBytesSent);
        writeKeyValue("bytes_recv", summary_.packetBytesRecv);
        writeKeyValue("packets_sent_failed", summary_.packetsSentFailed);
        writeKeyValue("packets_recv_failed", summary_.packetsRecvFailed);
        writeKeyValue("malformed_packets_recv", summary_.malformedPacketsRecv);
        writeKeyValue("malformed_packet_bytes_recv", summary_.malformedPacketBytesRecv);
        out << '\n';

        writeSectionHeader("Input stream summary");
        writeKeyValue("input_packets_received", summary_.inputPacketsReceived);
        writeKeyValue("input_packets_fully_stale", summary_.inputPacketsFullyStale);
        writeKeyValue("input_entries_too_late", summary_.inputEntriesTooLate);
        writeKeyValue("input_entries_too_far_ahead", summary_.inputEntriesTooFarAhead);
        out << '\n';

        writeSectionHeader("Simulation continuity");
        writeKeyValue("simulation_gaps", summary_.simulationGaps);
        writeKeyValue("buffered_input_recoveries", summary_.bufferedInputRecoveries);
        out << '\n';

        writeSectionHeader("Per-peer input continuity");
        if (peerContinuitySummaries_.empty())
        {
            out << "  - none\n";
        }
        else
        {
            std::vector<uint8_t> peerIds;
            peerIds.reserve(peerContinuitySummaries_.size());
            for (const auto& [peerId, summary] : peerContinuitySummaries_)
            {
                (void)summary;
                peerIds.push_back(peerId);
            }
            std::sort(peerIds.begin(), peerIds.end());

            for (const uint8_t peerId : peerIds)
            {
                const auto& peerSummary = peerContinuitySummaries_.at(peerId);
                const uint32_t pendingInputs =
                    (peerSummary.lastReceivedInputSeq > peerSummary.lastProcessedInputSeq)
                        ? (peerSummary.lastReceivedInputSeq - peerSummary.lastProcessedInputSeq)
                        : 0u;

                out << "  - peer=" << static_cast<int>(peerId)
                    << " ts_ms=" << peerSummary.timestampMs
                    << " gaps=" << peerSummary.simulationGaps
                    << " buffered_recoveries=" << peerSummary.bufferedInputRecoveries
                    << " last_recv_seq=" << peerSummary.lastReceivedInputSeq
                    << " last_processed_seq=" << peerSummary.lastProcessedInputSeq
                    << " pending_inputs=" << pendingInputs
                    << '\n';
            }
        }
        out << '\n';

        writeSectionHeader("Latest peer samples");
        if (latestPeerSamples_.empty())
        {
            out << "  - none\n";
        }
        else
        {
            std::vector<uint8_t> peerIds;
            peerIds.reserve(latestPeerSamples_.size());
            for (const auto& [peerId, sample] : latestPeerSamples_)
            {
                (void)sample;
                peerIds.push_back(peerId);
            }
            std::sort(peerIds.begin(), peerIds.end());

            for (const uint8_t peerId : peerIds)
            {
                const auto& sample = latestPeerSamples_.at(peerId);
                out << "  - peer=" << static_cast<int>(peerId)
                    << " ts_ms=" << sample.timestampMs
                    << " rtt_ms=" << sample.rttMs
                    << " rtt_var_ms=" << sample.rttVarianceMs
                    << " loss_permille=" << sample.packetLossPermille
                    << " q_rel=" << sample.queuedReliable
                    << " q_unrel=" << sample.queuedUnreliable
                    << '\n';
            }
        }
        out << '\n';

        writeSectionHeader("Packet aggregates");
        bool wroteAggregate = false;
        for (std::size_t i = 0; i < packetAggregates_.size(); ++i)
        {
            const auto& agg = packetAggregates_[i];
            const uint64_t total =
                agg.outgoingOk + agg.outgoingFail +
                agg.incomingOk + agg.incomingFail;

            if (total == 0)
                continue;

            wroteAggregate = true;

            out << "  - msg=0x" << std::hex << static_cast<int>(i) << std::dec
                << " name=" << msgTypeName(static_cast<EMsgType>(i))
                << " out_ok=" << agg.outgoingOk
                << " out_fail=" << agg.outgoingFail
                << " out_bytes=" << agg.outgoingBytes
                << " in_ok=" << agg.incomingOk
                << " in_fail=" << agg.incomingFail
                << " in_bytes=" << agg.incomingBytes
                << '\n';
        }

        if (!wroteAggregate)
            out << "  - none\n";

        out << '\n';

        writeSectionHeader("Recent events");
        if (recentCount_ == 0)
        {
            out << "  - none\n";
        }
        else
        {
            for (std::size_t i = 0; i < recentCount_; ++i)
            {
                const auto idx = (recentStart_ + i) % kRecentEventCapacity;
                const auto& event = recentEvents_[idx];

                out << "  - ts_ms=" << event.timestampMs
                    << " type=" << toString(event.type);

                switch (event.type)
                {
                    case NetEventType::PacketSent:
                    case NetEventType::PacketRecv:
                        out << " msg=0x" << std::hex << static_cast<int>(event.msgType) << std::dec
                            << " ch=" << static_cast<int>(event.channelId)
                            << " peer=" << static_cast<int>(event.peerId)
                            << " result=" << toString(event.packetResult);
                        break;

                    case NetEventType::PeerLifecycle:
                        if (event.peerId != 0xFF)
                            out << " peer=" << static_cast<int>(event.peerId);
                        out << " lifecycle=" << toString(event.lifecycleType)
                            << " transport_peer=" << event.detailA;
                        break;

                    case NetEventType::Simulation:
                        out << " peer=" << static_cast<int>(event.peerId)
                            << " seq=" << event.seq
                            << " sim=" << toString(event.simulationType)
                            << " server_tick=" << event.detailB;
                        if (event.simulationType == NetSimulationEventType::Gap)
                            out << " held_mask=" << formatInputMask(event.detailA);
                        break;

                    case NetEventType::SessionBegin:
                    case NetEventType::SessionEnd:
                        break;

                    default:
                        if (event.peerId != 0xFF)
                            out << " peer=" << static_cast<int>(event.peerId);
                        if (event.channelId != 0xFF)
                            out << " ch=" << static_cast<int>(event.channelId);
                        if (event.msgType != 0)
                            out << " msg=0x" << std::hex << static_cast<int>(event.msgType) << std::dec;
                        if (event.seq != 0)
                            out << " seq=" << event.seq;
                        if (event.detailA != 0)
                            out << " detail_a=" << event.detailA;
                        if (event.detailB != 0)
                            out << " detail_b=" << event.detailB;
                        break;
                }

                if (!event.note.empty())
                    out << " note=\"" << event.note << '"';

                out << '\n';
            }
        }

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
            << '|' << event.seq
            << '|' << static_cast<int>(event.packetDirection)
            << '|' << static_cast<int>(event.packetResult)
            << '|' << static_cast<int>(event.lifecycleType)
            << '|' << static_cast<int>(event.simulationType)
            << '|' << event.detailA
            << '|' << event.detailB
            << '|' << event.note;
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

        summary_ = {};
        summary_.enabled = enabled_;
        summary_.active = true;
        summary_.beginTimestampMs = nowMs();

        recentStart_ = 0;
        recentCount_ = 0;
        latestPeerSamples_.clear();
        peerContinuitySummaries_.clear();
        recentEventRepeatState_.clear();

        for (auto& item : packetAggregates_)
            item = {};
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
