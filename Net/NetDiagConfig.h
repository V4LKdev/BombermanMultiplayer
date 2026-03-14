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
    // ===== Master flags ==============================================================================================
    // =================================================================================================================

    /** @brief Enables diagnostics recording for the server session. */
    constexpr bool kDiagEnabledServer = true;

    // =================================================================================================================
    // ===== Storage policy ============================================================================================
    // =================================================================================================================

    /** @brief Maximum number of recent events retained in the NetDiagnostics ring buffer. */
    constexpr std::size_t kRecentEventCapacity = 256;

    // =================================================================================================================
    // ===== Reporting cadence =========================================================================================
    // =================================================================================================================

    /**
     * @brief Aggregated input diagnostics summary log interval in server simulation ticks.
     *
     * At the end of each interval the per-client counters are logged and reset.
     */
    constexpr uint32_t kInputDiagReportTicks = static_cast<uint32_t>(sim::kTickRate) * 4u; ///<  4 seconds at 60 ticks/s

    /** @brief Per-batch input sequence log interval. */
    constexpr uint32_t kInputBatchLogEveryN = static_cast<uint32_t>(sim::kTickRate) * 2u;

    /** @brief Server snapshot summary log interval in server simulation ticks. */
    constexpr uint32_t kServerSnapshotLogEveryN = static_cast<uint32_t>(sim::kTickRate) * 2u;

    // =================================================================================================================
    // ===== Anomaly detection policy ==================================================================================
    // =================================================================================================================

    /** @brief Consecutive anomaly streak that triggers a WARN log (ahead drops and input gaps). */
    constexpr uint16_t kRepeatedInputWarnThreshold = 6;

    /** @brief Minimum tick spacing between repeated anomaly WARN log lines. */
    constexpr uint32_t kRepeatedInputWarnCooldownTicks = static_cast<uint32_t>(sim::kTickRate) * 2u;

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETDIAGCONFIG_H
