/**
 * @file ServerState.h
 * @brief Authoritative server state model, lifecycle helpers, and fixed-tick simulation API.
 */

#ifndef BOMBERMAN_SERVERSTATE_H
#define BOMBERMAN_SERVERSTATE_H

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <enet/enet.h>

#include "Net/NetCommon.h"
#include "Net/NetDiagnostics.h"
#include "Sim/Movement.h"

namespace bomberman::server
{
    // ----- Server session constants -----

    /**
     * @brief Maximum ENet peers the dedicated server provisions live peer-session storage for.
     *
     * Allow a few extra transport-level peers so overflow clients can reach Hello
     * and receive an explicit ServerFull reject instead of timing out in ENet connect.
     */
    constexpr std::size_t kServerPeerSessionCapacity = net::kMaxPlayers + 4;

    // ----- Input buffering constants -----

    /**
     * @brief Number of authoritative input slots retained per active @ref MatchPlayerState.
     *
     * Currently two times the size of @ref net::kMaxInputBatchSize to allow one full batch
     * of buffered input history in addition to the next expected input sequence.
     */
    constexpr std::size_t kServerInputBufferSize = net::kMaxInputBatchSize * 2;

    /**
     * @brief Maximum distance ahead of lastProcessedInputSeq that a received input is allowed to be.
     *
     * @note Input sequence arithmetic currently assumes a dedicated-server
     * session will not approach `uint32_t` wraparound.
     *
     * @see kServerInputBufferSize
     */
    constexpr uint32_t kMaxBufferedInputLead = kServerInputBufferSize - 1;

    // ----- Server phase -----

    /**
     * @brief High-level dedicated-server phase for future lobby and match flow.
     */
    enum class ServerPhase : uint8_t
    {
        Lobby,         ///< Accepting players and waiting for match start.
        StartingMatch, ///< Transitioning accepted players from lobby into the next match.
        InMatch,       ///< Authoritative gameplay is active.
        EndOfMatch     ///< Match has finished and end-of-round presentation/results are active.
    };

    // ----- Durable player slots -----

    /**
     * @brief Durable per-player seat metadata stored separately from live peer and match state.
     *
     * Slots are keyed by authoritative `playerId` and survive disconnect until
     * they are overwritten by a later occupant of the same seat. Live
     * transport/session ownership stays in @ref PeerSession; this struct keeps
     * only durable identity and recent reconnect/diagnostic metadata.
     */
    struct PlayerSlot
    {
        uint8_t playerId = 0;                               ///< Stable authoritative player seat in [0, net::kMaxPlayers).
        std::string playerName;                             ///< Latest accepted player name associated with this player seat.
        ENetAddress lastKnownAddress{};                     ///< Last accepted or disconnected remote address, kept as reconnect/diagnostic hint only.
        uint32_t acceptedServerTick = 0;                    ///< Server tick when the current or most recent live session was accepted.
        std::optional<uint32_t> lastDisconnectServerTick{}; ///< Set once the current or most recent live session disconnects.
    };

    // ----- Live peer sessions -----

    /**
     * @brief Live transport/session state for one connected ENet peer.
     *
     * `peer->data` always points to this object while the transport peer is
     * connected, including before Hello is accepted. Once the peer is
     * associated with a player seat, @ref playerId is populated.
     */
    struct PeerSession
    {
        ENetPeer* peer = nullptr;           ///< Owning ENet peer for this live transport session.
        std::optional<uint8_t> playerId{};  ///< Authoritative player seat bound to this peer, if Hello has been accepted.
        uint32_t connectedServerTick = 0;   ///< Server tick when the transport session connected.
    };

    // ----- Match-only authoritative state -----

    /**
     * @brief One buffered authoritative input entry for a match player.
     *
     * Slots are indexed by `seq % @ref kServerInputBufferSize`. A slot is
     * reusable after the stored sequence has been consumed or discarded.
     */
    struct InputRingEntry
    {
        uint32_t seq = 0;           ///< Absolute input sequence stored in this slot.
        uint8_t buttons = 0;        ///< Button bitmask for `seq`.
        bool valid = false;         ///< True while this slot still holds an unconsumed input for `seq`.
        bool seenDirect = false;    ///< True when `seq` arrived as the newest entry in a received batch.
        bool seenBuffered = false;  ///< True when `seq` arrived through redundant batch history.
    };

