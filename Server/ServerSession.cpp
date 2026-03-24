/**
 * @file ServerSession.cpp
 * @brief Authoritative server session lifecycle, peer-session binding, and player-slot management.
 */

#include "ServerState.h"

#include <random>

#include "Sim/TileMapGen.h"
#include "Util/Log.h"

namespace bomberman::server
{
    namespace
    {
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

        /** @brief Records the most recent disconnect metadata for a durable player slot. */
        void markReleasedPlayerSlot(PlayerSlot& slot, const ENetPeer& peer, const uint32_t serverTick)
        {
            slot.lastKnownAddress = peer.address;
            slot.lastDisconnectServerTick = serverTick;
        }

        /** @brief Clears the stable peer-session slot and removes the `peer.data` back-pointer. */
        void clearPeerSessionStorage(ServerState& state, ENetPeer& peer)
        {
            const auto index = peerSessionIndex(peer);
            peer.data = nullptr;

            if (index.has_value())
                state.peerSessions[index.value()].reset();
        }

        /** @brief Removes all active bombs owned by the given player seat. */
        void clearOwnedBombState(ServerState& state, const uint8_t ownerId)
        {
            for (auto& bombEntry : state.bombs)
            {
                if (!bombEntry.has_value() || bombEntry->ownerId != ownerId)
                    continue;

                bombEntry.reset();
            }
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
                         const uint32_t snapshotIntervalTicks)
    {
        state.host = host;
        state.phase = ServerPhase::Lobby;
        state.serverTick = 0;
        state.inputLeadTicks = inputLeadTicks;
        state.snapshotIntervalTicks = snapshotIntervalTicks;

        // Reset all live peer sessions.
        for (auto& slot : state.peerSessions)
            slot.reset();

        // Reset all active match-player slots.
        for (auto& slot : state.matchPlayers)
            slot.reset();

        // Reset all active bomb slots for the new session.
        for (auto& slot : state.bombs)
            slot.reset();

        // Reset durable player slots for the new dedicated-server session.
        for (auto& slot : state.playerSlots)
            slot.reset();

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

        // Generate the tile map with either the provided seed or a random seed.
        uint32_t seed = mapSeed;
        if (!overrideMapSeed)
        {
            seed = std::random_device{}();
        }
        state.mapSeed = seed;
        sim::generateTileMap(state.mapSeed, state.tiles);

        if (overrideMapSeed)
            LOG_SERVER_INFO("Map generated with provided seed={}", state.mapSeed);
        else
            LOG_SERVER_INFO("Map generated with random seed={}", state.mapSeed);

        state.diag.beginSession("server", diagEnabled);
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
                           const std::string_view playerName)
    {
        auto& slotEntry = state.playerSlots[playerId];
        slotEntry.emplace();
        auto& slot = slotEntry.value();
        slot.playerId = playerId;
        slot.playerName = std::string(playerName);
        slot.lastKnownAddress = session.peer->address;
        slot.acceptedServerTick = state.serverTick;
        slot.lastDisconnectServerTick.reset();

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

            if (auto& slotEntry = state.playerSlots[playerId]; slotEntry.has_value())
                markReleasedPlayerSlot(slotEntry.value(), peer, state.serverTick);

            destroyMatchPlayerState(state, playerId);
            releasePlayerId(state, playerId);
        }

        clearPeerSessionStorage(state, peer);

        return releasedPlayerId;
    }

    // =================================================================================================================
    // ===== Match Player State Lifecycle ==============================================================================
    // =================================================================================================================

    void createMatchPlayerState(ServerState& state, const uint8_t playerId, const sim::TilePos spawnPos)
    {
        auto& matchEntry = state.matchPlayers[playerId];
        matchEntry.emplace();
        auto& matchPlayer = matchEntry.value();
        matchPlayer.playerId = playerId;
        matchPlayer.pos = spawnPos;
        matchPlayer.alive = true;
        matchPlayer.activeBombCount = 0;
        matchPlayer.maxBombs = sim::kDefaultPlayerMaxBombs;
        matchPlayer.bombRange = sim::kDefaultPlayerBombRange;
        matchPlayer.previousTickButtons = 0;
    }

    void destroyMatchPlayerState(ServerState& state, const uint8_t playerId)
    {
        clearOwnedBombState(state, playerId);
        state.matchPlayers[playerId].reset();
    }

} // namespace bomberman::server
