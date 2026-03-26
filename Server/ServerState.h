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
    constexpr std::size_t kServerInputBufferSize = static_cast<std::size_t>(net::kMaxInputBatchSize) * 2u;

    /**
     * @brief Maximum distance ahead of lastProcessedInputSeq that a received input is allowed to be.
     *
     * @note Input sequence arithmetic currently assumes a dedicated-server
     * session will not approach `uint32_t` wraparound.
     *
     * @see kServerInputBufferSize
     */
    constexpr uint32_t kMaxBufferedInputLead = kServerInputBufferSize - 1;

    // ----- Match gameplay-state constants -----

    /**
     * @brief Maximum number of simultaneously active bombs the server stores for one match.
     *
     * One bomb occupies one tile, so using the full tile-map area as the
     * storage bound keeps the representation future-proof for higher per-player
     * bomb caps without forcing a protocol or state-layout change later.
     */
    constexpr std::size_t kServerBombCapacity = static_cast<std::size_t>(tileArrayWidth) *
                                                static_cast<std::size_t>(tileArrayHeight);

    // ----- Server phase -----

    /**
     * @brief High-level dedicated-server phase for the current lobby and match flow.
     */
    enum class ServerPhase : uint8_t
    {
        Lobby,         ///< Accepting players and waiting for match start.
        LobbyCountdown,///< All required players are ready and the lobby countdown is running.
        StartingMatch, ///< Transitioning accepted players from lobby into the next match.
        InMatch,       ///< Authoritative gameplay is active.
        EndOfMatch     ///< Match has finished and end-of-round presentation/results are active.
    };

    // ----- Accepted player metadata -----

    /**
     * @brief Current-session metadata for one accepted player assignment.
     *
     * Slots are keyed by authoritative `playerId` while that assignment is
     * active. Live transport/session ownership stays in @ref PeerSession, and
     * disconnect ends the assignment immediately.
     */
    struct PlayerSlot
    {
        uint8_t playerId = 0;   ///< Stable authoritative player seat in [0, net::kMaxPlayers) for the current assignment.
        std::string playerName; ///< Latest accepted player name for this active assignment.
        bool ready = false;     ///< Passive lobby ready toggle owned by the current assignment.
        uint8_t wins = 0;       ///< Server-owned round wins for the current assignment only.
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
     * @ref PeerSession, and current accepted-player metadata lives in @ref PlayerSlot.
     */
    struct MatchPlayerState
    {
        uint8_t playerId = 0;       ///< Stable authoritative player seat in [0, net::kMaxPlayers).
        sim::TilePos pos{};         ///< Authoritative center position in tile-space Q8.
        bool alive = true;          ///< True while this player is alive in the current round.
        bool inputLocked = false;   ///< True while the server disallows gameplay input and movement for this player.
        uint8_t activeBombCount = 0; ///< Number of currently active bombs owned by this player.
        uint8_t maxBombs = sim::kDefaultPlayerMaxBombs; ///< Current bomb-cap loadout.
        uint8_t bombRange = sim::kDefaultPlayerBombRange; ///< Current blast-radius loadout.

        // ----- Input ring buffer -----
        std::array<InputRingEntry, kServerInputBufferSize> inputRing{}; ///< Indexed by `seq % @ref kServerInputBufferSize`.
        uint32_t lastReceivedInputSeq = 0;  ///< Highest sequence accepted into the ring.
        uint32_t lastProcessedInputSeq = 0; ///< Highest sequence already consumed for simulation.

        uint8_t lastAppliedButtons = 0; ///< Fallback buttons reused when the next sequence misses its deadline.
        uint8_t appliedButtons = 0;     ///< Buttons applied during the current authoritative simulation tick.
        uint8_t previousTickButtons = 0; ///< Buttons applied during the previous authoritative simulation tick.

        // ----- Consume timeline -----
        bool inputTimelineStarted = false;  ///< True once the fixed-delay consume timeline has been armed.
        uint32_t nextConsumeServerTick = 0; ///< Absolute server tick when `lastProcessedInputSeq + 1` expires.

        // ----- Warning throttles -----
        uint16_t consecutiveTooFarAheadBatches = 0;
        uint16_t consecutiveInputGaps = 0;
        uint32_t nextTooFarAheadWarnTick = 0;
        uint32_t nextGapWarnTick = 0;
    };

    /**
     * @brief Tile cell coordinate used by authoritative bomb state.
     *
     * Cell coordinates are stored in tile-map space, not world-pixel space, so
     * later explosion propagation and tile destruction can work directly
     * against @ref ServerState::tiles.
     */
    struct BombCell
    {
        uint8_t col = 0;
        uint8_t row = 0;
    };

    /**
     * @brief Authoritative state for one active bomb in the current match.
     *
     * Bombs snapshot their owner-derived gameplay properties at placement
     * time, so player changes do not retroactively affect already-placed bombs.
     * Once placed, they remain world state until they resolve or the round
     * explicitly clears them.
     */
    struct BombState
    {
        uint8_t ownerId = 0;      ///< Player seat that placed the bomb.
        BombCell cell{};          ///< Tile-map cell currently occupied by the bomb.
        uint32_t placedTick = 0;  ///< Authoritative server tick when the bomb was accepted.
        uint32_t explodeTick = 0; ///< Authoritative server tick when the bomb should detonate.
        uint8_t radius = 0;       ///< Explosion radius snapped from the owner's loadout at placement time.
    };

    // ----- Shared server session state -----

    /**
     * @brief Long-lived authoritative server state shared across receive and simulation paths.
     *
     * Owns accepted player metadata, live peer sessions, active in-match state,
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
        /** @brief Active accepted-player metadata keyed by authoritative player id. See @ref PlayerSlot. */
        std::array<std::optional<PlayerSlot>, net::kMaxPlayers> playerSlots{};
        /** @brief Stable-address active in-match state keyed by authoritative player id. */
        std::array<std::optional<MatchPlayerState>, net::kMaxPlayers> matchPlayers{};
        /** @brief Active authoritative bombs for the current round. */
        std::array<std::optional<BombState>, kServerBombCapacity> bombs{};

        std::array<uint8_t, net::kMaxPlayers> playerIdPool{}; ///< Sorted free-list of available player ids, released immediately on disconnect.
        uint8_t playerIdPoolSize = 0; ///< Number of valid entries currently stored in `playerIdPool`.

        std::optional<uint32_t> fixedMapSeedOverride{}; ///< Fixed map seed reused for every round when the server was started with `--seed`.
        uint32_t mapSeed = 0; ///< Seed prepared for the current or next authoritative round tile map.
        sim::TileMap tiles{}; ///< Authoritative collision map shared by all server-side movement steps.
        uint32_t currentMatchId = 0; ///< Current authoritative match identifier, or 0 while the server is idle in the lobby.
        uint32_t nextMatchId = 1; ///< Monotonic match-id generator used when the next round begins.
        uint32_t currentLobbyCountdownPlayerMask = 0; ///< Bitmask of players currently participating in the lobby countdown.
        uint32_t currentLobbyCountdownDeadlineTick = 0; ///< Tick at which the lobby countdown should commit into the next round start.
        uint8_t currentLobbyCountdownLastBroadcastSecond = 0; ///< Last whole countdown second already broadcast through LobbyState.
        uint32_t currentMatchPlayerMask = 0; ///< Bitmask of player ids participating in the current round start or active round.
        uint32_t currentMatchLoadedMask = 0; ///< Bitmask of participants that have acknowledged `MatchLoaded`.
        uint32_t currentMatchStartDeadlineTick = 0; ///< Tick deadline for `StartingMatch` load acknowledgements, or 0 when idle.
        uint32_t currentMatchGoShowTick = 0; ///< Tick at which gameplay scenes should display `GO!` for the current match.
        uint32_t currentMatchUnlockTick = 0; ///< Tick at which gameplay input unlocks for the current match.
        uint32_t currentEndOfMatchReturnTick = 0; ///< Tick deadline for automatic end-of-match return to the lobby, or 0 when inactive.
        std::optional<uint8_t> roundWinnerPlayerId{}; ///< Winner of the current or most recent round, if one exists.
        bool roundEndedInDraw = false; ///< True when the current or most recent round ended with no surviving player.

        // TODO: Feed phase transitions and derived idle state into NetDiagnostics.
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

    /** @brief Chooses the seed that should be used for the next authoritative round map. */
    void rollNextRoundMapSeed(ServerState& state);

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
     * @brief Commits an accepted peer session into active player metadata.
     *
     * Creates the active @ref PlayerSlot for `playerId` and binds the
     * authoritative assignment to the provided @ref PeerSession. Match state
     * creation stays separate so accepted peers can remain in the lobby before
     * a match starts.
     */
    void acceptPeerSession(ServerState& state, PeerSession& session, uint8_t playerId, std::string_view playerName);

    /**
     * @brief Releases the live peer session bound to `peer`, if any.
     *
     * Clears `peer.data`, destroys the active @ref PlayerSlot and any active
     * @ref MatchPlayerState for the same player seat, and returns the freed
     * player id to the pool immediately.
     *
     * @note Disconnect ends the current player assignment. Any later join is a
     * fresh admission from the current free-id pool.
     *
     * @return Freed player id on success, or `std::nullopt` if the peer had no accepted player seat attached.
     */
    [[nodiscard]]
    std::optional<uint8_t> releasePeerSession(ServerState& state, ENetPeer& peer);

    // ----- Match player state lifecycle -----
    /**
     * @brief Creates active in-match authoritative state for one player seat.
     *
     * Replaces any existing @ref MatchPlayerState in the same slot and seeds
     * the starting position from the shared default spawn-slot table.
     */
    void createMatchPlayerState(ServerState& state, uint8_t playerId);

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
