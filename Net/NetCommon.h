#ifndef BOMBERMAN_NET_NETCOMMON_H
#define BOMBERMAN_NET_NETCOMMON_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

/**
 * @brief Shared protocol constants, message types, and wire helpers.
 *
 * The protocol requires strict version match during handshake.
 * When changing wire layout, update:
 * 1. kProtocolVersion
 * 2. Size constants and static_assert checks
 * 3. expectedPayloadSize()
 * 4. Affected serializers and deserializers
 */

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Protocol Constants ========================================================================================
    // =================================================================================================================

    // -----------------------------------------------
    constexpr uint16_t      kProtocolVersion = 2;
    // -----------------------------------------------

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

    /** @brief First valid input sequence number. Seq 0 means "no input received yet". */
    constexpr uint32_t      kFirstInputSeq = 1;

    /** @brief Maximum number of inputs in a single batched MsgInput packet. */
    constexpr uint8_t       kMaxInputBatchSize = 16;

    // =================================================================================================================
    // ===== Input Bitmask Constants ===================================================================================
    // =================================================================================================================

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

    // =================================================================================================================
    // ===== ENet Channels =============================================================================================
    // =================================================================================================================

    /** @brief ENet channel identifiers. */
    enum class EChannel : uint8_t
    {
        ControlReliable      = 0,
        GameReliable         = 1,
        InputUnreliable      = 2,
        SnapshotUnreliable   = 3,
        CorrectionUnreliable = 4
    };
    constexpr std::size_t kChannelCount = 5;

    /** @brief Returns a human-readable name for a channel ID. */
    constexpr std::string_view channelName(uint8_t id)
    {
        switch (id)
        {
            case static_cast<uint8_t>(EChannel::ControlReliable):      return "ControlReliable";
            case static_cast<uint8_t>(EChannel::GameReliable):         return "GameReliable";
            case static_cast<uint8_t>(EChannel::InputUnreliable):      return "InputUnreliable";
            case static_cast<uint8_t>(EChannel::SnapshotUnreliable):   return "SnapshotUnreliable";
            case static_cast<uint8_t>(EChannel::CorrectionUnreliable): return "CorrectionUnreliable";
            default:                                                   return "Unknown";
        }
    }

    // =================================================================================================================
    // ===== Wire Size Constants =======================================================================================
    // =================================================================================================================

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

    constexpr std::size_t kMsgInputSize =
        sizeof(uint32_t) + // baseInputSeq
        sizeof(uint8_t) +  // count
        kMaxInputBatchSize; // inputs[kMaxInputBatchSize]

    constexpr std::size_t kMsgLevelInfoSize =
        sizeof(uint32_t);  // mapSeed

    constexpr std::size_t kMsgSnapshotSize =
        sizeof(uint32_t) + // serverTick
        sizeof(uint8_t) +  // playerCount
        kMaxPlayers *
            (sizeof(uint8_t) +   // playerId
             sizeof(int16_t) +   // xQ
             sizeof(int16_t) +   // yQ
             sizeof(uint8_t));   // flags

    constexpr std::size_t kMsgCorrectionSize =
        sizeof(uint32_t) + // serverTick
        sizeof(uint32_t) + // lastProcessedInputSeq
        sizeof(int16_t) +  // xQ
        sizeof(int16_t);   // yQ

    /** @brief Compile-time checks for expected wire sizes. */
    static_assert(sizeof(char) == 1, "Unexpected char size");

    static_assert(kPacketHeaderSize  == 3,   "PacketHeader size mismatch");
    static_assert(kMsgHelloSize      == 18,  "MsgHello size mismatch");
    static_assert(kMsgWelcomeSize    == 5,   "MsgWelcome size mismatch");
    static_assert(kMsgRejectSize     == 3,   "MsgReject size mismatch");
    static_assert(kMsgInputSize      == 21,  "MsgInput size mismatch");
    static_assert(kMsgLevelInfoSize  == 4,   "MsgLevelInfo size mismatch");
    static_assert(kMsgSnapshotSize   == 29,  "MsgSnapshot size mismatch");
    static_assert(kMsgCorrectionSize == 12,  "MsgCorrection size mismatch");

    // =================================================================================================================
    // ===== Message Types =============================================================================================
    // =================================================================================================================

    /** @brief Message type identifiers used in packet headers. */
    enum class EMsgType : uint8_t
    {
        Hello      = 0x01,
        Welcome    = 0x02,
        Reject     = 0x03,
        LevelInfo  = 0x04,

        Input      = 0x10,
        Snapshot   = 0x11,
        Correction = 0x12
    };

    /** @brief Checks if a raw byte value corresponds to a valid EMsgType. */
    inline bool isValidMsgType(uint8_t raw) {
        return raw == static_cast<uint8_t>(EMsgType::Hello)      ||
               raw == static_cast<uint8_t>(EMsgType::Welcome)    ||
               raw == static_cast<uint8_t>(EMsgType::Reject)     ||
               raw == static_cast<uint8_t>(EMsgType::LevelInfo)  ||
               raw == static_cast<uint8_t>(EMsgType::Input)      ||
               raw == static_cast<uint8_t>(EMsgType::Snapshot)   ||
               raw == static_cast<uint8_t>(EMsgType::Correction);
    }

    /** @brief Returns a human-readable name for a protocol message type. */
    constexpr std::string_view msgTypeName(EMsgType type)
    {
        switch (type)
        {
            case EMsgType::Hello:      return "Hello";
            case EMsgType::Welcome:    return "Welcome";
            case EMsgType::Reject:     return "Reject";
            case EMsgType::LevelInfo:  return "LevelInfo";
            case EMsgType::Input:      return "Input";
            case EMsgType::Snapshot:   return "Snapshot";
            case EMsgType::Correction: return "Correction";
            default:                   return "Unknown";
        }
    }

    /** @brief Returns the expected ENet channel for a given protocol message type. */
    constexpr EChannel expectedChannelFor(EMsgType type)
    {
        switch (type)
        {
            case EMsgType::Hello:
            case EMsgType::Welcome:
            case EMsgType::Reject:
            case EMsgType::LevelInfo:
                return EChannel::ControlReliable;
            case EMsgType::Input:
                return EChannel::InputUnreliable;
            case EMsgType::Snapshot:
                return EChannel::SnapshotUnreliable;
            case EMsgType::Correction:
                return EChannel::CorrectionUnreliable;
            default:
                return EChannel::ControlReliable;
        }
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
            case EMsgType::Input:      return kMsgInputSize;
            case EMsgType::Snapshot:   return kMsgSnapshotSize;
            case EMsgType::Correction: return kMsgCorrectionSize;
            default:                   return 0;
        }
    }

    // =================================================================================================================
    // ===== Wire Payload Types ========================================================================================
    // =================================================================================================================

    /** @brief Packet metadata prefix present on every wire message. */
    struct PacketHeader
    {
        EMsgType type{};          ///< Message type identifier.
        uint16_t payloadSize = 0; ///< Payload size in bytes, excluding header.
    };

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

    /**
     * @brief Welcome payload sent by server in response to Hello.
     *
     * @todo Extend this message with a reconnect token once the server keeps
     * durable player reservations across in-match disconnects.
     */
    struct MsgWelcome
    {
        uint16_t protocolVersion; ///< Server protocol version.
        uint8_t playerId;         ///< Server-assigned player identifier [0, kMaxPlayers).
        uint16_t serverTickRate;  ///< Authoritative server simulation tick rate.
    };

    /** @brief Reject payload sent by server in response to Hello on failure. */
    struct MsgReject
    {
        enum class EReason : uint8_t
        {
            VersionMismatch = 0x01,
            ServerFull = 0x02,
            Banned = 0x03,
            // TODO: Game Ongoing
            Other = 0xFF
        };

        EReason reason = EReason::Other; ///< Rejection reason code.

        uint16_t expectedProtocolVersion = 0; ///< Expected protocol version.
    };

    /**
    * @brief Level setup payload sent reliably by server after Welcome.
    */
    struct MsgLevelInfo
    {
        uint32_t mapSeed = 0; ///< 32-bit seed for the shared tile-map generator.
    };

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

    /** @brief Snapshot payload broadcast by server to all clients each simulation tick. */
    struct MsgSnapshot
    {
        uint32_t serverTick = 0;        ///< Authoritative server tick at which this snapshot was produced.
        uint8_t playerCount = 0;        ///< Number of active players in the game.

        struct PlayerEntry
        {
            uint8_t playerId = 0;       ///< Player identifier [0, kMaxPlayers).
            int16_t xQ = 0;             ///< Player X position in tile-space Q8 (xQ = tileColumn * 256).
            int16_t yQ = 0;             ///< Player Y position in tile-space Q8 (yQ = tileRow    * 256).

            enum class EPlayerFlags : uint8_t
            {
                Alive = 0x01,           ///< Player is alive if set, dead if unset.
                Invulnerable = 0x02     ///< Player is invulnerable if set (e.g. spawn protection).
            };

            static constexpr uint8_t kKnownFlags =
                static_cast<uint8_t>(EPlayerFlags::Alive) |
                static_cast<uint8_t>(EPlayerFlags::Invulnerable);

            EPlayerFlags flags{};     ///< Player alive status and other state flags.
        };

        PlayerEntry players[kMaxPlayers]; ///< State of each player.
    };

    /**
     * @brief Correction payload sent by server to the owning client.
     */
    struct MsgCorrection
    {
        uint32_t serverTick = 0;            ///< Server tick at which this correction applies.
        uint32_t lastProcessedInputSeq = 0; ///< Highest input seq the server has processed for this player.
        int16_t xQ = 0;                     ///< Corrected X position in tile-space Q8.
        int16_t yQ = 0;                     ///< Corrected Y position in tile-space Q8.
    };

    // =================================================================================================================
    // ===== Endian Helpers ============================================================================================
    // =================================================================================================================

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

    /** @brief Returns string length capped to `maxBytes` for bounded C strings. */
    constexpr std::size_t boundedStrLen(const char* s, std::size_t maxBytes)
    {
        if (!s) return 0;

        std::size_t n = 0;
        while(n < maxBytes && s[n] != '\0')
        {
            ++n;
        }
        return n;
    }


    /** @brief Sets `MsgHello::name` from `string_view` with truncation and zero padding. */
    inline void setHelloName(MsgHello& hello, std::string_view name)
    {
        std::memset(hello.name, 0, kPlayerNameMax);

        const std::size_t n = (name.size() < (kPlayerNameMax - 1)) ? name.size() : (kPlayerNameMax - 1);
        std::memcpy(hello.name, name.data(), n);
    }
    /** @brief C-string overload of setHelloName. */
    inline void setHelloName(MsgHello& hello, const char* name)
    {
        if (!name) {
            std::memset(hello.name, 0, kPlayerNameMax);
            return;
        }

        const std::size_t n = boundedStrLen(name, kPlayerNameMax - 1);
        setHelloName(hello, std::string_view{name, n});
    }

    // =================================================================================================================
    // ===== Serialization =============================================================================================
    // =================================================================================================================

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
        return true;
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
            case MsgReject::EReason::Other:
                outReject.expectedProtocolVersion = 0;
                break;
            default:
                return false;
        }

        return true;
    }

    /** @brief Serializes `MsgInput` to fixed-size wire payload. */
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

    /** @brief Serializes `MsgLevelInfo` to fixed-size wire payload. */
    inline void serializeMsgLevelInfo(const MsgLevelInfo& info, uint8_t* out) noexcept
    {
        writeU32LE(out, info.mapSeed);
    }

    /** @brief Deserializes `MsgLevelInfo` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgLevelInfo(const uint8_t* in, std::size_t inSize, MsgLevelInfo& outInfo)
    {
        if (inSize < kMsgLevelInfoSize)
        {
            return false;
        }
        outInfo.mapSeed = readU32LE(in);
        return true;
    }

    /** @brief Serializes `MsgSnapshot` to fixed-size wire payload. */
    inline void serializeMsgSnapshot(const MsgSnapshot& snap, uint8_t* out) noexcept
    {
        writeU32LE(out, snap.serverTick);
        const uint8_t playerCount = (snap.playerCount <= kMaxPlayers) ? snap.playerCount : kMaxPlayers;
        out[4] = playerCount;

        for (std::size_t i = 0; i < kMaxPlayers; ++i)
        {
            const auto& player = snap.players[i];
            const std::size_t offset = 5 + i * 6; ///<  Offset of the i-th player entry in the payload.
            out[offset] = player.playerId;
            writeU16LE(out + offset + 1, static_cast<uint16_t>(player.xQ));
            writeU16LE(out + offset + 3, static_cast<uint16_t>(player.yQ));
            out[offset + 5] = static_cast<uint8_t>(player.flags);
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

        outSnap.serverTick = readU32LE(in);
        outSnap.playerCount = in[4];
        if (outSnap.playerCount > kMaxPlayers)
        {
            return false;
        }

        for (std::size_t i = 0; i < kMaxPlayers; ++i)
        {
            const std::size_t offset = 5 + i * 6;
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
                if ((rawFlags & ~MsgSnapshot::PlayerEntry::kKnownFlags) != 0)
                {
                    return false;
                }
            }

            player.flags = static_cast<MsgSnapshot::PlayerEntry::EPlayerFlags>(rawFlags);
        }

        return true;
    }

    /** @brief Serializes `MsgCorrection` to fixed-size wire payload. */
    inline void serializeMsgCorrection(const MsgCorrection& corr, uint8_t* out) noexcept
    {
        writeU32LE(out, corr.serverTick);
        writeU32LE(out + 4, corr.lastProcessedInputSeq);
        writeU16LE(out + 8, static_cast<uint16_t>(corr.xQ));
        writeU16LE(out + 10, static_cast<uint16_t>(corr.yQ));
    }

    /** @brief Deserializes `MsgCorrection` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgCorrection(const uint8_t* in, std::size_t inSize, MsgCorrection& outCorr)
    {
        if (inSize < kMsgCorrectionSize)
        {
            return false;
        }
        outCorr.serverTick = readU32LE(in);
        outCorr.lastProcessedInputSeq = readU32LE(in + 4);
        outCorr.xQ = static_cast<int16_t>(readU16LE(in + 8));
        outCorr.yQ = static_cast<int16_t>(readU16LE(in + 10));
        return true;
    }

    // =================================================================================================================
    // ===== Packet Builders ===========================================================================================
    // =================================================================================================================

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

    /** @brief Builds a full LevelInfo packet (header + payload). Sent reliably by server after Welcome. */
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

} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETCOMMON_H
