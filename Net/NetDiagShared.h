#ifndef BOMBERMAN_NET_NETDIAGSHARED_H
#define BOMBERMAN_NET_NETDIAGSHARED_H

#include <cstdint>
#include <string>

/**
 * @file NetDiagShared.h
 * @brief Shared diagnostics event/sample types used by both server and client recorders.
 */

namespace bomberman::net
{
    inline constexpr uint32_t kDiagnosticsReportVersion = 1;

    /** @brief High-level kinds of recent events captured during a diagnostics session. */
    enum class NetEventType : uint8_t
    {
        Unknown,
        SessionBegin,
        SessionEnd,
        PeerLifecycle,
        PacketSent,
        PacketRecv,
        Simulation,
        Flow
    };

    /** @brief Peer lifecycle milestones emitted by multiplayer networking flows. */
    enum class NetPeerLifecycleType : uint8_t
    {
        TransportConnected,
        PlayerAccepted,
        PeerRejected,
        PeerDisconnected,
        TransportDisconnectedBeforeHandshake
    };

    /** @brief Packet travel direction recorded in a recent packet event. */
    enum class NetPacketDirection : uint8_t
    {
        Outgoing,
        Incoming
    };

    /** @brief Diagnostics classification for one packet attempt or receive path outcome. */
    enum class NetPacketResult : uint8_t
    {
        Ok,
        Dropped,
        Rejected,
        Malformed
    };

    /** @brief Simulation/input-timeline events worth retaining in recent-event history. */
    enum class NetSimulationEventType : uint8_t
    {
        Gap,
        BufferedDeadlineRecovery,
        RoundEnded
    };

    /** @brief Discrete diagnostics event stored in a recent-event ring buffer. */
    struct NetEvent
    {
        NetEventType type = NetEventType::Unknown;
        uint64_t timestampMs = 0; ///< Monotonic timestamp. Zero means "stamp on record".

        NetPacketDirection packetDirection = NetPacketDirection::Outgoing; ///< Valid for packet events.
        NetPacketResult packetResult = NetPacketResult::Ok; ///< Valid for packet events.
        NetPeerLifecycleType lifecycleType = NetPeerLifecycleType::TransportConnected; ///< Valid for lifecycle events.
        NetSimulationEventType simulationType = NetSimulationEventType::Gap; ///< Valid for simulation events.

        uint8_t peerId = 0xFF; ///< Gameplay player id when known, otherwise 0xFF.
        uint8_t channelId = 0xFF; ///< Raw ENet channel id for packet events.
        uint8_t msgType = 0; ///< Raw @ref EMsgType value for packet events.

        uint32_t seq = 0; ///< Input sequence or other event-specific sequence value.
        uint32_t detailA = 0; ///< Event-specific numeric detail.
        uint32_t detailB = 0; ///< Event-specific numeric detail.

        std::string note; ///< Optional short human-readable note for reports.
    };

    /** @brief Latest sampled transport health values for a single gameplay player id. */
    struct NetPeerTransportSample
    {
        uint8_t peerId = 0xFF;
        uint64_t timestampMs = 0;
        uint32_t rttMs = 0;
        uint32_t rttVarianceMs = 0;
        uint32_t packetLossPermille = 0;
        uint32_t queuedReliable = 0;
        uint32_t queuedUnreliable = 0;
    };

    /** @brief Aggregate input-continuity facts for one authoritative gameplay player id. */
    struct NetPeerContinuitySummary
    {
        uint8_t peerId = 0xFF;
        uint64_t timestampMs = 0;
        uint64_t directDeadlineConsumes = 0;
        uint64_t simulationGaps = 0;
        uint64_t bufferedDeadlineRecoveries = 0;
        uint32_t lastReceivedInputSeq = 0;
        uint32_t lastProcessedInputSeq = 0;
    };

    /** @brief Recent-event dedupe state keyed by semantic event signature. */
    struct RecentEventRepeatState
    {
        uint64_t lastEmittedTimestampMs = 0;
    };
} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETDIAGSHARED_H
