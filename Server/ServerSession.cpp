/**
 * @file ServerSession.cpp
 * @ingroup authoritative_server
 * @brief Authoritative server session lifecycle, peer-session binding, and player-slot management.
 */

#include "ServerState.h"

#include <random>

#include "ServerFlow.h"
#include "ServerPowerups.h"
#include "Sim/SpawnSlots.h"
#include "Sim/TileMapGen.h"
#include "Util/Log.h"

namespace bomberman::server
{
    namespace
    {
        static_assert(sim::kDefaultSpawnSlots.size() > 0, "Spawn slot table must not be empty");

        /**
         * @brief Maps an ENet incoming peer id to the stable peer-session storage slot.
         *
         * The server provisions @ref kServerPeerSessionCapacity live session
         * slots up front, indexed by `ENetPeer::incomingPeerID`.
         */
        [[nodiscard]]
        std::optional<std::size_t> peerSessionIndex(const ENetPeer& peer)
        {
            const std::size_t index = peer.incomingPeerID;
            if (index >= kServerPeerSessionCapacity)
                return std::nullopt;

            return index;
        }

        /** @brief Clears the stable peer-session slot and removes the `peer.data` back-pointer. */
        void clearPeerSessionStorage(ServerState& state, ENetPeer& peer)
        {
            const auto index = peerSessionIndex(peer);
            peer.data = nullptr;

            if (index.has_value())
                state.peerSessions[index.value()].reset();
        }

        void resetRoundFlowState(ServerState& state)
        {
            state.currentMatchId = 0;
            state.nextMatchId = 1;
            state.currentLobbyCountdownPlayerMask = 0;
            state.currentLobbyCountdownDeadlineTick = 0;
            state.currentLobbyCountdownLastBroadcastSecond = 0;
            state.currentMatchPlayerMask = 0;
            state.currentMatchLoadedMask = 0;
            state.currentMatchStartDeadlineTick = 0;
            state.currentMatchGoShowTick = 0;
            state.currentMatchUnlockTick = 0;
            state.currentEndOfMatchReturnTick = 0;
            state.roundWinnerPlayerId.reset();
            state.roundEndedInDraw = false;
        }

    } // namespace

    // =================================================================================================================
    // ===== Session Lifecycle =========================================================================================
    // =================================================================================================================

    void initServerState(ServerState& state,
                         ENetHost* host,
                         const bool diagEnabled,
                         const bool overrideMapSeed,
                         uint32_t mapSeed,
                         const uint32_t inputLeadTicks,
                         const uint32_t snapshotIntervalTicks,
                         const bool powersEnabled)
    {
        state.host = host;
        state.phase = ServerPhase::Lobby;
        state.serverTick = 0;
        state.inputLeadTicks = inputLeadTicks;
        state.snapshotIntervalTicks = snapshotIntervalTicks;
        state.powersEnabled = powersEnabled;

        state.peerSessions.fill(std::nullopt);
        state.matchPlayers.fill(std::nullopt);
        state.bombs.fill(std::nullopt);
        state.powerups.fill(std::nullopt);
        state.playerSlots.fill(std::nullopt);
        state.disconnectedPlayerReclaims.fill(std::nullopt);

        // Clear ENet back-pointers owned by the host's current peer array.
        if (state.host != nullptr)
        {
            for (std::size_t i = 0; i < state.host->peerCount; ++i)
                state.host->peers[i].data = nullptr;
        }

        // Initialize player ID pool.
        for (uint8_t i = 0; i < net::kMaxPlayers; ++i)
            state.playerIdPool[i] = i;

        state.playerIdPoolSize = net::kMaxPlayers;

        LOG_SERVER_INFO("ServerState initialized");

        state.fixedMapSeedOverride = overrideMapSeed ? std::optional<uint32_t>{mapSeed} : std::nullopt;
        rollNextRoundMapSeed(state);
        sim::generateTileMap(state.mapSeed, state.tiles);
        resetRoundFlowState(state);

        if (state.fixedMapSeedOverride.has_value())
            LOG_SERVER_INFO("Map generated with provided seed={}", state.mapSeed);
        else
            LOG_SERVER_INFO("Map generated with random seed={}", state.mapSeed);

        state.diag.beginSession("server", diagEnabled);
        state.diag.recordSessionConfig(net::ServerSessionConfig{
            .protocolVersion = net::kProtocolVersion,
            .tickRate = sim::kTickRate,
            .inputLeadTicks = state.inputLeadTicks,
            .snapshotIntervalTicks = state.snapshotIntervalTicks,
            .brickSpawnRandomize = bomberman::kBrickSpawnRandomize,
            .powerupsPerRound = sim::kPowerupsPerRound,
            .maxPlayers = net::kMaxPlayers,
            .powersEnabled = state.powersEnabled
        });
        refreshServerFlowDiagnostics(state);
    }