    /**
     * @brief Authoritative in-match state for one accepted player seat.
     *
     * The slot lives inside @ref ServerState::matchPlayers and exists only for
     * currently active in-match state. Live peer ownership stays in
     * @ref PeerSession, and durable player identity lives in @ref PlayerSlot.
     */
    struct MatchPlayerState
    {
        uint8_t playerId = 0;       ///< Stable authoritative player seat in [0, net::kMaxPlayers).
        sim::TilePos pos{};         ///< Authoritative center position in tile-space Q8.

        // ----- Input ring buffer -----
        std::array<InputRingEntry, kServerInputBufferSize> inputRing{}; ///< Indexed by `seq % @ref kServerInputBufferSize`.
        uint32_t lastReceivedInputSeq = 0;  ///< Highest sequence accepted into the ring.
        uint32_t lastProcessedInputSeq = 0; ///< Highest sequence already consumed for simulation.

        uint8_t lastAppliedButtons = 0; ///< Fallback buttons reused when the next sequence misses its deadline.
        uint8_t appliedButtons = 0;     ///< Buttons applied during the current authoritative simulation tick.

        // ----- Consume timeline -----
        bool inputTimelineStarted = false;  ///< True once the fixed-delay consume timeline has been armed.
        uint32_t nextConsumeServerTick = 0; ///< Absolute server tick when `lastProcessedInputSeq + 1` expires.

        // ----- Warning throttles -----
        uint16_t consecutiveTooFarAheadBatches = 0;
        uint16_t consecutiveInputGaps = 0;
        uint32_t nextTooFarAheadWarnTick = 0;
        uint32_t nextGapWarnTick = 0;
    };

    // ----- Shared server session state -----

    /**
     * @brief Long-lived authoritative server state shared across receive and simulation paths.
     *
     * Owns durable player seats, live peer sessions, active in-match state,
     * player-id allocation, the authoritative tile map, and the diagnostics
     * recorder for the active dedicated-server session.
     */
    struct ServerState
    {
        ENetHost* host = nullptr;               ///< Non-owning ENet host that owns transport peers and incoming peer ids for this session.
        ServerPhase phase = ServerPhase::Lobby; ///< Current high-level server flow phase.
        uint32_t serverTick = 0;                ///< Authoritative simulation tick advanced by `simulateServerTick()`.

        uint32_t inputLeadTicks = sim::kDefaultServerInputLeadTicks; ///< Fixed input consume delay in ticks.
        uint32_t snapshotIntervalTicks = sim::kDefaultServerSnapshotIntervalTicks; ///< Snapshot cadence in ticks.

        /** @brief Stable-address live peer sessions indexed by ENet incoming peer id for `peer->data` back-pointers. */
        std::array<std::optional<PeerSession>, kServerPeerSessionCapacity> peerSessions{};
        /** @brief Durable player seats keyed by authoritative player id. See @ref PlayerSlot. */
        std::array<std::optional<PlayerSlot>, net::kMaxPlayers> playerSlots{};
        /** @brief Stable-address active in-match state keyed by authoritative player id. */
        std::array<std::optional<MatchPlayerState>, net::kMaxPlayers> matchPlayers{};

        std::array<uint8_t, net::kMaxPlayers> playerIdPool{}; ///< Sorted free-list of available player ids.
        uint8_t playerIdPoolSize = 0; ///< Number of valid entries currently stored in `playerIdPool`.

        // TODO: Promote this from per-session to per-round once lobby/match flow exists, while keeping the CLI override path.
        uint32_t mapSeed = 0; ///< Seed used to generate the current authoritative tile map for this round.
        sim::TileMap tiles{}; ///< Authoritative collision map shared by all server-side movement steps.

        // TODO: Feed phase transitions and derived idle state into NetDiagnostics once lobby flow exists.
        net::NetDiagnostics diag; ///< Diagnostics recorder for this session.
    };

    // ----- Dispatcher context -----

    /** @brief Per-dispatch context passed through the typed packet handlers. */
    struct PacketDispatchContext
    {
        ServerState& state;
        ENetPeer* peer = nullptr;               ///< ENet peer currently being serviced for this dispatch.
        net::NetDiagnostics* diag = nullptr;    ///< Non-owning pointer to the diagnostics recorder.
        net::NetPacketResult receiveResult = net::NetPacketResult::Rejected; ///< Final packet classification recorded after handler dispatch.
        std::optional<uint8_t> recordedPlayerId{}; ///< Player seat known for diagnostics, or `std::nullopt` for pre-handshake traffic.
    };

