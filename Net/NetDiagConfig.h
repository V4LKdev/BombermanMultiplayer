#ifndef BOMBERMAN_NET_NETDIAGCONFIG_H
#define BOMBERMAN_NET_NETDIAGCONFIG_H

#include <cstddef>
#include <cstdint>

#include "Sim/SimConfig.h"

/**
 * @file NetDiagConfig.h
 * @brief Shared policy constants for lightweight multiplayer diagnostics.
 */

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Recent-event storage policy ===============================================================================
    // =================================================================================================================

    /** @brief Maximum number of recent events retained in the NetDiagnostics ring buffer. */
    constexpr std::size_t kRecentEventCapacity = 256;

    /** @brief Minimum time between repeated recent events with the same semantic signature. */
    constexpr uint64_t kRecentEventDedupeCooldownMs = 1000;

    // =================================================================================================================
    // ===== Current server runtime cadence ============================================================================
    // =================================================================================================================

    /** @brief Accepted-input debug log cadence in authoritative server ticks. */
    constexpr uint32_t kServerInputBatchLogIntervalTicks = static_cast<uint32_t>(sim::kTickRate) * 2u; ///< Every 2 seconds.

    /** @brief Snapshot summary debug log cadence in authoritative server ticks. */
    constexpr uint32_t kServerSnapshotLogIntervalTicks = static_cast<uint32_t>(sim::kTickRate) * 2u; ///< Every 2 seconds.

    /** @brief ENet transport-health sampling cadence in authoritative server ticks. */
    constexpr uint32_t kPeerTransportSampleTicks = static_cast<uint32_t>(sim::kTickRate); ///< Once per second.

    // =================================================================================================================
    // ===== Repeated anomaly warn throttling ==========================================================================
    // =================================================================================================================

    /** @brief Consecutive anomaly streak that triggers a WARN log for repeated ahead drops or input gaps. */
    constexpr uint16_t kRepeatedInputWarnThreshold = 6;

    /** @brief Minimum authoritative tick spacing between repeated anomaly WARN lines for the same peer. */
    constexpr uint32_t kRepeatedInputWarnCooldownTicks = static_cast<uint32_t>(sim::kTickRate) * 2u;

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETDIAGCONFIG_H