    void rollNextRoundMapSeed(ServerState& state)
    {
        if (state.fixedMapSeedOverride.has_value())
        {
            state.mapSeed = *state.fixedMapSeedOverride;
            return;
        }

        state.mapSeed = std::random_device{}();
    }

    // =================================================================================================================
    // ===== Player Id Allocation ======================================================================================
    // =================================================================================================================

    std::optional<uint8_t> acquirePlayerId(ServerState& state)
    {
        if (state.playerIdPoolSize == 0)
            return std::nullopt;

        const auto playerId = state.playerIdPool[0];

        for (uint8_t i = 1; i < state.playerIdPoolSize; ++i)
            state.playerIdPool[i - 1] = state.playerIdPool[i];

        --state.playerIdPoolSize;
        return playerId;
    }

    bool acquireSpecificPlayerId(ServerState& state, const uint8_t playerId)
    {
        for (uint8_t i = 0; i < state.playerIdPoolSize; ++i)
        {
            if (state.playerIdPool[i] != playerId)
                continue;

            for (uint8_t j = i + 1; j < state.playerIdPoolSize; ++j)
                state.playerIdPool[j - 1] = state.playerIdPool[j];

            --state.playerIdPoolSize;
            return true;
        }

        return false;
    }

    void releasePlayerId(ServerState& state, const uint8_t playerId)
    {
        if (state.playerIdPoolSize >= net::kMaxPlayers)
            return;

        // Keep free IDs sorted so acquirePlayerId() always returns the lowest available id.
        uint8_t insertIndex = 0;
        while (insertIndex < state.playerIdPoolSize && state.playerIdPool[insertIndex] < playerId)
            ++insertIndex;

        if (insertIndex < state.playerIdPoolSize && state.playerIdPool[insertIndex] == playerId)
            return;

        for (uint8_t i = state.playerIdPoolSize; i > insertIndex; --i)
            state.playerIdPool[i] = state.playerIdPool[i - 1];

        state.playerIdPool[insertIndex] = playerId;
        ++state.playerIdPoolSize;
    }

    // =================================================================================================================
    // ===== Peer Session Binding and Lookup ===========================================================================
    // =================================================================================================================

    PeerSession* bindPeerSession(ServerState& state, ENetPeer& peer)
    {
        const auto index = peerSessionIndex(peer);
        if (!index.has_value())
        {
            peer.data = nullptr;
            return nullptr;
        }

        auto& sessionEntry = state.peerSessions[index.value()];
        sessionEntry.emplace();
        auto& session = sessionEntry.value();
        session.peer = &peer;
        session.playerId.reset();
        session.connectedServerTick = state.serverTick;

        // Store the stable live-session address for all receive and disconnect handling.
        peer.data = &session;
        return &session;
    }

    PeerSession* getPeerSession(ENetPeer* peer)
    {
        return peer ? static_cast<PeerSession*>(peer->data) : nullptr;
    }

    const PeerSession* getPeerSession(const ENetPeer* peer)
    {
        return peer ? static_cast<const PeerSession*>(peer->data) : nullptr;
    }

