/**
 * @file ServerBombs.cpp
 * @brief Authoritative server-side bomb placement, explosion, and round-end logic.
 */

#include "ServerBombs.h"

#include <array>
#include <optional>
#include <utility>

#include "Net/NetSend.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    namespace
    {
        /** @brief Maximum number of blast cells produced by one cross-shaped explosion. */
        constexpr std::size_t kMaxBlastCellsPerBomb = kMaxExplosionBlastCells;
        constexpr std::size_t kMaxDestroyedBricksPerBomb = kMaxExplosionDestroyedBricks;

        /** @brief Broadcasts one reliable gameplay packet to every accepted match player. */
        template<std::size_t N>
        bool broadcastReliableGameplayPacket(ServerState& state,
                                             const std::array<uint8_t, N>& bytes,
                                             const EMsgType type,
                                             const std::size_t payloadSize)
        {
            bool anyQueued = false;

            for (const auto& slot : state.matchPlayers)
            {
                if (!slot.has_value())
                    continue;

                const auto* session = findPeerSessionByPlayerId(state, slot->playerId);
                if (session == nullptr || session->peer == nullptr)
                    continue;

                const bool queued = queueReliableGame(session->peer, bytes);
                state.diag.recordPacketSent(type,
                                            slot->playerId,
                                            static_cast<uint8_t>(EChannel::GameplayReliable),
                                            kPacketHeaderSize + payloadSize,
                                            queued ? NetPacketResult::Ok : NetPacketResult::Dropped);
                anyQueued |= queued;
            }

            return anyQueued;
        }

        /** @brief Returns true when the current authoritative buttons contain a new bomb-press edge. */
        [[nodiscard]]
        bool hasBombPlacementEdge(const MatchPlayerState& matchPlayer)
        {
            const bool bombHeldNow = (matchPlayer.appliedButtons & kInputBomb) != 0;
            const bool bombHeldLastTick = (matchPlayer.previousTickButtons & kInputBomb) != 0;
            return bombHeldNow && !bombHeldLastTick;
        }

        /** @brief Converts an authoritative player center position into the occupied tile cell. */
        [[nodiscard]]
        std::optional<BombCell> bombCellFromPlayerPosition(const sim::TilePos& pos)
        {
            const int32_t col = pos.xQ / 256;
            const int32_t row = pos.yQ / 256;
            if (col < 0 || row < 0 ||
                col >= static_cast<int32_t>(tileArrayWidth) ||
                row >= static_cast<int32_t>(tileArrayHeight))
            {
                return std::nullopt;
            }

            return BombCell{
                static_cast<uint8_t>(col),
                static_cast<uint8_t>(row)
            };
        }

        /** @brief Returns true when no active bomb currently occupies the given tile cell. */
        [[nodiscard]]
        bool isBombCellUnoccupied(const ServerState& state, const BombCell& cell)
        {
            for (const auto& bombEntry : state.bombs)
            {
                if (!bombEntry.has_value())
                    continue;

                if (bombEntry->cell.col == cell.col && bombEntry->cell.row == cell.row)
                    return false;
            }

            return true;
        }

        /** @brief Returns a free authoritative bomb slot, or `std::nullopt` when capacity is exhausted. */
        [[nodiscard]]
        std::optional<std::size_t> findFreeBombSlot(const ServerState& state)
        {
            for (std::size_t i = 0; i < state.bombs.size(); ++i)
            {
                if (!state.bombs[i].has_value())
                    return i;
            }

            return std::nullopt;
        }

        /** @brief Returns true when one active match player overlaps the full tile area of a blast cell. */
        [[nodiscard]]
        bool playerOverlapsBlastCell(const MatchPlayerState& matchPlayer, const BombCell& cell)
        {
            const int32_t tileLeftQ = static_cast<int32_t>(cell.col) * 256;
            const int32_t tileTopQ = static_cast<int32_t>(cell.row) * 256;
            const int32_t tileRightQ = tileLeftQ + 256;
            const int32_t tileBottomQ = tileTopQ + 256;

            const int32_t playerLeftQ = matchPlayer.pos.xQ - sim::kHitboxHalfQ;
            const int32_t playerTopQ = matchPlayer.pos.yQ - sim::kHitboxHalfQ;
            const int32_t playerRightQ = matchPlayer.pos.xQ + sim::kHitboxHalfQ;
            const int32_t playerBottomQ = matchPlayer.pos.yQ + sim::kHitboxHalfQ;

            return playerLeftQ < tileRightQ &&
                   playerRightQ > tileLeftQ &&
                   playerTopQ < tileBottomQ &&
                   playerBottomQ > tileTopQ;
        }

        /** @brief Returns the active authoritative match player for one player id, if present. */
        [[nodiscard]]
        MatchPlayerState* findMatchPlayerState(ServerState& state, const uint8_t playerId)
        {
            if (playerId >= state.matchPlayers.size())
                return nullptr;

            auto& entry = state.matchPlayers[playerId];
            return entry.has_value() ? &entry.value() : nullptr;
        }

        /** @brief Decrements the active-bomb count for the owner of one resolved bomb, if still present. */
        void releaseBombOwnership(ServerState& state, const BombState& bomb)
        {
            MatchPlayerState* owner = findMatchPlayerState(state, bomb.ownerId);
            if (owner == nullptr || owner->activeBombCount == 0)
                return;

            --owner->activeBombCount;
        }

        /** @brief Appends one blast cell if it is not already present in the current blast set. */
        void appendUniqueBlastCell(std::array<BombCell, kMaxBlastCellsPerBomb>& blastCells,
                                   std::size_t& blastCellCount,
                                   const BombCell cell)
        {
            for (std::size_t i = 0; i < blastCellCount; ++i)
            {
                if (blastCells[i].col == cell.col && blastCells[i].row == cell.row)
                    return;
            }

            if (blastCellCount < blastCells.size())
                blastCells[blastCellCount++] = cell;
        }

        /**
         * @brief Computes the cross-shaped blast cells for one detonation and applies brick destruction.
         *
         * Bricks are included in the returned blast set and then stop propagation in that
         * direction. Stone blocks are not included and also stop propagation.
         */
        void computeBlastCellsAndDestroyBricks(ServerState& state,
                                               const BombState& bomb,
                                               std::array<BombCell, kMaxBlastCellsPerBomb>& outBlastCells,
                                               std::size_t& outBlastCellCount,
                                               std::array<BombCell, kMaxDestroyedBricksPerBomb>& outDestroyedBricks,
                                               std::size_t& outDestroyedBrickCount)
        {
            outBlastCellCount = 0;
            outDestroyedBrickCount = 0;

            appendUniqueBlastCell(outBlastCells, outBlastCellCount, bomb.cell);

            constexpr std::array<std::pair<int8_t, int8_t>, 4> kDirections{{
                { 1,  0},
                {-1,  0},
                { 0,  1},
                { 0, -1},
            }};

            for (const auto [dx, dy] : kDirections)
            {
                for (uint8_t step = 1; step <= bomb.radius; ++step)
                {
                    const int32_t col = static_cast<int32_t>(bomb.cell.col) + dx * static_cast<int32_t>(step);
                    const int32_t row = static_cast<int32_t>(bomb.cell.row) + dy * static_cast<int32_t>(step);
                    if (col < 0 || row < 0 ||
                        col >= static_cast<int32_t>(tileArrayWidth) ||
                        row >= static_cast<int32_t>(tileArrayHeight))
                    {
                        break;
                    }

                    const BombCell cell{
                        static_cast<uint8_t>(col),
                        static_cast<uint8_t>(row)
                    };
                    const Tile tile = state.tiles[cell.row][cell.col];
                    if (tile == Tile::Stone)
                        break;

                    appendUniqueBlastCell(outBlastCells, outBlastCellCount, cell);

                    if (tile == Tile::Brick)
                    {
                        state.tiles[cell.row][cell.col] = Tile::Grass;
                        if (outDestroyedBrickCount < outDestroyedBricks.size())
                            outDestroyedBricks[outDestroyedBrickCount++] = cell;
                        break;
                    }
                }
            }
        }

        /** @brief Kills any alive players whose hitbox overlaps one of the resolved blast cells. */
        uint8_t killPlayersInBlast(ServerState& state,
                                   const BombState& bomb,
                                   const std::array<BombCell, kMaxBlastCellsPerBomb>& blastCells,
                                   const std::size_t blastCellCount,
                                   uint8_t& outKilledPlayerMask)
        {
            uint8_t killedCount = 0;
            outKilledPlayerMask = 0;

            for (auto& matchEntry : state.matchPlayers)
            {
                if (!matchEntry.has_value())
                    continue;

                MatchPlayerState& matchPlayer = matchEntry.value();
                if (!matchPlayer.alive)
                    continue;

                bool hitByBlast = false;
                for (std::size_t i = 0; i < blastCellCount; ++i)
                {
                    if (playerOverlapsBlastCell(matchPlayer, blastCells[i]))
                    {
                        hitByBlast = true;
                        break;
                    }
                }

                if (!hitByBlast)
                    continue;

                matchPlayer.alive = false;
                matchPlayer.inputLocked = true;
                matchPlayer.lastAppliedButtons = 0;
                matchPlayer.appliedButtons = 0;
                matchPlayer.previousTickButtons = 0;
                outKilledPlayerMask |= static_cast<uint8_t>(1u << matchPlayer.playerId);
                ++killedCount;

                LOG_SERVER_INFO("Player killed playerId={} by bombOwnerId={} tick={} blastOrigin=({}, {})",
                                static_cast<int>(matchPlayer.playerId),
                                static_cast<int>(bomb.ownerId),
                                state.serverTick,
                                static_cast<int>(bomb.cell.col),
                                static_cast<int>(bomb.cell.row));
            }

            return killedCount;
        }

        /** @brief Resolves one bomb detonation at the current authoritative tick. */
        void resolveBombExplosion(ServerState& state, const std::size_t bombIndex)
        {
            if (bombIndex >= state.bombs.size() || !state.bombs[bombIndex].has_value())
                return;

            const BombState bomb = state.bombs[bombIndex].value();

            std::array<BombCell, kMaxBlastCellsPerBomb> blastCells{};
            std::size_t blastCellCount = 0;
            std::array<BombCell, kMaxDestroyedBricksPerBomb> destroyedBricks{};
            std::size_t destroyedBrickCount = 0;
            computeBlastCellsAndDestroyBricks(state,
                                              bomb,
                                              blastCells,
                                              blastCellCount,
                                              destroyedBricks,
                                              destroyedBrickCount);

            uint8_t killedPlayerMask = 0;
            const uint8_t killedPlayerCount =
                killPlayersInBlast(state, bomb, blastCells, blastCellCount, killedPlayerMask);
            state.diag.recordBricksDestroyed(static_cast<uint32_t>(destroyedBrickCount));

            MsgExplosionResolved explosion{};
            explosion.serverTick = state.serverTick;
            explosion.ownerId = bomb.ownerId;
            explosion.originCol = bomb.cell.col;
            explosion.originRow = bomb.cell.row;
            explosion.radius = bomb.radius;
            explosion.killedPlayerMask = killedPlayerMask;
            explosion.blastCellCount = static_cast<uint8_t>(blastCellCount);
            explosion.destroyedBrickCount = static_cast<uint8_t>(destroyedBrickCount);
            for (std::size_t i = 0; i < blastCellCount; ++i)
            {
                explosion.blastCells[i].col = blastCells[i].col;
                explosion.blastCells[i].row = blastCells[i].row;
            }
            for (std::size_t i = 0; i < destroyedBrickCount; ++i)
            {
                explosion.destroyedBricks[i].col = destroyedBricks[i].col;
                explosion.destroyedBricks[i].row = destroyedBricks[i].row;
            }

            releaseBombOwnership(state, bomb);
            state.bombs[bombIndex].reset();

            if (broadcastReliableGameplayPacket(state,
                                                makeExplosionResolvedPacket(explosion),
                                                EMsgType::ExplosionResolved,
                                                kMsgExplosionResolvedSize))
            {
                flush(state.host);
            }

            LOG_SERVER_INFO("Bomb exploded ownerId={} tick={} origin=({}, {}) radius={} blastCells={} destroyedBricks={} killedPlayers={}",
                            static_cast<int>(bomb.ownerId),
                            state.serverTick,
                            static_cast<int>(bomb.cell.col),
                            static_cast<int>(bomb.cell.row),
                            static_cast<int>(bomb.radius),
                            blastCellCount,
                            static_cast<int>(destroyedBrickCount),
                            static_cast<int>(killedPlayerCount));
        }
    } // namespace

    void tryPlaceBomb(ServerState& state, MatchPlayerState& matchPlayer)
    {
        if (!hasBombPlacementEdge(matchPlayer))
            return;

        if (!matchPlayer.alive)
        {
            LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because the player is dead",
                                matchPlayer.playerId,
                                state.serverTick);
            return;
        }
        if (matchPlayer.inputLocked)
        {
            LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because input is currently locked",
                                matchPlayer.playerId,
                                state.serverTick);
            return;
        }

        if (matchPlayer.activeBombCount >= matchPlayer.maxBombs)
        {
            LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because activeBombCount={} maxBombs={}",
                                matchPlayer.playerId,
                                state.serverTick,
                                matchPlayer.activeBombCount,
                                matchPlayer.maxBombs);
            return;
        }

        const auto cell = bombCellFromPlayerPosition(matchPlayer.pos);
        if (!cell.has_value())
        {
            LOG_NET_INPUT_WARN("Rejected bomb placement playerId={} tick={} because the authoritative position is out of bounds pos=({}, {})",
                               matchPlayer.playerId,
                               state.serverTick,
                               matchPlayer.pos.xQ,
                               matchPlayer.pos.yQ);
            return;
        }

        const Tile tile = state.tiles[cell->row][cell->col];
        if (tile == Tile::Stone || tile == Tile::Brick)
        {
            LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because cell=({}, {}) is solid tile={}",
                                matchPlayer.playerId,
                                state.serverTick,
                                static_cast<int>(cell->col),
                                static_cast<int>(cell->row),
                                static_cast<int>(tile));
            return;
        }

        if (!isBombCellUnoccupied(state, *cell))
        {
            LOG_NET_INPUT_DEBUG("Rejected bomb placement playerId={} tick={} because cell=({}, {}) is already occupied",
                                matchPlayer.playerId,
                                state.serverTick,
                                static_cast<int>(cell->col),
                                static_cast<int>(cell->row));
            return;
        }

        const auto freeSlot = findFreeBombSlot(state);
        if (!freeSlot.has_value())
        {
            LOG_NET_INPUT_WARN("Rejected bomb placement playerId={} tick={} because authoritative bomb capacity {} is exhausted",
                               matchPlayer.playerId,
                               state.serverTick,
                               state.bombs.size());
            return;
        }

        auto& bombEntry = state.bombs[freeSlot.value()];
        bombEntry.emplace();
        auto& bomb = bombEntry.value();
        bomb.ownerId = matchPlayer.playerId;
        bomb.cell = *cell;
        bomb.placedTick = state.serverTick;
        bomb.explodeTick = state.serverTick + sim::kDefaultBombFuseTicks;
        bomb.radius = matchPlayer.bombRange;

        ++matchPlayer.activeBombCount;
        state.diag.recordBombPlaced(matchPlayer.playerId,
                                    bomb.cell.col,
                                    bomb.cell.row,
                                    bomb.radius,
                                    state.serverTick);

        LOG_NET_INPUT_INFO("Bomb placed playerId={} tick={} cell=({}, {}) radius={} activeBombs={}/{} explodeTick={}",
                           matchPlayer.playerId,
                           state.serverTick,
                           static_cast<int>(bomb.cell.col),
                           static_cast<int>(bomb.cell.row),
                           static_cast<int>(bomb.radius),
                           static_cast<int>(matchPlayer.activeBombCount),
                           static_cast<int>(matchPlayer.maxBombs),
                           bomb.explodeTick);

        MsgBombPlaced bombPlaced{};
        bombPlaced.serverTick = state.serverTick;
        bombPlaced.explodeTick = bomb.explodeTick;
        bombPlaced.ownerId = bomb.ownerId;
        bombPlaced.col = bomb.cell.col;
        bombPlaced.row = bomb.cell.row;
        bombPlaced.radius = bomb.radius;

        if (broadcastReliableGameplayPacket(state,
                                            makeBombPlacedPacket(bombPlaced),
                                            EMsgType::BombPlaced,
                                            kMsgBombPlacedSize))
        {
            flush(state.host);
        }
    }

    void resolveExplodingBombs(ServerState& state)
    {
        for (std::size_t i = 0; i < state.bombs.size(); ++i)
        {
            if (!state.bombs[i].has_value())
                continue;

            if (state.bombs[i]->explodeTick > state.serverTick)
                continue;

            resolveBombExplosion(state, i);
        }
    }

} // namespace bomberman::server
