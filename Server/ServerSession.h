#ifndef BOMBERMAN_SERVERSESSION_H
#define BOMBERMAN_SERVERSESSION_H

#include <array>
#include <cstdint>
#include <optional>

#include <enet/enet.h>

#include "Const.h"
#include "Net/NetDiagnostics.h"
#include "Net/NetCommon.h"
#include "Sim/Movement.h"

namespace bomberman::server
{
    /** @brief Typed receive outcome used to classify packet handling after header parse. */
    enum class ReceiveDispatchResult : uint8_t
    {
        Ok,
        Rejected,
        Malformed
    };

    /** @brief Server-side input ring buffer size per client. Must be power of two. */
    constexpr std::size_t kServerInputBufferSize = 32;

    /** @brief Maximum distance ahead of lastProcessedInputSeq that a received input is allowed to be. */
    constexpr uint32_t kInputWindowAhead = kServerInputBufferSize - 1;

    /** @brief A single slot in the per-client input ring buffer. */
    struct InputRingEntry
    {
        uint32_t seq     = 0;
        uint8_t  buttons = 0;
        bool     valid   = false; // Indicates whether this slot contains valid, unconsumed input.
        bool     seenDirect = false;   // True if this seq was seen as the newest entry in a received batch.
        bool     seenBuffered = false; // True if this seq was first/also seen via redundant batch history.
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
        uint32_t lastProcessedInputSeq = 0;

        uint8_t previousButtons = 0;   ///< Fallback buttons when input is missing.
        uint8_t currentButtons  = 0;   ///< Buttons used for the current simulation tick.

        // ---- Input timeline state ----
        bool inputTimelineStarted = false;    ///< True once this client has armed its fixed-delay consume timeline.
        uint32_t nextConsumeServerTick = 0;   ///< Absolute server tick when the next input sequence reaches consume deadline.

        // ---- Input warning state ----
        uint16_t consecutiveTooFarAheadBatches = 0;
        uint16_t consecutiveInputGaps = 0;
        uint32_t nextTooFarAheadWarnTick = 0;
        uint32_t nextGapWarnTick = 0;
    };

    /** @brief Long-lived server state shared across all dispatch calls. */
    struct ServerState
    {
        ENetHost* host = nullptr;
        uint32_t serverTick = 0;
        uint32_t inputLeadTicks = static_cast<uint32_t>(sim::kDefaultServerInputLeadTicks);
        uint32_t snapshotIntervalTicks = static_cast<uint32_t>(sim::kDefaultServerSnapshotIntervalTicks);

        /** @brief Stable-address client storage indexed by playerId. */
        std::array<std::optional<ClientState>, net::kMaxPlayers> clients{};

        uint8_t playerIdPool[net::kMaxPlayers]{}; ///< Pool of available player IDs [0, kMaxPlayers).
        uint8_t playerIdPoolSize = 0;

        uint32_t mapSeed = 0;
        sim::TileMap tiles{};

        net::NetDiagnostics diag; ///<  Diagnostics recorder for this session
    };

    /** @brief Initialises a ServerState to a clean pre-game state. */
    void initServerState(ServerState& state, ENetHost* host, bool diagEnabled = false,
                         bool overrideMapSeed = false, uint32_t mapSeed = 0,
                         uint32_t inputLeadTicks = static_cast<uint32_t>(sim::kDefaultServerInputLeadTicks),
                         uint32_t snapshotIntervalTicks = static_cast<uint32_t>(sim::kDefaultServerSnapshotIntervalTicks));

    /** @brief Returns the lowest available playerId and removes it from the free pool. */
    [[nodiscard]]
    std::optional<uint8_t> acquirePlayerId(ServerState& state);

    /** @brief Returns a playerId to the free pool while keeping pool order deterministic. */
    void releasePlayerId(ServerState& state, uint8_t playerId);

    /** @brief Per-dispatch context passed to handlers. */
    struct ServerContext
    {
        ServerState&          state;
        ENetPeer*             peer = nullptr;
        net::NetDiagnostics*  diag = nullptr; ///< Non-owning pointer, null-checked at each call site.
        ReceiveDispatchResult receiveResult = ReceiveDispatchResult::Rejected;
        uint8_t diagPeerId = 0xFF; ///< Gameplay peer/player ID if known, otherwise invalid sentinel.
    };

    /** @brief Advances the server simulation by one tick. */
    void simulateServerTick(ServerState& state);

} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSESSION_H
