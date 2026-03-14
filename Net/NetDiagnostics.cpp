#include "NetDiagnostics.h"

#include <chrono>
#include <fstream>
#include <utility>

namespace bomberman::net
{
    namespace
    {
        // =============================================================================================================
        // ===== Local string helpers ==================================================================================
        // =============================================================================================================

        constexpr const char* toString(NetEventType type)
        {
            switch (type)
            {
                case NetEventType::SessionBegin:  return "SessionBegin";
                case NetEventType::SessionEnd:    return "SessionEnd";
                case NetEventType::PacketSent:    return "PacketSent";
                case NetEventType::PacketRecv:    return "PacketRecv";
                case NetEventType::InputAnomaly:  return "InputAnomaly";
                case NetEventType::PeerSample:    return "PeerSample";
                case NetEventType::Note:          return "Note";
                default:                          return "Unknown";
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

        constexpr const char* toString(NetInputAnomalyType type)
        {
            switch (type)
            {
                case NetInputAnomalyType::OutOfOrder:     return "OutOfOrder";
                case NetInputAnomalyType::Duplicate:      return "Duplicate";
                case NetInputAnomalyType::Gap:            return "Gap";
                case NetInputAnomalyType::UnknownButtons: return "UnknownButtons";
                case NetInputAnomalyType::TooOld:         return "TooOld";
                case NetInputAnomalyType::Count:          return "Count";
                default:                                  return "Unknown";
            }
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

        pushRecentEvent(std::move(stamped));
    }

    void NetDiagnostics::recordPacketSent(const EMsgType type, const uint8_t channelId, const std::size_t bytes,
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
        event.channelId = channelId;
        event.msgType = static_cast<uint8_t>(type);
        event.valueA = static_cast<uint32_t>(bytes);
        pushRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordPacketRecv(const EMsgType type, const uint8_t channelId, const std::size_t bytes,
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
        event.channelId = channelId;
        event.msgType = static_cast<uint8_t>(type);
        event.valueA = static_cast<uint32_t>(bytes);
        pushRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordMalformedPacketRecv(const uint8_t channelId, const std::size_t bytes,
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
        event.channelId = channelId;
        event.valueA = static_cast<uint32_t>(bytes);
        event.note = note;
        pushRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordInputAnomaly(const NetInputAnomalyType type, const uint32_t inputSeq,
                                            const uint8_t buttons, std::string_view note)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.inputAnomalyCount++;

        const auto typeIndex = static_cast<std::size_t>(type);
        if (typeIndex < summary_.anomaliesByType.size())
            summary_.anomaliesByType[typeIndex]++;

        if (!shouldEmitInputAnomalyEvent(type))
            return;

        NetEvent event{};
        event.type = NetEventType::InputAnomaly;
        event.timestampMs = nowMs();
        event.anomalyType = type;
        event.seq = inputSeq;
        event.valueA = buttons;
        event.note = note;
        pushRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordInputEntriesRedundant(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.inputEntriesRedundant += count;
    }

    void NetDiagnostics::recordInputEntriesReceived(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.inputEntriesReceivedTotal += count;
    }

    void NetDiagnostics::recordInputEntriesAccepted(const uint32_t count)
    {
        if (!enabled_ || !sessionActive_ || count == 0)
            return;

        summary_.inputEntriesAccepted += count;
    }

    void NetDiagnostics::samplePeer(const uint8_t peerId, const uint32_t rttMs, const uint32_t packetLossPermille,
                                    const uint32_t queuedReliable, const uint32_t queuedUnreliable)
    {
        if (!enabled_ || !sessionActive_)
            return;

        NetPeerSample sample{};
        sample.peerId = peerId;
        sample.timestampMs = nowMs();
        sample.rttMs = rttMs;
        sample.packetLossPermille = packetLossPermille;
        sample.queuedReliable = queuedReliable;
        sample.queuedUnreliable = queuedUnreliable;

        latestPeerSamples_[peerId] = sample;

        // Latest peer health is kept in sample storage. Recent-event history is
        // reserved for more exceptional transitions and failures.
    }

    // =================================================================================================================
    // ===== Session maintenance and reporting =========================================================================
    // =================================================================================================================

    void NetDiagnostics::tick(const uint32_t nowMs)
    {
        if (!enabled_ || !sessionActive_)
            return;

        // Reserved for future cadence-based summary/report logic.
        (void)nowMs;
        summary_.tickCount++;
    }

    bool NetDiagnostics::writeSessionReport(const std::string_view filePath) const
    {
        if (filePath.empty())
            return false;

        std::ofstream out{std::string(filePath)};
        if (!out.is_open())
            return false;

        out << "net_diagnostics_report\n";
        out << "owner=" << ownerTag_ << "\n";
        out << "enabled=" << (summary_.enabled ? 1 : 0) << "\n";
        out << "active=" << (summary_.active ? 1 : 0) << "\n";
        out << "begin_ms=" << summary_.beginTimestampMs << "\n";
        out << "end_ms=" << summary_.endTimestampMs << "\n";
        out << "duration_ms=" << summary_.durationMs << "\n";
        out << "ticks=" << summary_.tickCount << "\n";
        out << "events_recorded=" << summary_.eventsRecorded << "\n";
        out << "events_dropped=" << summary_.eventsDropped << "\n";
        out << "packets_sent_attempts=" << summary_.packetsSent << "\n";
        out << "packets_recv_attempts=" << summary_.packetsRecv << "\n";
        out << "bytes_sent=" << summary_.packetBytesSent << "\n";
        out << "bytes_recv=" << summary_.packetBytesRecv << "\n";
        out << "packets_sent_failed=" << summary_.packetsSentFailed << "\n";
        out << "packets_recv_failed=" << summary_.packetsRecvFailed << "\n";
        out << "malformed_packets_recv=" << summary_.malformedPacketsRecv << "\n";
        out << "malformed_packet_bytes_recv=" << summary_.malformedPacketBytesRecv << "\n";
        out << "input_entries_received_total=" << summary_.inputEntriesReceivedTotal << "\n";
        out << "input_entries_accepted=" << summary_.inputEntriesAccepted << "\n";
        out << "input_entries_redundant=" << summary_.inputEntriesRedundant << "\n";
        if (summary_.inputEntriesReceivedTotal > 0)
        {
            const double redundancyRatio = static_cast<double>(summary_.inputEntriesRedundant)
                                         / static_cast<double>(summary_.inputEntriesReceivedTotal);
            out << "input_redundancy_ratio=" << redundancyRatio << "\n";
        }
        out << "input_anomalies=" << summary_.inputAnomalyCount << "\n";

        out << "anomalies_by_type\n";
        for (std::size_t i = 0; i < summary_.anomaliesByType.size(); ++i)
        {
            const auto type = static_cast<NetInputAnomalyType>(i);
            out << "  - " << toString(type) << ": " << summary_.anomaliesByType[i] << "\n";
        }

        out << "latest_peer_samples\n";
        for (const auto& [peerId, sample] : latestPeerSamples_)
        {
            out << "  - peer=" << static_cast<int>(peerId)
                << " ts_ms=" << sample.timestampMs
                << " rtt_ms=" << sample.rttMs
                << " loss_permille=" << sample.packetLossPermille
                << " q_rel=" << sample.queuedReliable
                << " q_unrel=" << sample.queuedUnreliable << "\n";
        }

        out << "recent_events\n";
        for (std::size_t i = 0; i < recentCount_; ++i)
        {
            const auto idx = (recentStart_ + i) % kRecentEventCapacity;
            const auto& event = recentEvents_[idx];

            out << "  - ts_ms=" << event.timestampMs
                << " type=" << toString(event.type)
                << " msg=0x" << std::hex << static_cast<int>(event.msgType) << std::dec
                << " ch=" << static_cast<int>(event.channelId)
                << " peer=" << static_cast<int>(event.peerId)
                << " seq=" << event.seq
                << " a=" << event.valueA
                << " b=" << event.valueB
                << " result=" << toString(event.packetResult);

            if (!event.note.empty())
                out << " note=\"" << event.note << "\"";

            out << "\n";
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

        for (auto& item : packetAggregates_)
            item = {};

    }

    bool NetDiagnostics::shouldEmitPacketEvent(const NetPacketResult result)
    {
        // Successful packet traffic is counted exactly in summary counters, but
        // does not belong in the bounded recent-event history by default.
        return result != NetPacketResult::Ok;
    }

    bool NetDiagnostics::shouldEmitInputAnomalyEvent(const NetInputAnomalyType type)
    {
        // "TooOld" is expected under the batched resend model. We count it
        // exactly in summaries, but do not retain individual occurrences in
        // bounded recent-event history.
        if (type == NetInputAnomalyType::TooOld)
            return false;

        return true;
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
            summary_.eventsDropped++;
        }

        summary_.eventsRecorded++;
    }

} // namespace bomberman::net