    PeerSession* findPeerSessionByPlayerId(ServerState& state, const uint8_t playerId)
    {
        for (auto& sessionEntry : state.peerSessions)
        {
            if (!sessionEntry.has_value() || !sessionEntry->playerId.has_value())
                continue;

            if (sessionEntry->playerId.value() == playerId)
                return &sessionEntry.value();
        }

        return nullptr;
    }

    const PeerSession* findPeerSessionByPlayerId(const ServerState& state, const uint8_t playerId)
    {
        for (const auto& sessionEntry : state.peerSessions)
        {
            if (!sessionEntry.has_value() || !sessionEntry->playerId.has_value())
                continue;

            if (sessionEntry->playerId.value() == playerId)
                return &sessionEntry.value();
        }

        return nullptr;
    }

    // =================================================================================================================
    // ===== Peer Session Acceptance and Release =======================================================================
    // =================================================================================================================

    void acceptPeerSession(ServerState& state,
                           PeerSession& session,
                           const uint8_t playerId,
                           const std::string_view playerName,
                           const uint8_t carriedWins)
    {
        auto& slotEntry = state.playerSlots[playerId];
        slotEntry.emplace();
        auto& slot = slotEntry.value();
        slot.playerId = playerId;
        slot.playerName = std::string(playerName);
        slot.ready = false;
        slot.wins = carriedWins;

        // Once the seat is occupied again, any previous reclaim metadata for
        // that playerId is no longer relevant.
        state.disconnectedPlayerReclaims[playerId].reset();

        // Bind the accepted player seat to the live session.
        session.playerId = playerId;
    }

    std::optional<uint8_t> releasePeerSession(ServerState& state, ENetPeer& peer)
    {
        auto* session = getPeerSession(&peer);
        if (session == nullptr)
        {
            peer.data = nullptr;
            return std::nullopt;
        }

        std::optional<uint8_t> releasedPlayerId = session->playerId;
        if (releasedPlayerId.has_value())
        {
            const uint8_t playerId = releasedPlayerId.value();
            if (const auto& slotEntry = state.playerSlots[playerId]; slotEntry.has_value())
            {
                state.disconnectedPlayerReclaims[playerId] = DisconnectedPlayerReclaim{
                    .playerName = slotEntry->playerName,
                    .wins = slotEntry->wins
                };
            }

            destroyMatchPlayerState(state, playerId);
            state.playerSlots[playerId].reset();

            releasePlayerId(state, playerId);
            clearPeerSessionStorage(state, peer);
            handleAcceptedPlayerReleased(state, playerId);
            return releasedPlayerId;
        }

        clearPeerSessionStorage(state, peer);

        return releasedPlayerId;
    }

    // =================================================================================================================
    // ===== Match Player State Lifecycle ==============================================================================
    // =================================================================================================================

    void createMatchPlayerState(ServerState& state, const uint8_t playerId)
    {
        auto& matchEntry = state.matchPlayers[playerId];
        matchEntry.emplace();
        auto& matchPlayer = matchEntry.value();
        matchPlayer.playerId = playerId;
        matchPlayer.pos = sim::spawnTilePosForPlayerId(playerId);
        matchPlayer.alive = true;
        matchPlayer.inputLocked = true;
        matchPlayer.activeBombCount = 0;
        matchPlayer.maxBombs = sim::kDefaultPlayerMaxBombs;
        matchPlayer.bombRange = sim::kDefaultPlayerBombRange;
        matchPlayer.invincibleUntilTick = 0;
        matchPlayer.speedBoostUntilTick = 0;
        matchPlayer.bombRangeBoostUntilTick = 0;
        matchPlayer.maxBombsBoostUntilTick = 0;
        matchPlayer.previousTickButtons = 0;
    }

    void destroyMatchPlayerState(ServerState& state, const uint8_t playerId)
    {
        state.matchPlayers[playerId].reset();
    }

} // namespace bomberman::server
