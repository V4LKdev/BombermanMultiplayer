#ifndef BOMBERMAN_SERVERSESSION_H
#define BOMBERMAN_SERVERSESSION_H

#include <array>
#include <cstdint>
#include <optional>

#include <enet/enet.h>

#include "Const.h"
#include "Net/NetCommon.h"
#include "Sim/Movement.h"

namespace bomberman::server
{

    /** @brief Server-side input ring buffer size per client. Must be power of two. */
    constexpr std::size_t kServerInputBufferSize = 32;

    /** @brief Maximum distance ahead of lastConsumedInputSeq that a received input is allowed to be. */
    constexpr uint32_t kInputWindowAhead = kServerInputBufferSize - 1;

    /** @brief Aggregated input diagnostics reporting cadence in simulation ticks. */
    constexpr uint32_t kInputDiagReportTicks = static_cast<uint32_t>(sim::kTickRate) * 4u;

    /** @brief Warning threshold for repeated ahead drops or input gaps before emitting a WARN line. */
    constexpr uint16_t kRepeatedInputWarnThreshold = 6;

    /** @brief Minimum spacing between repeated input WARN logs. */
    constexpr uint32_t kRepeatedInputWarnCooldownTicks = static_cast<uint32_t>(sim::kTickRate) * 2u;

    /** @brief Server snapshot debug summary cadence in ticks. */
    constexpr uint32_t kServerSnapshotLogEveryN = static_cast<uint32_t>(sim::kTickRate) * 2u;

    /** @brief A single slot in the per-client input ring buffer. */
    struct InputRingEntry
    {
        uint32_t seq     = 0;
        uint8_t  buttons = 0;
        bool     valid   = false; // Indicates whether this slot contains valid, unconsumed input.
    };

    /** @brief Authoritative per-client state on the server. */
    struct ClientState
    {
        uint8_t playerId = 0;
        ENetPeer* peer = nullptr;      ///< Owning ENet peer.
        sim::TilePos pos{};            ///< Authoritative position in tile-Q8.

        // ---- Input ring buffer ----
        InputRingEntry inputRing[kServerInputBufferSize]{}; ///< Indexed by seq % kServerInputBufferSize.
        uint32_t lastReceivedInputSeq = 0;
        uint32_t lastConsumedInputSeq = 0;

        uint8_t previousButtons = 0;   ///< Fallback buttons when input is missing.
        uint8_t currentButtons  = 0;   ///< Buttons used for the current simulation tick.

        // ---- Input diagnostics ----
        uint32_t lateDrops = 0;
        uint32_t aheadDrops = 0;
        uint32_t inputGaps = 0;
        uint64_t inputLeadSum = 0;
        uint32_t inputLeadSamples = 0;
        uint32_t nextInputDiagTick = kInputDiagReportTicks;
        uint16_t consecutiveAheadDropBatches = 0;
        uint16_t consecutiveInputGaps = 0;
        uint32_t nextAheadWarnTick = 0;
        uint32_t nextGapWarnTick = 0;
    };

    /** @brief Long-lived server state shared across all dispatch calls. */
    struct ServerState
    {
        ENetHost* host = nullptr;
        uint32_t serverTick = 0;

        /** @brief Stable-address client storage indexed by playerId. */
        std::array<std::optional<ClientState>, net::kMaxPlayers> clients{};

        /** @brief Free player ID pool. Pop to allocate, push to free. */
        uint8_t playerIdPool[net::kMaxPlayers]{};
        uint8_t playerIdPoolSize = 0;

        uint32_t mapSeed = 0;
        sim::TileMap tiles{};
    };

    /** @brief Initialises a ServerState to a clean pre-game state. */
    void initServerState(ServerState& state, ENetHost* host, bool overrideMapSeed = false, uint32_t mapSeed = 0);

    /** @brief Per-dispatch context passed to handlers. */
    struct ServerContext
    {
        ServerState& state;
        ENetPeer*    peer;
    };

    /** @brief Advances the server simulation by one tick. */
    void simulateServerTick(ServerState& state);

    /** @brief Builds a `MsgSnapshot` from the current `ServerState` for broadcasting to clients. */
    net::MsgSnapshot buildSnapshot(const ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSESSION_H