    // ----- Session lifecycle -----

    /**
     * @brief Resets a `ServerState` for a new dedicated-server session.
     *
     * Rebuilds player-id allocation, clears live peer sessions, player slots,
     * and active match state, generates the authoritative tile map, and
     * starts a new diagnostics session.
     */
    void initServerState(ServerState& state,
                         ENetHost* host,
                         bool diagEnabled = false,
                         bool overrideMapSeed = false,
                         uint32_t mapSeed = 0,
                         uint32_t inputLeadTicks = sim::kDefaultServerInputLeadTicks,
                         uint32_t snapshotIntervalTicks = sim::kDefaultServerSnapshotIntervalTicks);

    // ----- Player-id allocation -----

    /** @brief Returns the lowest available playerId and removes it from the free pool. */
    [[nodiscard]]
    std::optional<uint8_t> acquirePlayerId(ServerState& state);

    /** @brief Returns a playerId to the free pool while keeping pool order deterministic. */
    void releasePlayerId(ServerState& state, uint8_t playerId);

    // ----- Peer-session binding and lookup -----
    /**
     * @brief Binds a connected ENet peer to stable live peer-session storage.
     *
     * Creates or resets the live @ref PeerSession for this transport peer and
     * writes the stable back-pointer into `peer.data`.
     *
     * @return Pointer to the bound peer session, or `nullptr` if the peer id
     * is outside @ref kServerPeerSessionCapacity.
     */
    [[nodiscard]]
    PeerSession* bindPeerSession(ServerState& state, ENetPeer& peer);

    /** @brief Returns the live peer session referenced by `peer->data`, if any. */
    [[nodiscard]]
    PeerSession* getPeerSession(ENetPeer* peer);
    [[nodiscard]]
    const PeerSession* getPeerSession(const ENetPeer* peer);

    /** @brief Returns the live peer session currently bound to `playerId`, if any. */
    [[nodiscard]]
    PeerSession* findPeerSessionByPlayerId(ServerState& state, uint8_t playerId);
    [[nodiscard]]
    const PeerSession* findPeerSessionByPlayerId(const ServerState& state, uint8_t playerId);

    /**
     * @brief Commits an accepted peer session into a durable player slot.
     *
     * Updates the @ref PlayerSlot for `playerId` and binds the authoritative
     * seat to the provided @ref PeerSession. Match state creation stays
     * separate so future lobby flow can accept peers before a match starts.
     */
    void acceptPeerSession(ServerState& state, PeerSession& session, uint8_t playerId, std::string_view playerName);

    /**
     * @brief Releases the live peer session bound to `peer`, if any.
     *
     * Clears `peer.data`, updates the durable @ref PlayerSlot, destroys any
     * active @ref MatchPlayerState for the same player seat, and returns the
     * freed player id to the pool immediately.
     *
     * @note This preserves the current no-reconnect behavior. Future
     * reconnect work should extend this helper instead of reintroducing
     * disconnect cleanup logic in call sites.
     *
     * @return Freed player id on success, or `std::nullopt` if the peer had no accepted player seat attached.
     */
    [[nodiscard]]
    std::optional<uint8_t> releasePeerSession(ServerState& state, ENetPeer& peer);

    // ----- Match player state lifecycle -----
    /**
     * @brief Creates active in-match authoritative state for one player seat.
     *
     * Replaces any existing @ref MatchPlayerState in the same slot.
     */
    void createMatchPlayerState(ServerState& state, uint8_t playerId, sim::TilePos spawnPos);

    /**
     * @brief Destroys active in-match authoritative state for one player seat, if present.
     */
    void destroyMatchPlayerState(ServerState& state, uint8_t playerId);

    // ----- Fixed-tick simulation -----

    /**
     * @brief Advances the authoritative server by one fixed simulation tick.
     *
     * @note Per tick, the server resolves each match player's next scheduled input,
     * steps authoritative movement, queues owner corrections, and optionally
     * broadcasts a snapshot for all active @ref MatchPlayerState entries.
     */
    void simulateServerTick(ServerState& state);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSTATE_H
