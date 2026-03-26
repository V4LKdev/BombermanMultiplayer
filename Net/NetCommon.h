#ifndef BOMBERMAN_NET_NETCOMMON_H
#define BOMBERMAN_NET_NETCOMMON_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "Const.h"

/**
 * @file NetCommon.h
 * @brief Shared protocol constants, message types, and wire helpers.
 *
 * The protocol requires a strict version match during handshake.
 *
 * @note When changing wire layout, update:
 * 1. @ref kProtocolVersion
 * 2. Wire size constants and `static_assert` checks
 * 3. @ref expectedPayloadSize
 * 4. The affected serializers and deserializers
 */

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Protocol Definition =======================================================================================
    // =================================================================================================================

    // ----- Protocol Constants -----

    constexpr uint16_t      kProtocolVersion = 8;

    constexpr uint16_t      kDefaultServerPort = 12345;  ///< Default server port used by both client and server.
    constexpr std::size_t   kMaxPacketSize = 1400;       ///< Upper packet size bound (below typical 1500-byte MTU).
    constexpr uint32_t      kInputLogEveryN = 30;        ///< Input log sampling interval on client and server.
    constexpr uint32_t      kSnapshotLogEveryN = 30;     ///< Snapshot log sampling interval on client and server.
    constexpr uint32_t      kPeerPingIntervalMs = 500;   ///< Transport heartbeat interval for connected peers.
    constexpr uint32_t      kPeerTimeoutLimit = 8;       ///< Consecutive timeout limit before ENet drops a peer.
    constexpr uint32_t      kPeerTimeoutMinimumMs = 5000;  ///< Lower bound for ENet peer timeout detection.
    constexpr uint32_t      kPeerTimeoutMaximumMs = 10000; ///< Upper bound for ENet peer timeout detection.

    /** @note Changing these values will affect wire layout and require protocol version bump. */
    constexpr std::size_t   kPlayerNameMax = 16;
    constexpr uint8_t       kMaxPlayers = 4;             ///< Maximum supported player count in a game instance.
    constexpr uint8_t       kMaxSnapshotBombs = kMaxPlayers * 4; ///< Maximum bombs carried by one snapshot payload.
    constexpr std::size_t   kMaxExplosionBlastCells =
        1u + 4u * (static_cast<std::size_t>((tileArrayWidth > tileArrayHeight) ?
            tileArrayWidth :
            tileArrayHeight) - 1u); ///< Maximum cells a cross-shaped blast can touch.
    constexpr std::size_t   kMaxExplosionDestroyedBricks = kMaxExplosionBlastCells - 1u; ///< Upper bound for bricks one blast can destroy.

    /** @brief First valid input sequence number. Seq 0 means "no input received yet". */
    constexpr uint32_t      kFirstInputSeq = 1;

    /** @brief Maximum number of inputs in a single batched MsgInput packet. */
    constexpr uint8_t       kMaxInputBatchSize = 16;

    // ----- Input Bitmask Constants -----

    constexpr uint8_t kInputUp    = 0x01;
    constexpr uint8_t kInputDown  = 0x02;
    constexpr uint8_t kInputLeft  = 0x04;
    constexpr uint8_t kInputRight = 0x08;
    constexpr uint8_t kInputBomb  = 0x10;

    /** @brief Union of all defined input bits. Used for unknown-bit rejection. */
    constexpr uint8_t kInputKnownBits = kInputUp | kInputDown | kInputLeft | kInputRight | kInputBomb;

    /** @brief Derives horizontal movement {-1, 0, 1} from a button bitmask. */
    constexpr int8_t buttonsToMoveX(uint8_t buttons)
    {
        int8_t dx = 0;
        if (buttons & kInputRight) dx += 1;
        if (buttons & kInputLeft)  dx -= 1;
        return dx;
    }

    /** @brief Derives vertical movement {-1, 0, 1} from a button bitmask. */
    constexpr int8_t buttonsToMoveY(uint8_t buttons)
    {
        int8_t dy = 0;
        if (buttons & kInputDown) dy += 1;
        if (buttons & kInputUp)   dy -= 1;
        return dy;
    }

    // ----- ENet Channels -----

    /**
     * @brief ENet channel identifiers used by the current protocol.
     *
     * @note ENet preserves packet order only within a single channel. The
     * protocol assigns each message type to exactly one expected channel
     * and rejects packets received on the wrong one.
     */
    enum class EChannel : uint8_t
    {
        ControlReliable      = 0,
        GameplayReliable     = 1,
        InputUnreliable      = 2,
        SnapshotUnreliable   = 3,
        CorrectionUnreliable = 4
    };
    constexpr std::size_t kChannelCount = 5;  ///< Number of ENet channels provisioned by client and server.

    /** @brief Returns a human-readable name for a channel ID. */
    constexpr std::string_view channelName(uint8_t id)
    {
        switch (id)
        {
            case static_cast<uint8_t>(EChannel::ControlReliable):      return "ControlReliable";
            case static_cast<uint8_t>(EChannel::GameplayReliable):     return "GameplayReliable";
            case static_cast<uint8_t>(EChannel::InputUnreliable):      return "InputUnreliable";
            case static_cast<uint8_t>(EChannel::SnapshotUnreliable):   return "SnapshotUnreliable";
            case static_cast<uint8_t>(EChannel::CorrectionUnreliable): return "CorrectionUnreliable";
            default:                                                   return "Unknown";
        }
    }

    // ----- Wire Size Constants -----

    constexpr std::size_t kPacketHeaderSize =
        sizeof(uint8_t) +  // type
        sizeof(uint16_t);  // payloadSize

    constexpr std::size_t kMsgHelloSize =
        sizeof(uint16_t) + // protocolVersion
        kPlayerNameMax;    // name (fixed-size field)

    constexpr std::size_t kMsgWelcomeSize =
        sizeof(uint16_t) + // protocolVersion
        sizeof(uint8_t) +  // playerId
        sizeof(uint16_t);  // serverTickRate

    constexpr std::size_t kMsgRejectSize =
        sizeof(uint8_t) +  // reason
        sizeof(uint16_t);  // expectedProtocolVersion

    constexpr std::size_t kMsgLevelInfoSize =
        sizeof(uint32_t) +  // matchId
        sizeof(uint32_t);   // mapSeed

    constexpr std::size_t kMsgLobbyReadySize =
        sizeof(uint8_t);   // ready

    constexpr std::size_t kMsgMatchLoadedSize =
        sizeof(uint32_t);  // matchId

    constexpr std::size_t kMsgMatchStartSize =
        sizeof(uint32_t) +  // matchId
        sizeof(uint32_t) +  // goShowServerTick
        sizeof(uint32_t);   // unlockServerTick

    constexpr std::size_t kMsgMatchCancelledSize =
        sizeof(uint32_t);  // matchId

    constexpr std::size_t kMsgMatchResultSize =
        sizeof(uint32_t) +  // matchId
        sizeof(uint8_t) +   // result
        sizeof(uint8_t) +   // winnerPlayerId
        kPlayerNameMax;     // winnerName

    constexpr std::size_t kMsgLobbyStateSeatSize =
        sizeof(uint8_t) +  // flags
        sizeof(uint8_t) +  // wins
        kPlayerNameMax;    // name (fixed-size field)

    constexpr std::size_t kMsgLobbyStateSize =
        sizeof(uint8_t) +   // phase
        sizeof(uint8_t) +   // countdownSecondsRemaining
        sizeof(uint16_t) +  // reserved
        kMaxPlayers * kMsgLobbyStateSeatSize;

    constexpr std::size_t kMsgInputSize =
        sizeof(uint32_t) + // baseInputSeq
        sizeof(uint8_t) +  // count
        kMaxInputBatchSize; // inputs[kMaxInputBatchSize]

    constexpr std::size_t kMsgSnapshotSize =
        sizeof(uint32_t) + // matchId
        sizeof(uint32_t) + // serverTick
        sizeof(uint8_t) +  // playerCount
        sizeof(uint8_t) +  // bombCount
        kMaxPlayers *
            (sizeof(uint8_t) +   // playerId
             sizeof(int16_t) +   // xQ
             sizeof(int16_t) +   // yQ
             sizeof(uint8_t)) +  // flags
        kMaxSnapshotBombs *
            (sizeof(uint8_t) +   // ownerId
             sizeof(uint8_t) +   // col
             sizeof(uint8_t) +   // row
             sizeof(uint8_t));   // radius

    constexpr std::size_t kMsgCorrectionSize =
        sizeof(uint32_t) + // matchId
        sizeof(uint32_t) + // serverTick
        sizeof(uint32_t) + // lastProcessedInputSeq
        sizeof(int16_t) +  // xQ
        sizeof(int16_t);   // yQ

    constexpr std::size_t kMsgBombPlacedSize =
        sizeof(uint32_t) + // matchId
        sizeof(uint32_t) + // serverTick
        sizeof(uint32_t) + // explodeTick
        sizeof(uint8_t) +  // ownerId
        sizeof(uint8_t) +  // col
        sizeof(uint8_t) +  // row
        sizeof(uint8_t);   // radius

    constexpr std::size_t kMsgExplosionResolvedSize =
        sizeof(uint32_t) + // matchId
        sizeof(uint32_t) + // serverTick
        sizeof(uint8_t) +  // ownerId
        sizeof(uint8_t) +  // originCol
        sizeof(uint8_t) +  // originRow
        sizeof(uint8_t) +  // radius
        sizeof(uint8_t) +  // killedPlayerMask
        sizeof(uint8_t) +  // blastCellCount
        sizeof(uint8_t) +  // destroyedBrickCount
        kMaxExplosionBlastCells *
            (sizeof(uint8_t) + // col
             sizeof(uint8_t)) + // row
        kMaxExplosionDestroyedBricks *
            (sizeof(uint8_t) + // col
             sizeof(uint8_t)); // row

    /** @brief Compile-time checks for expected wire sizes. */
    static_assert(sizeof(char) == 1, "Unexpected char size");

    static_assert(kPacketHeaderSize         == 3,  "PacketHeader size mismatch");
    static_assert(kMsgHelloSize             == 18, "MsgHello size mismatch");
    static_assert(kMsgWelcomeSize           == 5,  "MsgWelcome size mismatch");
    static_assert(kMsgRejectSize            == 3,  "MsgReject size mismatch");
    static_assert(kMsgLevelInfoSize         == 8,  "MsgLevelInfo size mismatch");
    static_assert(kMsgLobbyReadySize        == 1,  "MsgLobbyReady size mismatch");
    static_assert(kMsgMatchLoadedSize       == 4,  "MsgMatchLoaded size mismatch");
    static_assert(kMsgMatchStartSize        == 12, "MsgMatchStart size mismatch");
    static_assert(kMsgMatchCancelledSize    == 4,  "MsgMatchCancelled size mismatch");
    static_assert(kMsgMatchResultSize       == 22, "MsgMatchResult size mismatch");
    static_assert(kMsgLobbyStateSeatSize    == 18, "MsgLobbyState seat size mismatch");
    static_assert(kMsgLobbyStateSize        == 76, "MsgLobbyState size mismatch");
    static_assert(kMsgInputSize             == 21, "MsgInput size mismatch");
    static_assert(kMsgSnapshotSize          == 98, "MsgSnapshot size mismatch");
    static_assert(kMsgCorrectionSize        == 16, "MsgCorrection size mismatch");
    static_assert(kMsgBombPlacedSize        == 16, "MsgBombPlaced size mismatch");
    static_assert(kMsgExplosionResolvedSize == 497,"MsgExplosionResolved size mismatch");

    constexpr std::size_t kSnapshotPlayersOffset =
        sizeof(uint32_t) + // matchId
        sizeof(uint32_t) + // serverTick
        sizeof(uint8_t) +  // playerCount
        sizeof(uint8_t);   // bombCount
    constexpr std::size_t kSnapshotPlayerEntrySize =
        sizeof(uint8_t) +  // playerId
        sizeof(int16_t) +  // xQ
        sizeof(int16_t) +  // yQ
        sizeof(uint8_t);   // flags
    constexpr std::size_t kSnapshotBombsOffset =
        kSnapshotPlayersOffset + kMaxPlayers * kSnapshotPlayerEntrySize;
    constexpr std::size_t kSnapshotBombEntrySize =
        sizeof(uint8_t) +  // ownerId
        sizeof(uint8_t) +  // col
        sizeof(uint8_t) +  // row
        sizeof(uint8_t);   // radius

    constexpr std::size_t kExplosionBlastCellsOffset =
        sizeof(uint32_t) + // matchId
        sizeof(uint32_t) + // serverTick
        sizeof(uint8_t) +  // ownerId
        sizeof(uint8_t) +  // originCol
        sizeof(uint8_t) +  // originRow
        sizeof(uint8_t) +  // radius
        sizeof(uint8_t) +  // killedPlayerMask
        sizeof(uint8_t) +  // blastCellCount
        sizeof(uint8_t);   // destroyedBrickCount
    constexpr std::size_t kExplosionCellEntrySize =
        sizeof(uint8_t) +  // col
        sizeof(uint8_t);   // row
    constexpr std::size_t kExplosionDestroyedBricksOffset =
        kExplosionBlastCellsOffset + kMaxExplosionBlastCells * kExplosionCellEntrySize;

    // ----- Message Types -----

    /** @brief Message type identifiers used in packet headers. */
    enum class EMsgType : uint8_t
    {
        Invalid    = 0x00,
        Hello      = 0x01,
        Welcome    = 0x02,
        Reject     = 0x03,
        LevelInfo  = 0x04,
        LobbyState = 0x05,
        LobbyReady = 0x06,
        MatchLoaded = 0x07,
        MatchStart = 0x08,
        MatchCancelled = 0x09,
        MatchResult = 0x0A,

        Input      = 0x10,
        Snapshot   = 0x11,
        Correction = 0x12,
        BombPlaced = 0x13,
        ExplosionResolved = 0x14
    };

    /** @brief Checks whether a raw byte value corresponds to a valid @ref EMsgType. */
    inline bool isValidMsgType(uint8_t raw)
    {
        return raw == static_cast<uint8_t>(EMsgType::Hello)      ||
               raw == static_cast<uint8_t>(EMsgType::Welcome)    ||
               raw == static_cast<uint8_t>(EMsgType::Reject)     ||
               raw == static_cast<uint8_t>(EMsgType::LevelInfo)  ||
               raw == static_cast<uint8_t>(EMsgType::LobbyState) ||
               raw == static_cast<uint8_t>(EMsgType::LobbyReady) ||
               raw == static_cast<uint8_t>(EMsgType::MatchLoaded) ||
               raw == static_cast<uint8_t>(EMsgType::MatchStart) ||
               raw == static_cast<uint8_t>(EMsgType::MatchCancelled) ||
               raw == static_cast<uint8_t>(EMsgType::MatchResult) ||
               raw == static_cast<uint8_t>(EMsgType::Input)      ||
               raw == static_cast<uint8_t>(EMsgType::Snapshot)   ||
               raw == static_cast<uint8_t>(EMsgType::Correction) ||
               raw == static_cast<uint8_t>(EMsgType::BombPlaced) ||
               raw == static_cast<uint8_t>(EMsgType::ExplosionResolved);
    }

    /** @brief Returns a human-readable name for a protocol message type. */
    constexpr std::string_view msgTypeName(EMsgType type)
    {
        switch (type)
        {
            case EMsgType::Invalid:    return "Invalid";
            case EMsgType::Hello:      return "Hello";
            case EMsgType::Welcome:    return "Welcome";
            case EMsgType::Reject:     return "Reject";
            case EMsgType::LevelInfo:  return "LevelInfo";
            case EMsgType::LobbyState: return "LobbyState";
            case EMsgType::LobbyReady: return "LobbyReady";
            case EMsgType::MatchLoaded: return "MatchLoaded";
            case EMsgType::MatchStart: return "MatchStart";
            case EMsgType::MatchCancelled: return "MatchCancelled";
            case EMsgType::MatchResult: return "MatchResult";
            case EMsgType::Input:      return "Input";
            case EMsgType::Snapshot:   return "Snapshot";
            case EMsgType::Correction: return "Correction";
            case EMsgType::BombPlaced: return "BombPlaced";
            case EMsgType::ExplosionResolved: return "ExplosionResolved";
            default:                   return "Unknown";
        }
    }

    /**
     * @brief Returns the required ENet channel for a protocol message type.
     *
     * @note Handshake, lobby, and match-flow control messages all use reliable
     * control delivery owned by higher-level session and match flow.
     */
    constexpr EChannel expectedChannelFor(EMsgType type)
    {
        switch (type)
        {
            case EMsgType::Hello:
            case EMsgType::Welcome:
            case EMsgType::Reject:
            case EMsgType::LevelInfo:
            case EMsgType::LobbyState:
            case EMsgType::LobbyReady:
            case EMsgType::MatchLoaded:
            case EMsgType::MatchStart:
            case EMsgType::MatchCancelled:
            case EMsgType::MatchResult:
                return EChannel::ControlReliable;
            case EMsgType::Input:
                return EChannel::InputUnreliable;
            case EMsgType::Snapshot:
                return EChannel::SnapshotUnreliable;
            case EMsgType::Correction:
                return EChannel::CorrectionUnreliable;
            case EMsgType::BombPlaced:
            case EMsgType::ExplosionResolved:
                return EChannel::GameplayReliable;
            default:
                return EChannel::ControlReliable;
        }
    }

    /** @brief Returns true when the given player-id bitmask uses only valid player bits. */
    constexpr bool isValidPlayerMask(const uint32_t mask)
    {
        return (mask & ~((1u << kMaxPlayers) - 1u)) == 0;
    }

    /** @brief Returns the payload offset of the explosion blast-cell entry at `index`. */
    constexpr std::size_t explosionBlastCellOffset(std::size_t index)
    {
        return kExplosionBlastCellsOffset + index * kExplosionCellEntrySize;
    }

    /** @brief Returns the payload offset of the explosion destroyed-brick entry at `index`. */
    constexpr std::size_t explosionDestroyedBrickOffset(std::size_t index)
    {
        return kExplosionDestroyedBricksOffset + index * kExplosionCellEntrySize;
    }

    /** @brief Returns true when the given cell lies within the current tile-map bounds. */
    constexpr bool isValidTileCell(const uint8_t col, const uint8_t row)
    {
        return col < ::bomberman::tileArrayWidth && row < ::bomberman::tileArrayHeight;
    }

    /** @brief Packs one tile cell into a monotonic key for ordering and deduplication. */
    constexpr uint16_t tileCellKey(const uint8_t col, const uint8_t row)
    {
        return static_cast<uint16_t>(row) * static_cast<uint16_t>(::bomberman::tileArrayWidth) +
               static_cast<uint16_t>(col);
    }

    /** @brief Returns true when a message was received on its expected ENet channel. */
    constexpr bool isExpectedChannelFor(EMsgType type, uint8_t channelId)
    {
        return channelId == static_cast<uint8_t>(expectedChannelFor(type));
    }

    /**
     * @brief Returns the exact expected payload size for a message type.
     *
     * All current messages use fixed-size payloads. Returns 0 for unknown types.
     */
    constexpr std::size_t expectedPayloadSize(EMsgType type)
    {
        switch (type)
        {
            case EMsgType::Hello:      return kMsgHelloSize;
            case EMsgType::Welcome:    return kMsgWelcomeSize;
            case EMsgType::Reject:     return kMsgRejectSize;
            case EMsgType::LevelInfo:  return kMsgLevelInfoSize;
            case EMsgType::LobbyState: return kMsgLobbyStateSize;
            case EMsgType::LobbyReady: return kMsgLobbyReadySize;
            case EMsgType::MatchLoaded: return kMsgMatchLoadedSize;
            case EMsgType::MatchStart: return kMsgMatchStartSize;
            case EMsgType::MatchCancelled: return kMsgMatchCancelledSize;
            case EMsgType::MatchResult: return kMsgMatchResultSize;
            case EMsgType::Input:      return kMsgInputSize;
            case EMsgType::Snapshot:   return kMsgSnapshotSize;
            case EMsgType::Correction: return kMsgCorrectionSize;
            case EMsgType::BombPlaced: return kMsgBombPlacedSize;
            case EMsgType::ExplosionResolved: return kMsgExplosionResolvedSize;
            default:                   return 0;
        }
    }

    // =================================================================================================================
    // ===== Wire Payload Types ========================================================================================
    // =================================================================================================================

    /** @brief Packet metadata prefix present on every wire message. */
    struct PacketHeader
    {
        EMsgType type = EMsgType::Invalid; ///< Message type identifier.
        uint16_t payloadSize = 0; ///< Payload size in bytes, excluding header.
    };

    // ----- Control Message Payloads -----

    /** @brief Hello payload sent by client during handshake. */
    struct MsgHello
    {
        uint16_t protocolVersion; ///< Client protocol version.

        /**
         * @brief Player display name in a fixed-size wire field.
         *
         * The field is always @ref kPlayerNameMax bytes. The value must be
         * NUL-terminated and zero-padded. Maximum visible length is
         * kPlayerNameMax - 1.
         */
        char name[kPlayerNameMax];
    };

    /** @brief Welcome payload sent by server in response to Hello. */
    struct MsgWelcome
    {
        uint16_t protocolVersion; ///< Server protocol version.
        uint8_t playerId;         ///< Server-assigned player identifier [0, kMaxPlayers).
        uint16_t serverTickRate;  ///< Authoritative server simulation tick rate. Must be greater than zero.
    };

    /** @brief Reject payload sent by server in response to Hello on failure. */
    struct MsgReject
    {
        enum class EReason : uint8_t
        {
            VersionMismatch = 0x01,
            ServerFull = 0x02,
            Banned = 0x03,
            GameInProgress = 0x04, ///< Server is not admitting new players outside lobby/pre-start flow.
            Other = 0xFF
        };

        EReason reason = EReason::Other; ///< Rejection reason code.

        uint16_t expectedProtocolVersion = 0; ///< Expected protocol version.
    };

    /**
     * @brief Round-start payload sent reliably by the server when a match begins.
     *
     * Carries the current authoritative match identifier and the map seed
     * needed to instantiate the shared gameplay scene for this round.
     */
    struct MsgLevelInfo
    {
        uint32_t matchId = 0; ///< Monotonic authoritative match identifier for this round start.
        uint32_t mapSeed = 0; ///< 32-bit seed for the shared tile-map generator.
    };

    /** @brief Client-authored desired ready state for its currently accepted lobby seat. */
    struct MsgLobbyReady
    {
        uint8_t ready = 0; ///< `1` marks ready, `0` clears ready.
    };

    /** @brief Client acknowledgement that the gameplay scene for one round start is loaded. */
    struct MsgMatchLoaded
    {
        uint32_t matchId = 0; ///< Authoritative match identifier being acknowledged.
    };

    /** @brief Explicit reliable start edge sent once all current match participants have loaded. */
    struct MsgMatchStart
    {
        uint32_t matchId = 0;          ///< Authoritative match identifier being started.
        uint32_t goShowServerTick = 0; ///< Authoritative server tick at which `GO!` should appear.
        uint32_t unlockServerTick = 0; ///< Authoritative server tick at which gameplay input unlocks.
    };

    /** @brief Explicit pre-start cancel edge sent when the current round start is aborted back to lobby. */
    struct MsgMatchCancelled
    {
        uint32_t matchId = 0; ///< Authoritative match identifier that was cancelled before start.
    };

    /** @brief Reliable end-of-match result edge sent before the automatic return to lobby. */
    struct MsgMatchResult
    {
        enum class EResult : uint8_t
        {
            Draw = 0x00,
            Win = 0x01
        };

        uint32_t matchId = 0;              ///< Authoritative match identifier that just ended.
        EResult result = EResult::Draw;    ///< End-of-match result type.
        uint8_t winnerPlayerId = 0xFF;     ///< Winning player id for `Win`, otherwise `0xFF`.
        char winnerName[kPlayerNameMax]{}; ///< Winner display name for `Win`, otherwise zeroed.
    };

    /**
     * @brief Passive lobby seat snapshot sent reliably by the server.
     *
     * Slots are encoded in stable player-id seat order so clients can render a
     * dynamic lobby list without inferring seat ownership from packet order.
     */
    struct MsgLobbyState
    {
        enum class EPhase : uint8_t
        {
            Idle = 0x00,
            Countdown = 0x01
        };

        struct SeatEntry
        {
            enum class ESeatFlags : uint8_t
            {
                None = 0x00,
                Occupied = 0x01,
                Ready = 0x02
            };

            static constexpr uint8_t kKnownFlags =
                static_cast<uint8_t>(ESeatFlags::Occupied) |
                static_cast<uint8_t>(ESeatFlags::Ready);

            ESeatFlags flags = ESeatFlags::None; ///< Occupancy/ready state for this player-id seat.
            uint8_t wins = 0;                    ///< Server-owned round wins for the current seat occupant.
            char name[kPlayerNameMax]{};         ///< Seat display name, NUL-terminated and zero-padded when occupied.
        };

        EPhase phase = EPhase::Idle;           ///< Lobby presentation phase.
        uint8_t countdownSecondsRemaining = 0; ///< Remaining whole countdown seconds during `Countdown`.
        uint16_t reserved = 0;                 ///< Reserved field. Must be zero in the current protocol version.
        SeatEntry seats[kMaxPlayers]{}; ///< Stable player-id keyed lobby seats.
    };

    static_assert(sizeof(MsgLobbyState::SeatEntry) == kMsgLobbyStateSeatSize, "MsgLobbyState::SeatEntry size mismatch");
    static_assert(sizeof(MsgLobbyState) == kMsgLobbyStateSize, "MsgLobbyState size mismatch");

    // ----- Gameplay Message Payloads -----

    /**
     * @brief Batched input payload sent by client each simulation tick.
     *
     * Carries up to @ref kMaxInputBatchSize input entries for redundancy.
     * Each entry is a button bitmask. Unused entries are zero-padded.
     */
    struct MsgInput
    {
        uint32_t baseInputSeq = 0;              ///< Sequence number of the first entry in the batch.
        uint8_t count = 0;                      ///< Number of valid entries (1..kMaxInputBatchSize).
        uint8_t inputs[kMaxInputBatchSize]{};   ///< Button bitmasks, zero-padded beyond count.
    };

    /** @brief Compact tile cell payload reused by reliable gameplay events. */
    struct MsgCell
    {
        uint8_t col = 0;                        ///< Tile-map column.
        uint8_t row = 0;                        ///< Tile-map row.
    };

    /**
     * @brief Snapshot payload broadcast by the server to all clients.
     *
     * Contains active authoritative round player state plus active bombs for
     * already-connected clients. Players are packed in ascending player-id
     * slot order. Bombs are packed in ascending cell order.
     *
     * Truth boundary today:
     * - authoritative for player positions / replicated player flags / active bomb membership
     * - not a payload for mid-match joins or recovery
     * - does not encode destroyed tiles, round-result presentation state, or
     *   enough world state to rebuild an in-progress round after disconnect
     *
     * @note The owning local player may still appear in the snapshot even when
     * client prediction is using owner corrections for local position.
     */
    struct MsgSnapshot
    {
        uint32_t matchId = 0;           ///< Authoritative match identifier that produced this snapshot.
        uint32_t serverTick = 0;        ///< Authoritative server tick at which this snapshot was produced.
        uint8_t playerCount = 0;        ///< Number of active entries stored in `players`.
        uint8_t bombCount = 0;          ///< Number of active entries stored in `bombs`.

        struct PlayerEntry
        {
            uint8_t playerId = 0;       ///< Player identifier [0, kMaxPlayers).
            int16_t xQ = 0;             ///< Player X position in tile-space Q8 (center position).
            int16_t yQ = 0;             ///< Player Y position in tile-space Q8 (center position).

            enum class EPlayerFlags : uint8_t
            {
                None = 0x00,            ///< No replicated player flags are set.
                Alive = 0x01,           ///< Player is alive if set, dead if unset.
                Invulnerable = 0x02,    ///< Player is invulnerable if set (e.g. spawn protection).
                InputLocked = 0x04      ///< Player input/movement is server-locked if set.
            };

            static constexpr uint8_t kKnownFlags =
                static_cast<uint8_t>(EPlayerFlags::Alive) |
                static_cast<uint8_t>(EPlayerFlags::Invulnerable) |
                static_cast<uint8_t>(EPlayerFlags::InputLocked);

            EPlayerFlags flags = EPlayerFlags::None; ///< Player alive status and other state flags.
        };

        PlayerEntry players[kMaxPlayers]; ///< Packed active-player entries; slots beyond `playerCount` are ignored.

        struct BombEntry
        {
            uint8_t ownerId = 0;        ///< Player identifier [0, kMaxPlayers) that owns this bomb.
            uint8_t col = 0;            ///< Tile-map column occupied by the bomb.
            uint8_t row = 0;            ///< Tile-map row occupied by the bomb.
            uint8_t radius = 0;         ///< Explosion radius snapped at placement time.
        };

        BombEntry bombs[kMaxSnapshotBombs]; ///< Packed active bombs; slots beyond `bombCount` are ignored.
    };

    /**
     * @brief Owner-only correction payload sent by the server.
     *
     * A correction acknowledges the highest input sequence the server has
     * processed for that player and carries the authoritative local position
     * at that point. It is not a broadcast world-state message.
     */
    struct MsgCorrection
    {
        uint32_t matchId = 0;               ///< Authoritative match identifier that produced this correction.
        uint32_t serverTick = 0;            ///< Server tick at which this correction applies.
        uint32_t lastProcessedInputSeq = 0; ///< Highest input seq the server has processed for this player.
        int16_t xQ = 0;                     ///< Corrected X position in tile-space Q8.
        int16_t yQ = 0;                     ///< Corrected Y position in tile-space Q8.
    };

    /**
     * @brief Reliable discrete bomb-placement event sent by the server.
     *
     * This message is presentation-focused. Snapshots remain the long-lived
     * authority for durable bomb existence and membership, so consumers that
     * merge both streams still need to respect authoritative server tick order
     * across channels.
     */
    struct MsgBombPlaced
    {
        uint32_t matchId = 0;               ///< Authoritative match identifier that accepted this bomb placement.
        uint32_t serverTick = 0;            ///< Server tick at which the bomb placement was accepted.
        uint32_t explodeTick = 0;           ///< Server tick at which this bomb is scheduled to explode.
        uint8_t ownerId = 0;                ///< Player identifier [0, kMaxPlayers) that owns this bomb.
        uint8_t col = 0;                    ///< Tile-map column occupied by the bomb.
        uint8_t row = 0;                    ///< Tile-map row occupied by the bomb.
        uint8_t radius = 0;                 ///< Explosion radius snapped at placement time.
    };

    /**
     * @brief Reliable authoritative explosion-resolution event sent by the server.
     *
     * Carries the resolved blast footprint and the set of bricks destroyed by
     * the detonation so connected clients can update world presentation
     * immediately. This complements snapshots because snapshots do not carry
     * destroyed-tile history for later admission or recovery.
     */
    struct MsgExplosionResolved
    {
        uint32_t matchId = 0;                   ///< Authoritative match identifier that resolved this explosion.
        uint32_t serverTick = 0;                ///< Server tick at which the bomb explosion resolved.
        uint8_t ownerId = 0;                    ///< Player identifier [0, kMaxPlayers) that owned the bomb.
        uint8_t originCol = 0;                  ///< Explosion origin column.
        uint8_t originRow = 0;                  ///< Explosion origin row.
        uint8_t radius = 0;                     ///< Radius snapped from the bomb that detonated.
        uint8_t killedPlayerMask = 0;           ///< Bitmask of player ids killed by this resolved explosion.
        uint8_t blastCellCount = 0;             ///< Number of valid entries stored in `blastCells`.
        uint8_t destroyedBrickCount = 0;        ///< Number of valid entries stored in `destroyedBricks`.
        MsgCell blastCells[kMaxExplosionBlastCells]{};              ///< Blast footprint cells in propagation order.
        MsgCell destroyedBricks[kMaxExplosionDestroyedBricks]{};    ///< Brick cells destroyed by this detonation.
    };

    // =================================================================================================================
    // ===== Wire Helpers ==============================================================================================
    // =================================================================================================================

    // ----- Endian Helpers -----

    /** @brief Writes a 16-bit value using little-endian encoding. */
    constexpr void writeU16LE(uint8_t* out, uint16_t value)
    {
        out[0] = static_cast<uint8_t>(value & 0xFFu);
        out[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    }

    /** @brief Writes a 32-bit value using little-endian encoding. */
    constexpr void writeU32LE(uint8_t* out, uint32_t value)
    {
        out[0] = static_cast<uint8_t>(value & 0xFFu);
        out[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
        out[2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
        out[3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
    }

    /** @brief Reads a 16-bit value encoded as little-endian. */
    constexpr uint16_t readU16LE(const uint8_t* in)
    {
        return static_cast<uint16_t>(in[0]) |
               (static_cast<uint16_t>(in[1]) << 8u);
    }

    /** @brief Reads a 32-bit value encoded as little-endian. */
    constexpr uint32_t readU32LE(const uint8_t* in)
    {
        return static_cast<uint32_t>(in[0]) |
               (static_cast<uint32_t>(in[1]) << 8u) |
               (static_cast<uint32_t>(in[2]) << 16u) |
               (static_cast<uint32_t>(in[3]) << 24u);
    }

    // ----- String Field Helpers -----

    /** @brief Returns string length capped to `maxBytes` for bounded C strings. */
    constexpr std::size_t boundedStrLen(const char* s, const std::size_t maxBytes)
    {
        if (!s) return 0;

        std::size_t length = 0;
        while(length < maxBytes && s[length] != '\0')
        {
            ++length;
        }
        return length;
    }

    /** @brief Returns the payload offset of the snapshot player entry at `index`. */
    constexpr std::size_t snapshotPlayerOffset(std::size_t index)
    {
        return kSnapshotPlayersOffset + index * kSnapshotPlayerEntrySize;
    }

    /** @brief Returns the payload offset of the snapshot bomb entry at `index`. */
    constexpr std::size_t snapshotBombOffset(std::size_t index)
    {
        return kSnapshotBombsOffset + index * kSnapshotBombEntrySize;
    }

    /** @brief Sets `MsgHello::name` from `string_view` with truncation and zero padding. */
    inline void setHelloName(MsgHello& hello, std::string_view name)
    {
        std::memset(hello.name, 0, kPlayerNameMax);

        const std::size_t copyLen = (name.size() < (kPlayerNameMax - 1)) ? name.size() : (kPlayerNameMax - 1);
        std::memcpy(hello.name, name.data(), copyLen);
    }
    /** @brief C-string overload of setHelloName. */
    inline void setHelloName(MsgHello& hello, const char* name)
    {
        if (!name) {
            std::memset(hello.name, 0, kPlayerNameMax);
            return;
        }

        const std::size_t copyLen = boundedStrLen(name, kPlayerNameMax - 1);
        setHelloName(hello, std::string_view{name, copyLen});
    }

    /** @brief Returns true when one lobby-seat flag field contains only known bits. */
    constexpr bool isValidLobbySeatFlags(const uint8_t flags)
    {
        return (flags & static_cast<uint8_t>(~MsgLobbyState::SeatEntry::kKnownFlags)) == 0;
    }

    /** @brief Returns true when one lobby-state phase field contains a known encoding. */
    constexpr bool isValidLobbyPhase(const uint8_t rawPhase)
    {
        return rawPhase == static_cast<uint8_t>(MsgLobbyState::EPhase::Idle) ||
               rawPhase == static_cast<uint8_t>(MsgLobbyState::EPhase::Countdown);
    }

    /** @brief Sets one lobby-seat display name with truncation and zero padding. */
    inline void setLobbySeatName(MsgLobbyState::SeatEntry& seat, std::string_view name)
    {
        std::memset(seat.name, 0, kPlayerNameMax);

        const std::size_t copyLen = (name.size() < (kPlayerNameMax - 1)) ? name.size() : (kPlayerNameMax - 1);
        std::memcpy(seat.name, name.data(), copyLen);
    }

    /** @brief Sets one match-result winner display name with truncation and zero padding. */
    inline void setMatchResultWinnerName(MsgMatchResult& matchResult, std::string_view name)
    {
        std::memset(matchResult.winnerName, 0, kPlayerNameMax);

        const std::size_t copyLen = (name.size() < (kPlayerNameMax - 1)) ? name.size() : (kPlayerNameMax - 1);
        std::memcpy(matchResult.winnerName, name.data(), copyLen);
    }

    /** @brief Returns the visible lobby-seat display name. */
    [[nodiscard]]
    inline std::string_view lobbySeatName(const MsgLobbyState::SeatEntry& seat)
    {
        return std::string_view(seat.name, boundedStrLen(seat.name, kPlayerNameMax - 1));
    }

    /** @brief Returns the visible winner display name carried by one match-result payload. */
    [[nodiscard]]
    inline std::string_view matchResultWinnerName(const MsgMatchResult& matchResult)
    {
        return std::string_view(matchResult.winnerName, boundedStrLen(matchResult.winnerName, kPlayerNameMax - 1));
    }

    /** @brief Returns true when one lobby seat is currently occupied. */
    constexpr bool lobbySeatIsOccupied(const MsgLobbyState::SeatEntry& seat)
    {
        return (static_cast<uint8_t>(seat.flags) &
                static_cast<uint8_t>(MsgLobbyState::SeatEntry::ESeatFlags::Occupied)) != 0;
    }

    /** @brief Returns true when one occupied lobby seat is marked ready. */
    constexpr bool lobbySeatIsReady(const MsgLobbyState::SeatEntry& seat)
    {
        return (static_cast<uint8_t>(seat.flags) &
                static_cast<uint8_t>(MsgLobbyState::SeatEntry::ESeatFlags::Ready)) != 0;
    }

    /** @brief Returns true when the authoritative lobby is in the visible pre-match countdown phase. */
    constexpr bool lobbyCountdownActive(const MsgLobbyState& lobbyState)
    {
        return lobbyState.phase == MsgLobbyState::EPhase::Countdown &&
               lobbyState.countdownSecondsRemaining > 0;
    }

    // =================================================================================================================
    // ===== Serialization =============================================================================================
    // =================================================================================================================

    // ----- Header Serialization -----

    /** @brief Serializes `PacketHeader` into `kPacketHeaderSize` bytes. */
    inline void serializeHeader(const PacketHeader& header, uint8_t* out) noexcept
    {
        out[0] = static_cast<uint8_t>(header.type);
        writeU16LE(out + 1, header.payloadSize);
    }

    /**
     * @brief Deserializes and validates PacketHeader.
     *
     * Validates type, payload bounds, and expected payload size for known message types.
     */
    [[nodiscard]]
    inline bool deserializeHeader(const uint8_t* in, std::size_t inSize, PacketHeader& outHeader)
    {
        if (inSize < kPacketHeaderSize)
        {
            return false;
        }

        const uint8_t rawType = in[0];
        if (!isValidMsgType(rawType))
        {
            return false;
        }

        const uint16_t payloadSize = readU16LE(in + 1);
        if (inSize != kPacketHeaderSize + payloadSize)
        {
            return false;
        }

        // Current protocol uses fixed-size payloads for all message types.
        const auto msgType = static_cast<EMsgType>(rawType);
        const std::size_t expected = expectedPayloadSize(msgType);
        if (expected > 0 && payloadSize != expected)
        {
            return false;
        }

        outHeader.type = msgType;
        outHeader.payloadSize = payloadSize;
        return true;
    }

    // ----- Control Payload Serialization -----

    /** @brief Serializes `MsgHello` to fixed-size wire payload. */
    inline void serializeMsgHello(const MsgHello& hello, uint8_t* out) noexcept
    {
        writeU16LE(out, hello.protocolVersion);
        std::memset(out + 2, 0, kPlayerNameMax);
        const std::size_t nameLen = boundedStrLen(hello.name, kPlayerNameMax - 1);
        std::memcpy(out + 2, hello.name, nameLen);
    }

    /** @brief Deserializes `MsgHello` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgHello(const uint8_t* in, std::size_t inSize, MsgHello& outHello)
    {
        if (inSize < kMsgHelloSize)
        {
            return false;
        }

        outHello.protocolVersion = readU16LE(in);
        std::memcpy(outHello.name, in + 2, kPlayerNameMax);

        outHello.name[kPlayerNameMax - 1] = '\0';

        const std::size_t n = boundedStrLen(outHello.name, kPlayerNameMax - 1);
        std::memset(outHello.name + n, 0, kPlayerNameMax - n);

        return true;
    }

    /** @brief Serializes `MsgWelcome` to fixed-size wire payload. */
    inline void serializeMsgWelcome(const MsgWelcome& welcome, uint8_t* out) noexcept
    {
        writeU16LE(out, welcome.protocolVersion);
        out[2] = welcome.playerId;
        writeU16LE(out + 3, welcome.serverTickRate);
    }

    /** @brief Deserializes `MsgWelcome` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgWelcome(const uint8_t* in, std::size_t inSize, MsgWelcome& outWelcome)
    {
        if (inSize < kMsgWelcomeSize)
        {
            return false;
        }
        outWelcome.protocolVersion = readU16LE(in);
        outWelcome.playerId = in[2];
        if (outWelcome.playerId >= kMaxPlayers)
        {
            return false;
        }
        outWelcome.serverTickRate = readU16LE(in + 3);
        return outWelcome.serverTickRate != 0;
    }

    /** @brief Serializes `MsgReject` to fixed-size wire payload. */
    inline void serializeMsgReject(const MsgReject& reject, uint8_t* out) noexcept
    {
        out[0] = static_cast<uint8_t>(reject.reason);
        writeU16LE(out + 1, reject.expectedProtocolVersion);
    }

    /** @brief Deserializes `MsgReject` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgReject(const uint8_t* in, std::size_t inSize, MsgReject& outReject)
    {
        if (inSize < kMsgRejectSize)
        {
            return false;
        }

        outReject.reason = static_cast<MsgReject::EReason>(in[0]);

        switch (outReject.reason)
        {
            case MsgReject::EReason::VersionMismatch:
                outReject.expectedProtocolVersion = readU16LE(in + 1);
                break;
            case MsgReject::EReason::ServerFull:
            case MsgReject::EReason::Banned:
            case MsgReject::EReason::GameInProgress:
            case MsgReject::EReason::Other:
                outReject.expectedProtocolVersion = 0;
                break;
            default:
                return false;
        }

        return true;
    }

    /** @brief Serializes `MsgLevelInfo` to fixed-size wire payload. */
    inline void serializeMsgLevelInfo(const MsgLevelInfo& info, uint8_t* out) noexcept
    {
        writeU32LE(out, info.matchId);
        writeU32LE(out + 4, info.mapSeed);
    }

    /** @brief Deserializes `MsgLevelInfo` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgLevelInfo(const uint8_t* in, std::size_t inSize, MsgLevelInfo& outInfo)
    {
        if (inSize < kMsgLevelInfoSize)
        {
            return false;
        }
        outInfo.matchId = readU32LE(in);
        outInfo.mapSeed = readU32LE(in + 4);
        if (outInfo.matchId == 0)
        {
            return false;
        }
        return true;
    }

    /** @brief Returns true when one lobby-ready value uses a known wire encoding. */
    constexpr bool isValidLobbyReadyValue(const uint8_t ready)
    {
        return ready <= 1u;
    }

    /** @brief Serializes `MsgLobbyReady` to a fixed-size wire payload. */
    inline void serializeMsgLobbyReady(const MsgLobbyReady& ready, uint8_t* out) noexcept
    {
        out[0] = ready.ready != 0 ? 1u : 0u;
    }

    /** @brief Deserializes `MsgLobbyReady` from a fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgLobbyReady(const uint8_t* in, std::size_t inSize, MsgLobbyReady& outReady)
    {
        if (inSize < kMsgLobbyReadySize)
        {
            return false;
        }

        if (!isValidLobbyReadyValue(in[0]))
        {
            return false;
        }

        outReady.ready = in[0];
        return true;
    }

    /** @brief Serializes `MsgMatchLoaded` to fixed-size wire payload. */
    inline void serializeMsgMatchLoaded(const MsgMatchLoaded& loaded, uint8_t* out) noexcept
    {
        writeU32LE(out, loaded.matchId);
    }

    /** @brief Deserializes `MsgMatchLoaded` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgMatchLoaded(const uint8_t* in, std::size_t inSize, MsgMatchLoaded& outLoaded)
    {
        if (inSize < kMsgMatchLoadedSize)
        {
            return false;
        }

        outLoaded.matchId = readU32LE(in);
        return outLoaded.matchId != 0;
    }

    /** @brief Serializes `MsgMatchStart` to fixed-size wire payload. */
    inline void serializeMsgMatchStart(const MsgMatchStart& matchStart, uint8_t* out) noexcept
    {
        writeU32LE(out, matchStart.matchId);
        writeU32LE(out + 4, matchStart.goShowServerTick);
        writeU32LE(out + 8, matchStart.unlockServerTick);
    }

    /** @brief Deserializes `MsgMatchStart` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgMatchStart(const uint8_t* in, std::size_t inSize, MsgMatchStart& outMatchStart)
    {
        if (inSize < kMsgMatchStartSize)
        {
            return false;
        }

        outMatchStart.matchId = readU32LE(in);
        outMatchStart.goShowServerTick = readU32LE(in + 4);
        outMatchStart.unlockServerTick = readU32LE(in + 8);
        return outMatchStart.matchId != 0 &&
               outMatchStart.goShowServerTick != 0 &&
               outMatchStart.unlockServerTick != 0 &&
               outMatchStart.goShowServerTick <= outMatchStart.unlockServerTick;
    }

    /** @brief Serializes `MsgMatchCancelled` to fixed-size wire payload. */
    inline void serializeMsgMatchCancelled(const MsgMatchCancelled& cancelled, uint8_t* out) noexcept
    {
        writeU32LE(out, cancelled.matchId);
    }

    /** @brief Deserializes `MsgMatchCancelled` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgMatchCancelled(const uint8_t* in,
                                             std::size_t inSize,
                                             MsgMatchCancelled& outCancelled)
    {
        if (inSize < kMsgMatchCancelledSize)
        {
            return false;
        }

        outCancelled.matchId = readU32LE(in);
        return outCancelled.matchId != 0;
    }

    /** @brief Serializes `MsgMatchResult` to fixed-size wire payload. */
    inline void serializeMsgMatchResult(const MsgMatchResult& matchResult, uint8_t* out) noexcept
    {
        writeU32LE(out, matchResult.matchId);
        out[4] = static_cast<uint8_t>(matchResult.result);
        out[5] = matchResult.winnerPlayerId;
        std::memset(out + 6, 0, kPlayerNameMax);
        const std::size_t nameLen = boundedStrLen(matchResult.winnerName, kPlayerNameMax - 1);
        std::memcpy(out + 6, matchResult.winnerName, nameLen);
    }

    /** @brief Deserializes `MsgMatchResult` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgMatchResult(const uint8_t* in, std::size_t inSize, MsgMatchResult& outMatchResult)
    {
        if (inSize < kMsgMatchResultSize)
        {
            return false;
        }

        outMatchResult.matchId = readU32LE(in);
        outMatchResult.result = static_cast<MsgMatchResult::EResult>(in[4]);
        outMatchResult.winnerPlayerId = in[5];
        std::memcpy(outMatchResult.winnerName, in + 6, kPlayerNameMax);
        outMatchResult.winnerName[kPlayerNameMax - 1] = '\0';

        switch (outMatchResult.result)
        {
            case MsgMatchResult::EResult::Draw:
                outMatchResult.winnerPlayerId = 0xFF;
                std::memset(outMatchResult.winnerName, 0, kPlayerNameMax);
                return outMatchResult.matchId != 0;
            case MsgMatchResult::EResult::Win:
                if (outMatchResult.matchId == 0 || outMatchResult.winnerPlayerId >= kMaxPlayers)
                {
                    return false;
                }
                return !matchResultWinnerName(outMatchResult).empty();
            default:
                return false;
        }
    }

    /** @brief Serializes `MsgLobbyState` to fixed-size wire payload. */
    inline void serializeMsgLobbyState(const MsgLobbyState& lobbyState, uint8_t* out) noexcept
    {
        out[0] = static_cast<uint8_t>(lobbyState.phase);
        out[1] = lobbyState.countdownSecondsRemaining;
        writeU16LE(out + 2, 0);

        for (std::size_t i = 0; i < kMaxPlayers; ++i)
        {
            const auto offset = 4 + i * kMsgLobbyStateSeatSize;
            const auto& seat = lobbyState.seats[i];

            out[offset] = static_cast<uint8_t>(seat.flags);
            out[offset + 1] = seat.wins;
            std::memset(out + offset + 2, 0, kPlayerNameMax);
            const std::size_t nameLen = boundedStrLen(seat.name, kPlayerNameMax - 1);
            std::memcpy(out + offset + 2, seat.name, nameLen);
        }
    }

    /** @brief Deserializes `MsgLobbyState` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgLobbyState(const uint8_t* in, std::size_t inSize, MsgLobbyState& outLobbyState)
    {
        if (inSize < kMsgLobbyStateSize)
        {
            return false;
        }

        if (!isValidLobbyPhase(in[0]))
        {
            return false;
        }

        outLobbyState.phase = static_cast<MsgLobbyState::EPhase>(in[0]);
        outLobbyState.countdownSecondsRemaining = in[1];
        outLobbyState.reserved = readU16LE(in + 2);
        if (outLobbyState.reserved != 0)
        {
            return false;
        }
        if (outLobbyState.phase == MsgLobbyState::EPhase::Countdown)
        {
            if (outLobbyState.countdownSecondsRemaining == 0)
            {
                return false;
            }
        }
        else
        {
            outLobbyState.countdownSecondsRemaining = 0;
        }

        for (std::size_t i = 0; i < kMaxPlayers; ++i)
        {
            const auto offset = 4 + i * kMsgLobbyStateSeatSize;
            auto& seat = outLobbyState.seats[i];

            const uint8_t rawFlags = in[offset];
            if (!isValidLobbySeatFlags(rawFlags))
            {
                return false;
            }
            if ((rawFlags & static_cast<uint8_t>(MsgLobbyState::SeatEntry::ESeatFlags::Ready)) != 0 &&
                (rawFlags & static_cast<uint8_t>(MsgLobbyState::SeatEntry::ESeatFlags::Occupied)) == 0)
            {
                return false;
            }

            seat.flags = static_cast<MsgLobbyState::SeatEntry::ESeatFlags>(rawFlags);
            seat.wins = in[offset + 1];
            std::memcpy(seat.name, in + offset + 2, kPlayerNameMax);
            seat.name[kPlayerNameMax - 1] = '\0';

            const std::size_t nameLen = boundedStrLen(seat.name, kPlayerNameMax - 1);
            std::memset(seat.name + nameLen, 0, kPlayerNameMax - nameLen);

            if (!lobbySeatIsOccupied(seat))
            {
                seat.wins = 0;
                std::memset(seat.name, 0, kPlayerNameMax);
                continue;
            }

        }

        return true;
    }

    // ----- Gameplay Payload Serialization -----

    /**
     * @brief Serializes `MsgInput` to its fixed-size wire payload.
     *
     * @note Callers are expected to keep entries beyond `count` zero-padded.
     */
    inline void serializeMsgInput(const MsgInput& input, uint8_t* out) noexcept
    {
        writeU32LE(out, input.baseInputSeq);
        out[4] = input.count;
        std::memcpy(out + 5, input.inputs, kMaxInputBatchSize);
    }

    /** @brief Deserializes `MsgInput` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgInput(const uint8_t* in, std::size_t inSize, MsgInput& outInput)
    {
        if (inSize < kMsgInputSize)
        {
            return false;
        }

        outInput.baseInputSeq = readU32LE(in);
        outInput.count = in[4];

        if (outInput.count == 0 || outInput.count > kMaxInputBatchSize)
        {
            return false;
        }

        // Reject if the batch starts before the first valid sequence.
        if (outInput.baseInputSeq < kFirstInputSeq)
        {
            return false;
        }

        std::memcpy(outInput.inputs, in + 5, kMaxInputBatchSize);

        // Reject if any input entry contains unknown bits outside the defined input mask.
        for (uint8_t i = 0; i < outInput.count; ++i)
        {
            if ((outInput.inputs[i] & ~kInputKnownBits) != 0)
            {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Serializes `MsgSnapshot` to its fixed-size wire payload.
     *
     * @note Callers are expected to keep entries beyond `playerCount`
     * zero-initialized because the current wire format always writes all
     * `kMaxPlayers` slots.
     */
    inline void serializeMsgSnapshot(const MsgSnapshot& snap, uint8_t* out) noexcept
    {
        writeU32LE(out, snap.matchId);
        writeU32LE(out + 4, snap.serverTick);
        const uint8_t playerCount = (snap.playerCount <= kMaxPlayers) ? snap.playerCount : kMaxPlayers;
        const uint8_t bombCount = (snap.bombCount <= kMaxSnapshotBombs) ? snap.bombCount : kMaxSnapshotBombs;
        out[8] = playerCount;
        out[9] = bombCount;

        for (std::size_t i = 0; i < kMaxPlayers; ++i)
        {
            const auto& player = snap.players[i];
            const std::size_t offset = snapshotPlayerOffset(i);
            out[offset] = player.playerId;
            writeU16LE(out + offset + 1, static_cast<uint16_t>(player.xQ));
            writeU16LE(out + offset + 3, static_cast<uint16_t>(player.yQ));
            out[offset + 5] = static_cast<uint8_t>(player.flags);
        }

        for (std::size_t i = 0; i < kMaxSnapshotBombs; ++i)
        {
            const auto& bomb = snap.bombs[i];
            const std::size_t offset = snapshotBombOffset(i);
            out[offset] = bomb.ownerId;
            out[offset + 1] = bomb.col;
            out[offset + 2] = bomb.row;
            out[offset + 3] = bomb.radius;
        }
    }

    /** @brief Deserializes `MsgSnapshot` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgSnapshot(const uint8_t* in, std::size_t inSize, MsgSnapshot& outSnap)
    {
        if (inSize < kMsgSnapshotSize)
        {
            return false;
        }

        outSnap.matchId = readU32LE(in);
        outSnap.serverTick = readU32LE(in + 4);
        outSnap.playerCount = in[8];
        outSnap.bombCount = in[9];
        if (outSnap.playerCount > kMaxPlayers)
        {
            return false;
        }
        if (outSnap.bombCount > kMaxSnapshotBombs)
        {
            return false;
        }

        uint8_t seenPlayerMask = 0;
        uint8_t previousPlayerId = 0;
        bool hasPreviousPlayer = false;

        for (std::size_t i = 0; i < kMaxPlayers; ++i)
        {
            const std::size_t offset = snapshotPlayerOffset(i);
            auto& player = outSnap.players[i];
            player.playerId = in[offset];
            player.xQ = static_cast<int16_t>(readU16LE(in + offset + 1));
            player.yQ = static_cast<int16_t>(readU16LE(in + offset + 3));
            const uint8_t rawFlags = in[offset + 5];

            // Only validate active player entries.
            if (i < outSnap.playerCount)
            {
                if (player.playerId >= kMaxPlayers)
                {
                    return false;
                }
                if (hasPreviousPlayer && player.playerId <= previousPlayerId)
                {
                    return false;
                }

                const uint8_t playerBit = static_cast<uint8_t>(1u << player.playerId);
                if ((seenPlayerMask & playerBit) != 0)
                {
                    return false;
                }
                seenPlayerMask |= playerBit;
                previousPlayerId = player.playerId;
                hasPreviousPlayer = true;

                if ((rawFlags & ~MsgSnapshot::PlayerEntry::kKnownFlags) != 0)
                {
                    return false;
                }
            }

            player.flags = static_cast<MsgSnapshot::PlayerEntry::EPlayerFlags>(rawFlags);
        }

        uint16_t previousBombCellKey = 0;
        bool hasPreviousBomb = false;
        for (std::size_t i = 0; i < kMaxSnapshotBombs; ++i)
        {
            const std::size_t offset = snapshotBombOffset(i);
            auto& bomb = outSnap.bombs[i];
            bomb.ownerId = in[offset];
            bomb.col = in[offset + 1];
            bomb.row = in[offset + 2];
            bomb.radius = in[offset + 3];

            if (i < outSnap.bombCount)
            {
                if (bomb.ownerId >= kMaxPlayers)
                {
                    return false;
                }
                if (bomb.col >= ::bomberman::tileArrayWidth || bomb.row >= ::bomberman::tileArrayHeight)
                {
                    return false;
                }
                if (bomb.radius == 0)
                {
                    return false;
                }

                const uint16_t bombCellKey =
                    static_cast<uint16_t>(bomb.row) * static_cast<uint16_t>(::bomberman::tileArrayWidth) +
                    static_cast<uint16_t>(bomb.col);
                if (hasPreviousBomb && bombCellKey <= previousBombCellKey)
                {
                    return false;
                }

                previousBombCellKey = bombCellKey;
                hasPreviousBomb = true;
            }
        }

        return true;
    }

    /** @brief Serializes `MsgCorrection` to fixed-size wire payload. */
    inline void serializeMsgCorrection(const MsgCorrection& corr, uint8_t* out) noexcept
    {
        writeU32LE(out, corr.matchId);
        writeU32LE(out + 4, corr.serverTick);
        writeU32LE(out + 8, corr.lastProcessedInputSeq);
        writeU16LE(out + 12, static_cast<uint16_t>(corr.xQ));
        writeU16LE(out + 14, static_cast<uint16_t>(corr.yQ));
    }

    /** @brief Deserializes `MsgCorrection` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgCorrection(const uint8_t* in, std::size_t inSize, MsgCorrection& outCorr)
    {
        if (inSize < kMsgCorrectionSize)
        {
            return false;
        }
        outCorr.matchId = readU32LE(in);
        outCorr.serverTick = readU32LE(in + 4);
        outCorr.lastProcessedInputSeq = readU32LE(in + 8);
        outCorr.xQ = static_cast<int16_t>(readU16LE(in + 12));
        outCorr.yQ = static_cast<int16_t>(readU16LE(in + 14));
        return true;
    }

    /** @brief Serializes `MsgBombPlaced` to fixed-size wire payload. */
    inline void serializeMsgBombPlaced(const MsgBombPlaced& bombPlaced, uint8_t* out) noexcept
    {
        writeU32LE(out, bombPlaced.matchId);
        writeU32LE(out + 4, bombPlaced.serverTick);
        writeU32LE(out + 8, bombPlaced.explodeTick);
        out[12] = bombPlaced.ownerId;
        out[13] = bombPlaced.col;
        out[14] = bombPlaced.row;
        out[15] = bombPlaced.radius;
    }

    /** @brief Deserializes `MsgBombPlaced` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgBombPlaced(const uint8_t* in, std::size_t inSize, MsgBombPlaced& outBombPlaced)
    {
        if (inSize < kMsgBombPlacedSize)
        {
            return false;
        }

        outBombPlaced.matchId = readU32LE(in);
        outBombPlaced.serverTick = readU32LE(in + 4);
        outBombPlaced.explodeTick = readU32LE(in + 8);
        outBombPlaced.ownerId = in[12];
        outBombPlaced.col = in[13];
        outBombPlaced.row = in[14];
        outBombPlaced.radius = in[15];

        if (outBombPlaced.ownerId >= kMaxPlayers)
        {
            return false;
        }
        if (!isValidTileCell(outBombPlaced.col, outBombPlaced.row))
        {
            return false;
        }
        if (outBombPlaced.radius == 0 || outBombPlaced.explodeTick < outBombPlaced.serverTick)
        {
            return false;
        }

        return true;
    }

    /** @brief Serializes `MsgExplosionResolved` to fixed-size wire payload. */
    inline void serializeMsgExplosionResolved(const MsgExplosionResolved& explosion, uint8_t* out) noexcept
    {
        writeU32LE(out, explosion.matchId);
        writeU32LE(out + 4, explosion.serverTick);
        out[8] = explosion.ownerId;
        out[9] = explosion.originCol;
        out[10] = explosion.originRow;
        out[11] = explosion.radius;
        out[12] = explosion.killedPlayerMask;
        out[13] = static_cast<uint8_t>((explosion.blastCellCount <= kMaxExplosionBlastCells) ?
            explosion.blastCellCount :
            kMaxExplosionBlastCells);
        out[14] = static_cast<uint8_t>((explosion.destroyedBrickCount <= kMaxExplosionDestroyedBricks) ?
            explosion.destroyedBrickCount :
            kMaxExplosionDestroyedBricks);

        for (std::size_t i = 0; i < kMaxExplosionBlastCells; ++i)
        {
            const std::size_t offset = explosionBlastCellOffset(i);
            out[offset] = explosion.blastCells[i].col;
            out[offset + 1] = explosion.blastCells[i].row;
        }

        for (std::size_t i = 0; i < kMaxExplosionDestroyedBricks; ++i)
        {
            const std::size_t offset = explosionDestroyedBrickOffset(i);
            out[offset] = explosion.destroyedBricks[i].col;
            out[offset + 1] = explosion.destroyedBricks[i].row;
        }
    }

    /** @brief Deserializes `MsgExplosionResolved` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgExplosionResolved(const uint8_t* in,
                                                std::size_t inSize,
                                                MsgExplosionResolved& outExplosion)
    {
        if (inSize < kMsgExplosionResolvedSize)
        {
            return false;
        }

        outExplosion.matchId = readU32LE(in);
        outExplosion.serverTick = readU32LE(in + 4);
        outExplosion.ownerId = in[8];
        outExplosion.originCol = in[9];
        outExplosion.originRow = in[10];
        outExplosion.radius = in[11];
        outExplosion.killedPlayerMask = in[12];
        outExplosion.blastCellCount = in[13];
        outExplosion.destroyedBrickCount = in[14];

        if (outExplosion.ownerId >= kMaxPlayers)
        {
            return false;
        }
        if (!isValidTileCell(outExplosion.originCol, outExplosion.originRow))
        {
            return false;
        }
        if (outExplosion.radius == 0)
        {
            return false;
        }
        if (!isValidPlayerMask(outExplosion.killedPlayerMask))
        {
            return false;
        }
        if (outExplosion.blastCellCount == 0 || outExplosion.blastCellCount > kMaxExplosionBlastCells)
        {
            return false;
        }
        if (outExplosion.destroyedBrickCount > kMaxExplosionDestroyedBricks ||
            outExplosion.destroyedBrickCount > outExplosion.blastCellCount)
        {
            return false;
        }

        std::array<bool, static_cast<std::size_t>(::bomberman::tileArrayWidth) *
                              static_cast<std::size_t>(::bomberman::tileArrayHeight)> seenBlastCells{};
        std::array<bool, static_cast<std::size_t>(::bomberman::tileArrayWidth) *
                              static_cast<std::size_t>(::bomberman::tileArrayHeight)> seenDestroyedBricks{};

        for (std::size_t i = 0; i < kMaxExplosionBlastCells; ++i)
        {
            const std::size_t offset = explosionBlastCellOffset(i);
            auto& cell = outExplosion.blastCells[i];
            cell.col = in[offset];
            cell.row = in[offset + 1];

            if (i < outExplosion.blastCellCount)
            {
                if (!isValidTileCell(cell.col, cell.row))
                {
                    return false;
                }

                const std::size_t cellIndex = tileCellKey(cell.col, cell.row);
                if (seenBlastCells[cellIndex])
                {
                    return false;
                }

                seenBlastCells[cellIndex] = true;
            }
        }

        if (outExplosion.blastCells[0].col != outExplosion.originCol ||
            outExplosion.blastCells[0].row != outExplosion.originRow)
        {
            return false;
        }

        for (std::size_t i = 0; i < kMaxExplosionDestroyedBricks; ++i)
        {
            const std::size_t offset = explosionDestroyedBrickOffset(i);
            auto& cell = outExplosion.destroyedBricks[i];
            cell.col = in[offset];
            cell.row = in[offset + 1];

            if (i < outExplosion.destroyedBrickCount)
            {
                if (!isValidTileCell(cell.col, cell.row))
                {
                    return false;
                }

                const std::size_t cellIndex = tileCellKey(cell.col, cell.row);
                if (!seenBlastCells[cellIndex] || seenDestroyedBricks[cellIndex])
                {
                    return false;
                }

                seenDestroyedBricks[cellIndex] = true;
            }
        }

        return true;
    }

    // =================================================================================================================
    // ===== Packet Builders ===========================================================================================
    // =================================================================================================================

    // ----- Control Packet Builders -----

    /** @brief Builds a full Hello packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> makeHelloPacket(const MsgHello& hello)
    {
        PacketHeader header{};
        header.type = EMsgType::Hello;
        header.payloadSize = static_cast<uint16_t>(kMsgHelloSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgHello(hello, bytes.data() + kPacketHeaderSize);

        return bytes;
    }
    /** @brief Convenience overload building Hello from name and version. */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> makeHelloPacket(std::string_view name, uint16_t protocolVersion)
    {
        MsgHello hello{};
        hello.protocolVersion = protocolVersion;
        setHelloName(hello, name);
        return makeHelloPacket(hello);
    }

    /** @brief Builds a full Welcome packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgWelcomeSize> makeWelcomePacket(const MsgWelcome& welcome)
    {
        PacketHeader header{};
        header.type = EMsgType::Welcome;
        header.payloadSize = static_cast<uint16_t>(kMsgWelcomeSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgWelcomeSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgWelcome(welcome, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full Reject packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgRejectSize> makeRejectPacket(const MsgReject& reject)
    {
        PacketHeader header{};
        header.type = EMsgType::Reject;
        header.payloadSize = static_cast<uint16_t>(kMsgRejectSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgRejectSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgReject(reject, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full LevelInfo packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgLevelInfoSize> makeLevelInfoPacket(const MsgLevelInfo& info)
    {
        PacketHeader header{};
        header.type = EMsgType::LevelInfo;
        header.payloadSize = static_cast<uint16_t>(kMsgLevelInfoSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgLevelInfoSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgLevelInfo(info, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full LobbyReady packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgLobbyReadySize> makeLobbyReadyPacket(const MsgLobbyReady& ready)
    {
        PacketHeader header{};
        header.type = EMsgType::LobbyReady;
        header.payloadSize = static_cast<uint16_t>(kMsgLobbyReadySize);

        std::array<uint8_t, kPacketHeaderSize + kMsgLobbyReadySize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgLobbyReady(ready, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Convenience overload building LobbyReady directly from the desired ready state. */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgLobbyReadySize> makeLobbyReadyPacket(const bool ready)
    {
        MsgLobbyReady msg{};
        msg.ready = ready ? 1u : 0u;
        return makeLobbyReadyPacket(msg);
    }

    /** @brief Builds a full MatchLoaded packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgMatchLoadedSize> makeMatchLoadedPacket(const MsgMatchLoaded& loaded)
    {
        PacketHeader header{};
        header.type = EMsgType::MatchLoaded;
        header.payloadSize = static_cast<uint16_t>(kMsgMatchLoadedSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgMatchLoadedSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgMatchLoaded(loaded, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Convenience overload building MatchLoaded directly from a match identifier. */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgMatchLoadedSize> makeMatchLoadedPacket(const uint32_t matchId)
    {
        MsgMatchLoaded loaded{};
        loaded.matchId = matchId;
        return makeMatchLoadedPacket(loaded);
    }

    /** @brief Builds a full MatchStart packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgMatchStartSize> makeMatchStartPacket(const MsgMatchStart& matchStart)
    {
        PacketHeader header{};
        header.type = EMsgType::MatchStart;
        header.payloadSize = static_cast<uint16_t>(kMsgMatchStartSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgMatchStartSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgMatchStart(matchStart, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Convenience overload building MatchStart directly from match timing values. */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgMatchStartSize> makeMatchStartPacket(const uint32_t matchId,
                                                                                             const uint32_t goShowServerTick,
                                                                                             const uint32_t unlockServerTick)
    {
        MsgMatchStart matchStart{};
        matchStart.matchId = matchId;
        matchStart.goShowServerTick = goShowServerTick;
        matchStart.unlockServerTick = unlockServerTick;
        return makeMatchStartPacket(matchStart);
    }

    /** @brief Builds a full MatchCancelled packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgMatchCancelledSize> makeMatchCancelledPacket(
        const MsgMatchCancelled& cancelled)
    {
        PacketHeader header{};
        header.type = EMsgType::MatchCancelled;
        header.payloadSize = static_cast<uint16_t>(kMsgMatchCancelledSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgMatchCancelledSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgMatchCancelled(cancelled, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Convenience overload building MatchCancelled directly from a match identifier. */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgMatchCancelledSize> makeMatchCancelledPacket(
        const uint32_t matchId)
    {
        MsgMatchCancelled cancelled{};
        cancelled.matchId = matchId;
        return makeMatchCancelledPacket(cancelled);
    }

    /** @brief Builds a full MatchResult packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgMatchResultSize> makeMatchResultPacket(
        const MsgMatchResult& matchResult)
    {
        PacketHeader header{};
        header.type = EMsgType::MatchResult;
        header.payloadSize = static_cast<uint16_t>(kMsgMatchResultSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgMatchResultSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgMatchResult(matchResult, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full LobbyState packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgLobbyStateSize> makeLobbyStatePacket(const MsgLobbyState& lobbyState)
    {
        PacketHeader header{};
        header.type = EMsgType::LobbyState;
        header.payloadSize = static_cast<uint16_t>(kMsgLobbyStateSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgLobbyStateSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgLobbyState(lobbyState, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    // ----- Gameplay Packet Builders -----

    /** @brief Builds a full Input packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgInputSize> makeInputPacket(const MsgInput& input)
    {
        PacketHeader header{};
        header.type = EMsgType::Input;
        header.payloadSize = static_cast<uint16_t>(kMsgInputSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgInputSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgInput(input, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full Snapshot packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgSnapshotSize> makeSnapshotPacket(const MsgSnapshot& snap)
    {
        PacketHeader header{};
        header.type = EMsgType::Snapshot;
        header.payloadSize = static_cast<uint16_t>(kMsgSnapshotSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgSnapshotSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgSnapshot(snap, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full Correction packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgCorrectionSize> makeCorrectionPacket(const MsgCorrection& corr)
    {
        PacketHeader header{};
        header.type = EMsgType::Correction;
        header.payloadSize = static_cast<uint16_t>(kMsgCorrectionSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgCorrectionSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgCorrection(corr, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full BombPlaced packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgBombPlacedSize> makeBombPlacedPacket(const MsgBombPlaced& bombPlaced)
    {
        PacketHeader header{};
        header.type = EMsgType::BombPlaced;
        header.payloadSize = static_cast<uint16_t>(kMsgBombPlacedSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgBombPlacedSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgBombPlaced(bombPlaced, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full ExplosionResolved packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgExplosionResolvedSize> makeExplosionResolvedPacket(
        const MsgExplosionResolved& explosion)
    {
        PacketHeader header{};
        header.type = EMsgType::ExplosionResolved;
        header.payloadSize = static_cast<uint16_t>(kMsgExplosionResolvedSize);

        std::array<uint8_t, kPacketHeaderSize + kMsgExplosionResolvedSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgExplosionResolved(explosion, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETCOMMON_H
