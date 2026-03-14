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
                case NetInputAnomalyType::OutOfOrder: return "OutOfOrder";
                case NetInputAnomalyType::Duplicate:  return "Duplicate";
                case NetInputAnomalyType::Gap:        return "Gap";
                case NetInputAnomalyType::Count:      return "Count";
                default:                              return "Unknown";
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

    void NetDiagnostics::recordPacketSent(const EMsgType type, const uint8_t peerId, const uint8_t channelId,
                                            const std::size_t bytes, const NetPacketResult result)
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
        event.valueA = static_cast<uint32_t>(bytes);
        pushRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordPacketRecv(const EMsgType type, const uint8_t peerId, const uint8_t channelId, const std::size_t bytes,
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
        event.valueA = static_cast<uint32_t>(bytes);
        pushRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordMalformedPacketRecv(const uint8_t peerId, const uint8_t channelId, const std::size_t bytes,
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
        event.valueA = static_cast<uint32_t>(bytes);
        event.note = note;
        pushRecentEvent(std::move(event));
    }

    void NetDiagnostics::recordInputAnomaly(const NetInputAnomalyType type, const uint8_t peerId, const uint32_t inputSeq,
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
        event.peerId = peerId;
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

    void NetDiagnostics::recordStaleInputBatch(const uint8_t peerId, const uint32_t highestSeq, const uint8_t count)
    {
        if (!enabled_ || !sessionActive_)
            return;

        summary_.staleInputBatches++;

        NetEvent event{};
        event.type = NetEventType::Note;
        event.timestampMs = nowMs();
        event.peerId = peerId;
        event.seq = highestSeq;
        event.valueA = count;
        event.note = "stale input batch";
        pushRecentEvent(std::move(event));
    }

    // =================================================================================================================
    // ===== Peer transport health sampling ============================================================================
    // =================================================================================================================

    void NetDiagnostics::samplePeer(const uint8_t peerId,
                                    const uint32_t rttMs,
                                    const uint32_t rttVarianceMs,
                                    const uint32_t packetLossPermille,
                                    const uint32_t queuedReliable,
                                    const uint32_t queuedUnreliable)
    {
        if (!enabled_ || !sessionActive_)
            return;

        NetPeerSample sample{};
        sample.peerId = peerId;
        sample.timestampMs = nowMs();
        sample.rttMs = rttMs;
        sample.rttVarianceMs = rttVarianceMs;
        sample.packetLossPermille = packetLossPermille;
        sample.queuedReliable = queuedReliable;
        sample.queuedUnreliable = queuedUnreliable;

        latestPeerSamples_[peerId] = sample;
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

        std::ofstream out{std::string(filePath)};
        if (!out)
            return false;

        const uint64_t reportEndMs = summary_.active ? nowMs() : summary_.endTimestampMs;
        const uint64_t reportDurationMs =
            (reportEndMs >= summary_.beginTimestampMs) ? (reportEndMs - summary_.beginTimestampMs) : 0;

        //  Little formatting lambdas
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
        writeKeyValue("enabled", summary_.enabled ? 1 : 0);
        out << '\n';

        if (!summary_.enabled)
        {
            writeKeyValue("note", "diagnostics disabled for this session");
            writeKeyValue("hint", "run with '--net-diag' to enable diagnostics and generate a full report");
            return out.good();
        }

        writeSectionHeader("Session summary");
        writeKeyValue("active", summary_.active ? 1 : 0);
        writeKeyValue("begin_ms", summary_.beginTimestampMs);
        writeKeyValue("end_ms", reportEndMs);
        writeKeyValue("duration_ms", reportDurationMs);
        writeKeyValue("ticks", summary_.tickCount);
        out << '\n';

        writeSectionHeader("Packet summary");
        writeKeyValue("events_recorded", summary_.eventsRecorded);
        writeKeyValue("events_dropped", summary_.eventsDropped);
        writeKeyValue("packets_sent_attempts", summary_.packetsSent);
        writeKeyValue("packets_recv_attempts", summary_.packetsRecv);
        writeKeyValue("bytes_sent", summary_.packetBytesSent);
        writeKeyValue("bytes_recv", summary_.packetBytesRecv);
        writeKeyValue("packets_sent_failed", summary_.packetsSentFailed);
        writeKeyValue("packets_recv_failed", summary_.packetsRecvFailed);
        writeKeyValue("malformed_packets_recv", summary_.malformedPacketsRecv);
        writeKeyValue("malformed_packet_bytes_recv", summary_.malformedPacketBytesRecv);
        out << '\n';

        writeSectionHeader("Input summary");
        writeKeyValue("stale_input_batches", summary_.staleInputBatches);
        writeKeyValue("input_entries_received_total", summary_.inputEntriesReceivedTotal);
        writeKeyValue("input_entries_accepted", summary_.inputEntriesAccepted);
        writeKeyValue("input_entries_redundant", summary_.inputEntriesRedundant);

        if (summary_.inputEntriesReceivedTotal > 0)
        {
            const double redundancyRatio =
                static_cast<double>(summary_.inputEntriesRedundant) /
                static_cast<double>(summary_.inputEntriesReceivedTotal);

            out << std::fixed << std::setprecision(3);
            writeKeyValue("input_redundancy_ratio", redundancyRatio);
            out << std::defaultfloat;
        }

        writeKeyValue("input_anomalies", summary_.inputAnomalyCount);
        out << '\n';

        writeSectionHeader("Anomalies by type");
        for (std::size_t i = 0; i < summary_.anomaliesByType.size(); ++i)
        {
            const auto type = static_cast<NetInputAnomalyType>(i);
            out << "  - " << toString(type) << ": " << summary_.anomaliesByType[i] << '\n';
        }
        out << '\n';

        writeSectionHeader("Latest peer samples");
        if (latestPeerSamples_.empty())
        {
            out << "  - none\n";
        }
        else
        {
            for (const auto& [peerId, sample] : latestPeerSamples_)
            {
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

                case NetEventType::InputAnomaly:
                    out << " peer=" << static_cast<int>(event.peerId)
                        << " seq=" << event.seq
                        << " anomaly=" << toString(event.anomalyType)
                        << " buttons=" << event.valueA;
                    break;

                case NetEventType::Note:
                    if (event.peerId != 0xFF)
                        out << " peer=" << static_cast<int>(event.peerId);
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
                    if (event.valueA != 0)
                        out << " a=" << event.valueA;
                    if (event.valueB != 0)
                        out << " b=" << event.valueB;
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
        // Only emit packet events for non-OK results.
        return result != NetPacketResult::Ok;
    }

    bool NetDiagnostics::shouldEmitInputAnomalyEvent([[maybe_unused]] const NetInputAnomalyType type)
    {
        // Current live anomaly types are all uncommon enough to keep in recent-event history.
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
