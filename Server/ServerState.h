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
#include "Sim/PowerupConfig.h"

/**
 * @brief Authoritative dedicated-server state, match flow, and fixed-tick simulation support.
 */
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

    /** @brief Maximum number of hidden/revealed round powerups the server tracks at once. */
    constexpr std::size_t kServerPowerupCapacity = sim::kPowerupsPerRound;

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
        uint8_t playerId = 0;
        std::string playerName;
        bool ready = false;
        uint8_t wins = 0;
    };

    /**
     * @brief Bounded reconnect cache for one recently disconnected player seat.
     *
     * This is only used for simple lobby-only score reclaim. If the same
     * player name rejoins before another admission takes the freed seat, the
     * server can restore the prior `playerId` and carried win count.
     */
    struct DisconnectedPlayerReclaim
    {
        std::string playerName;
        uint8_t wins = 0;
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
        ENetPeer* peer = nullptr;
        std::optional<uint8_t> playerId{};  ///< Populated once Hello is accepted.
        uint32_t connectedServerTick = 0;
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
        uint32_t seq = 0;
        uint8_t buttons = 0;
        bool valid = false;        ///< True while this slot still holds an unconsumed input.
        bool seenDirect = false;   ///< Seen as the newest entry in a received batch.
        bool seenBuffered = false; ///< Seen through redundant batch history.
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
        uint8_t playerId = 0;
        sim::TilePos pos{};
        bool alive = true;
        bool inputLocked = false;
        uint8_t activeBombCount = 0;
        uint8_t maxBombs = sim::kDefaultPlayerMaxBombs;
        uint8_t bombRange = sim::kDefaultPlayerBombRange;
        uint32_t invincibleUntilTick = 0;
        uint32_t speedBoostUntilTick = 0;
        uint32_t bombRangeBoostUntilTick = 0;
        uint32_t maxBombsBoostUntilTick = 0;

        // ----- Input ring buffer -----
        std::array<InputRingEntry, kServerInputBufferSize> inputRing{}; ///< Indexed by `seq % @ref kServerInputBufferSize`.
        uint32_t lastReceivedInputSeq = 0;
        uint32_t lastProcessedInputSeq = 0;

        uint8_t lastAppliedButtons = 0; ///< Reused when the next sequence misses its deadline.
        uint8_t appliedButtons = 0;
        uint8_t previousTickButtons = 0;

        // ----- Consume timeline -----
        bool inputTimelineStarted = false;  ///< True once the fixed-delay consume timeline has been armed.
        uint32_t nextConsumeServerTick = 0;

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
        uint8_t ownerId = 0;
        BombCell cell{};
        uint32_t placedTick = 0;
        uint32_t explodeTick = 0;
        uint8_t radius = 0; ///< Snapped from the owner's loadout at placement time.
    };

    /**
     * @brief Authoritative state for one round-scoped hidden or revealed powerup.
     */
    struct PowerupState
    {
        sim::PowerupType type = sim::PowerupType::SpeedBoost;
        BombCell cell{};
        bool revealed = false;     ///< True once the covering brick has been destroyed.
        uint32_t revealedTick = 0;
    };

    // ----- Shared server session state -----

    /**
     * @brief Long-lived authoritative server state shared across receive and simulation paths.
     *
     * The state is layered in three parts:
     * - live transport sessions
     * - accepted lobby/player-seat metadata
     * - active round flow and authoritative match state
     */
    struct ServerState
    {
        // ----- Session host and runtime config -----

        ENetHost* host = nullptr;               ///< Non-owning ENet host for this session.
        ServerPhase phase = ServerPhase::Lobby; ///< Current high-level server flow phase.
        uint32_t serverTick = 0;                ///< Authoritative simulation tick advanced by `simulateServerTick()`.

        uint32_t inputLeadTicks = sim::kDefaultServerInputLeadTicks;
        uint32_t snapshotIntervalTicks = sim::kDefaultServerSnapshotIntervalTicks;
        bool powersEnabled = true; ///< Round powerups are seeded and replicated.

        // ----- Live transport sessions -----

        /** @brief Stable-address live peer sessions indexed by ENet incoming peer id. */
        std::array<std::optional<PeerSession>, kServerPeerSessionCapacity> peerSessions{};

        // ----- Accepted players and lobby persistence -----

        /** @brief Active accepted-player metadata keyed by player id. See @ref PlayerSlot. */
        std::array<std::optional<PlayerSlot>, net::kMaxPlayers> playerSlots{};
        /** @brief Last disconnected occupant per player id for bounded lobby-only reconnect reclaim. */
        std::array<std::optional<DisconnectedPlayerReclaim>, net::kMaxPlayers> disconnectedPlayerReclaims{};

        // ----- Active round authoritative world state -----

        /** @brief Stable-address active in-match state keyed by player id. */
        std::array<std::optional<MatchPlayerState>, net::kMaxPlayers> matchPlayers{};
        /** @brief Active bombs for the current round. */
        std::array<std::optional<BombState>, kServerBombCapacity> bombs{};
        /** @brief Hidden and revealed round powerups for the current or next match. */
        std::array<std::optional<PowerupState>, kServerPowerupCapacity> powerups{};

        /** @brief Collision map shared by all server-side movement steps. */
        sim::TileMap tiles{};

        // ----- Player-id allocation -----

        std::array<uint8_t, net::kMaxPlayers> playerIdPool{}; ///< Sorted free-list of available player ids.
        uint8_t playerIdPoolSize = 0;

        // ----- Round map selection -----

        std::optional<uint32_t> fixedMapSeedOverride{}; ///< Fixed map seed reused for every round when started with `--seed`.
        uint32_t mapSeed = 0;

        // ----- Lobby and round flow state -----

        uint32_t currentMatchId = 0; ///< Current match identifier, or 0 while idle in the lobby.
        uint32_t nextMatchId = 1;

        uint32_t currentLobbyCountdownPlayerMask = 0; ///< Bitmask of players currently participating in the lobby countdown.
        uint32_t currentLobbyCountdownDeadlineTick = 0;
        uint8_t currentLobbyCountdownLastBroadcastSecond = 0;

        uint32_t currentMatchPlayerMask = 0; ///< Bitmask of player ids participating in the current round start or active round.
        uint32_t currentMatchLoadedMask = 0;
        uint32_t currentMatchStartDeadlineTick = 0;
        uint32_t currentMatchGoShowTick = 0;
        uint32_t currentMatchUnlockTick = 0;
        uint32_t currentEndOfMatchReturnTick = 0;
        std::optional<uint8_t> roundWinnerPlayerId{}; ///< Winner of the current or most recent round, if any.
        bool roundEndedInDraw = false;

        // ----- Diagnostics -----

        net::NetDiagnostics diag; ///< Diagnostics recorder for this session.
    };

    // ----- Dispatcher context -----

    /** @brief Per-dispatch context passed through the typed packet handlers. */
    struct PacketDispatchContext
    {
        ServerState& state;
        ENetPeer* peer = nullptr;
        net::NetDiagnostics* diag = nullptr;
        net::NetPacketResult receiveResult = net::NetPacketResult::Rejected;
        std::optional<uint8_t> recordedPlayerId{}; ///< `std::nullopt` for pre-handshake traffic.
    };

    // ----- Session lifecycle -----

    /** @brief Resets a `ServerState` for a new dedicated-server session. */
    void initServerState(ServerState& state,
                         ENetHost* host,
                         bool diagEnabled = false,
                         bool overrideMapSeed = false,
                         uint32_t mapSeed = 0,
                         uint32_t inputLeadTicks = sim::kDefaultServerInputLeadTicks,
                         uint32_t snapshotIntervalTicks = sim::kDefaultServerSnapshotIntervalTicks,
                         bool powersEnabled = true);

    /** @brief Chooses the seed for the next round map. */
    void rollNextRoundMapSeed(ServerState& state);

    // ----- Player-id allocation -----

    /** @brief Returns the lowest available playerId. */
    [[nodiscard]]
    std::optional<uint8_t> acquirePlayerId(ServerState& state);

    /** @brief Removes a specific playerId from the free pool if available. */
    [[nodiscard]]
    bool acquireSpecificPlayerId(ServerState& state, uint8_t playerId);

    /** @brief Returns a playerId to the free pool. */
    void releasePlayerId(ServerState& state, uint8_t playerId);

    // ----- Peer-session binding and lookup -----
    /** @brief Binds a connected ENet peer to stable live peer-session storage. */
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
     * Creates the @ref PlayerSlot and binds it to the provided @ref PeerSession.
     */
    void acceptPeerSession(ServerState& state,
                           PeerSession& session,
                           uint8_t playerId,
                           std::string_view playerName,
                           uint8_t carriedWins = 0);

    /**
     * @brief Releases the live peer session bound to `peer`, if any.
     *
     * Disconnect ends the current assignment immediately. Later lobby admission
     * may reclaim the same free seat by exact player name.
     */
    [[nodiscard]]
    std::optional<uint8_t> releasePeerSession(ServerState& state, ENetPeer& peer);

    // ----- Match player state lifecycle -----
    /** @brief Creates active in-match state for one player seat. */
    void createMatchPlayerState(ServerState& state, uint8_t playerId);

    /** @brief Destroys active in-match state for one player seat, if present. */
    void destroyMatchPlayerState(ServerState& state, uint8_t playerId);

    // ----- Fixed-tick simulation -----

    /** @brief Advances the authoritative server by one fixed simulation tick. */
    void simulateServerTick(ServerState& state);
} // namespace bomberman::server

#endif // BOMBERMAN_SERVERSTATE_H
