#ifndef BOMBERMAN_NET_NETDIAGCONFIG_H
#define BOMBERMAN_NET_NETDIAGCONFIG_H

#include <cstddef>
#include <cstdint>

#include "Sim/SimConfig.h"

/**
 * @brief Diagnostics policy constants shared by client- and server-side networking code.
 */

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Storage policy ============================================================================================
    // =================================================================================================================

    /** @brief Maximum number of recent events retained in the NetDiagnostics ring buffer. */
    constexpr std::size_t kRecentEventCapacity = 256;

    // =================================================================================================================
    // ===== Runtime Reporting cadence =================================================================================
    // =================================================================================================================

    /** @brief Per-batch input sequence log interval. */
    constexpr uint32_t kInputBatchLogIntervalTicks = static_cast<uint32_t>(sim::kTickRate) * 2u; ///< Every 2 seconds.

    /** @brief Server snapshot summary log interval in server simulation ticks. */
    constexpr uint32_t kServerSnapshotLogIntervalTicks = static_cast<uint32_t>(sim::kTickRate) * 2u; ///< Every 2 seconds.

    /** @brief ENet peer transport sample interval in server simulation ticks. */
    constexpr uint32_t kPeerSampleTicks = static_cast<uint32_t>(sim::kTickRate); ///< Once per second.

    // =================================================================================================================
    // ===== Anomaly detection policy ==================================================================================
    // =================================================================================================================

    /** @brief Consecutive anomaly streak that triggers a WARN log (ahead drops and input gaps). */
    constexpr uint16_t kRepeatedInputWarnThreshold = 6;

    /** @brief Minimum tick spacing between repeated anomaly WARN log lines. */
    constexpr uint32_t kRepeatedInputWarnCooldownTicks = static_cast<uint32_t>(sim::kTickRate) * 2u;

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETDIAGCONFIG_H
