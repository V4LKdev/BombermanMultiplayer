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
 * 3. minPayloadSize()
 * 4. Affected serializers and deserializers
 */

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Protocol Constants ========================================================================================
    // =================================================================================================================

    // -----------------------------------------------
    constexpr uint16_t kProtocolVersion = 1;
    // -----------------------------------------------

    constexpr uint16_t kDefaultServerPort = 12345; ///< Default server port used by both client and server.
    constexpr std::size_t kMaxPacketSize = 1400; ///< Upper packet size bound (below typical 1500-byte MTU).
    constexpr uint32_t kInputLogEveryN = 30;     ///< Input log sampling interval on client and server. (TODO: replace with proper telemetry)
    constexpr uint32_t kStateLogEveryN = 30;     ///< State log sampling interval on server. (TODO: replace with proper telemetry)

    /** @note Changing these values will affect wire layout and require protocol version bump. */
    constexpr std::size_t kPlayerNameMax = 16;
    constexpr uint8_t kMaxPlayers = 4;          ///< Maximum supported player count in a game instance.

    /** @brief ENet channel identifiers. */
    enum class EChannel : uint8_t
    {
        Control = 0,   ///< Reliable control traffic.
        GameState = 1  ///< Unreliable gameplay traffic.
    };
    constexpr std::size_t kChannelCount = 2; ///< Total configured ENet channels.

    /** @brief Returns a human-readable name for a channel ID. */
    constexpr std::string_view channelName(uint8_t id)
    {
        switch (id)
        {
            case static_cast<uint8_t>(EChannel::Control):   return "Control";
            case static_cast<uint8_t>(EChannel::GameState): return "GameState";
            default:                                        return "Unknown";
        }
    }


    constexpr std::size_t kPacketHeaderSize =
        sizeof(uint8_t) +  // type
        sizeof(uint16_t) + // payloadSize
        sizeof(uint32_t) + // sequence
        sizeof(uint32_t) + // tick
        sizeof(uint8_t);   // flags

    constexpr std::size_t kMsgHelloSize =
        sizeof(uint16_t) + // protocolVersion
        kPlayerNameMax;    // name (fixed-size field)

    constexpr std::size_t kMsgWelcomeSize =
        sizeof(uint16_t) + // protocolVersion
        sizeof(uint32_t) + // clientId
        sizeof(uint16_t);  // serverTickRate

    constexpr std::size_t kMsgRejectSize =
        sizeof(uint8_t) +  // reason
        sizeof(uint16_t);  // expectedProtocolVersion

    constexpr std::size_t kMsgInputSize =
        sizeof(int8_t) +   // moveX
        sizeof(int8_t) +   // moveY
        sizeof(uint16_t);  // bombCommandId

    constexpr std::size_t kMsgLevelInfoSize =
        sizeof(uint32_t);  // mapSeed

    constexpr std::size_t kMsgStateSize =
        sizeof(uint8_t) +  // playerCount
        kMaxPlayers *
            (sizeof(uint8_t) +   // clientId
             sizeof(int16_t) +   // xQ
             sizeof(int16_t) +   // yQ
             sizeof(uint8_t));   // flags

    /** @brief Compile-time checks for expected wire sizes. */
    static_assert(sizeof(char) == 1, "Unexpected char size");

    static_assert(kPacketHeaderSize == 12,  "PacketHeader size mismatch");
    static_assert(kMsgHelloSize     == 18,  "MsgHello size mismatch");
    static_assert(kMsgWelcomeSize   == 8,   "MsgWelcome size mismatch");
    static_assert(kMsgRejectSize    == 3,   "MsgReject size mismatch");
    static_assert(kMsgInputSize     == 4,   "MsgInput size mismatch");
    static_assert(kMsgLevelInfoSize == 4,   "MsgLevelInfo size mismatch");
    static_assert(kMsgStateSize     == 25,  "MsgState size mismatch");

    // =================================================================================================================
    // ===== Message Types =============================================================================================
    // =================================================================================================================

    /** @brief Message type identifiers used in packet headers. */
    enum class EMsgType : uint8_t
    {
        Hello = 0x01,
        Welcome = 0x02,
        Reject = 0x03,
        LevelInfo = 0x04,

        Input = 0x10,
        State = 0x11
    };

    /** @brief Checks if a raw byte value corresponds to a valid EMsgType. */
    inline bool isValidMsgType(uint8_t raw) {
        return raw == static_cast<uint8_t>(EMsgType::Hello)      ||
               raw == static_cast<uint8_t>(EMsgType::Welcome)    ||
               raw == static_cast<uint8_t>(EMsgType::Reject)     ||
               raw == static_cast<uint8_t>(EMsgType::LevelInfo)  ||
               raw == static_cast<uint8_t>(EMsgType::Input)      ||
               raw == static_cast<uint8_t>(EMsgType::State);
    }

    /** @brief Returns minimum payload size for a message type/version. */
    constexpr std::size_t minPayloadSize(EMsgType type, uint16_t version = kProtocolVersion)
    {
        switch (type)
        {
            case EMsgType::Hello:
                if (version == 1) return kMsgHelloSize;      // v1: 18 bytes
                break;
            case EMsgType::Welcome:
                if (version == 1) return kMsgWelcomeSize;    // v1: 8 bytes
                break;
            case EMsgType::Reject:
                if (version == 1) return kMsgRejectSize;     // v1: 3 bytes
                break;
            case EMsgType::LevelInfo:
                if (version == 1) return kMsgLevelInfoSize;  // v1: 4 bytes
                break;
            case EMsgType::Input:
                if (version == 1) return kMsgInputSize;      // v1: 4 bytes
                break;
            case EMsgType::State:
                if (version == 1) return kMsgStateSize;      // v1: 25 bytes
                break;
            default:
                break;
        }
        return 0; // Unknown type or unsupported version
    }

    // =================================================================================================================
    // ===== Wire Payload Types ========================================================================================
    // =================================================================================================================

    /** @brief Packet metadata prefix present on every wire message. */
    struct PacketHeader
    {
        EMsgType type{};          ///< Message type identifier.
        uint16_t payloadSize = 0; ///< Payload size in bytes, excluding header.
        uint32_t sequence = 0;    ///< Message sequence value (message-specific semantics).
        uint32_t tick = 0;        ///< Sender tick at packet creation time.
        uint8_t flags = 0;        ///< Reserved packet flags.
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

    /** @brief Welcome payload sent by server in response to Hello. */
    struct MsgWelcome
    {
        uint16_t protocolVersion; ///< Server protocol version.
        uint32_t clientId;        ///< Server-assigned client identifier. // TODO: consider switching to uint8_t for size?
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

    /** @brief Input payload sent by client each simulation tick. */
    struct MsgInput
    {
        int8_t moveX = 0; ///< Horizontal movement in {-1, 0, 1}.
        int8_t moveY = 0; ///< Vertical movement in {-1, 0, 1}.

        /**
         * @brief Monotonically increasing bomb-placement command identifier.
         *
         * Incremented on each bomb key edge and sent every input tick.
         * The server applies a new request when this value changes.
         */
        uint16_t bombCommandId = 0;
    };

    /** @brief State payload sent by server to clients each simulation tick. */
    // TODO: State uses full snapshots rn. Switch to delta snapshots later.
    struct MsgState
    {
        uint8_t playerCount; ///< Number of active players in the game.

        struct PlayerState
        {
            uint8_t clientId; ///< Player identifier.
            int16_t xQ; ///< Player X position in tile-space Q8 (xQ = tileColumn * 256).
            int16_t yQ; ///< Player Y position in tile-space Q8 (yQ = tileRow    * 256).

            enum class EPlayerFlags : uint8_t
            {
                Alive = 0x01, ///< Player is alive if set, dead if unset.
                Invulnerable = 0x02  ///< Player is invulnerable if set (e.g. spawn protection).
            };

            static constexpr uint8_t kKnownFlags =
                static_cast<uint8_t>(EPlayerFlags::Alive) |
                static_cast<uint8_t>(EPlayerFlags::Invulnerable);

            EPlayerFlags flags;     ///< Player alive status and other state flags.
        };

        // TODO: Consider switching to a variable-length array of PlayerState with playerCount.
        PlayerState players[kMaxPlayers]; ///< State of each player.
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
        // Clear first so unused bytes remain zero-padded.
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
        writeU32LE(out + 3, header.sequence);
        writeU32LE(out + 7, header.tick);
        out[11] = header.flags;
    }

    /**
     * @brief Deserializes and validates PacketHeader.
     *
     * Validates type, payload bounds, and input buffer length.
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
        if (payloadSize > (kMaxPacketSize - kPacketHeaderSize))
        {
            return false;
        }

        if (inSize < kPacketHeaderSize + payloadSize)
        {
            return false;
        }

        // Ensure payload is large enough for the declared message type.
        const auto msgType = static_cast<EMsgType>(rawType);
        const std::size_t minSize = minPayloadSize(msgType);
        if (minSize > 0 && payloadSize < minSize)
        {
            return false;
        }

        outHeader.type = msgType;
        outHeader.payloadSize = payloadSize;
        outHeader.sequence = readU32LE(in + 3);
        outHeader.tick = readU32LE(in + 7);
        outHeader.flags = in[11];
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

        // Guarantee C-string safety even if sender violated the contract.
        outHello.name[kPlayerNameMax - 1] = '\0';

        // Normalize to in-memory zero-padding invariant.
        const std::size_t n = boundedStrLen(outHello.name, kPlayerNameMax - 1);
        std::memset(outHello.name + n, 0, kPlayerNameMax - n);

        return true;
    }

    /** @brief Serializes `MsgWelcome` to fixed-size wire payload. */
    inline void serializeMsgWelcome(const MsgWelcome& welcome, uint8_t* out) noexcept
    {
        writeU16LE(out, welcome.protocolVersion);
        writeU32LE(out + 2, welcome.clientId);
        writeU16LE(out + 6, welcome.serverTickRate);
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
        outWelcome.clientId = readU32LE(in + 2);
        outWelcome.serverTickRate = readU16LE(in + 6);
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
        out[0] = input.moveX;
        out[1] = input.moveY;
        writeU16LE(out + 2, input.bombCommandId);
    }

    /** @brief Deserializes `MsgInput` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgInput(const uint8_t* in, std::size_t inSize, MsgInput& outInput)
    {
        if (inSize < kMsgInputSize)
        {
            return false;
        }

        const auto rawMoveX = static_cast<int8_t>(in[0]);
        const auto rawMoveY = static_cast<int8_t>(in[1]);

        // Validate movement domain.
        if ((rawMoveX < -1 || rawMoveX > 1) || (rawMoveY < -1 || rawMoveY > 1))
        {
            return false;
        }

        outInput.moveX = rawMoveX;
        outInput.moveY = rawMoveY;
        outInput.bombCommandId = readU16LE(in + 2);
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

    /** @brief Serializes `MsgState` to fixed-size wire payload. */
    inline void serializeMsgState(const MsgState& state, uint8_t* out) noexcept
    {
        const uint8_t playerCount = (state.playerCount <= kMaxPlayers) ? state.playerCount : kMaxPlayers;
        out[0] = playerCount;

        for (std::size_t i = 0; i < kMaxPlayers; ++i)
        {
            const auto& player = state.players[i];
            const std::size_t offset = 1 + i * 6;
            out[offset] = player.clientId;
            writeU16LE(out + offset + 1, static_cast<uint16_t>(player.xQ));
            writeU16LE(out + offset + 3, static_cast<uint16_t>(player.yQ));
            out[offset + 5] = static_cast<uint8_t>(player.flags);
        }
    }

    /** @brief Deserializes `MsgState` from fixed-size wire payload. */
    [[nodiscard]]
    inline bool deserializeMsgState(const uint8_t* in, std::size_t inSize, MsgState& outState)
    {
        if (inSize < kMsgStateSize)
        {
            return false;
        }

        outState.playerCount = in[0];
        if (outState.playerCount > kMaxPlayers)
        {
            return false;
        }

        for (std::size_t i = 0; i < kMaxPlayers; ++i)
        {
            const std::size_t offset = 1 + i * 6;
            auto& player = outState.players[i];
            player.clientId = in[offset];
            player.xQ = static_cast<int16_t>(readU16LE(in + offset + 1));
            player.yQ = static_cast<int16_t>(readU16LE(in + offset + 3));
            const uint8_t rawFlags = in[offset + 5];
            if ((rawFlags & ~MsgState::PlayerState::kKnownFlags) != 0)
            {
                return false;
            }
            player.flags = static_cast<MsgState::PlayerState::EPlayerFlags>(rawFlags);
        }

        return true;
    }

    // =================================================================================================================
    // ===== Packet Builders ===========================================================================================
    // =================================================================================================================

    /** @brief Builds a full Hello packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> makeHelloPacket(const MsgHello& hello, uint32_t sequence, uint32_t tick)
    {
        PacketHeader header{};
        header.type = EMsgType::Hello;
        header.payloadSize = static_cast<uint16_t>(kMsgHelloSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;

        std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgHello(hello, bytes.data() + kPacketHeaderSize);

        return bytes;
    }
    /** @brief Convenience overload building Hello from name and version. */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> makeHelloPacket(std::string_view name, uint16_t protocolVersion, uint32_t sequence = 0, uint32_t tick = 0)
    {
        MsgHello hello{};
        hello.protocolVersion = protocolVersion;
        setHelloName(hello, name);
        return makeHelloPacket(hello, sequence, tick);
    }

    /** @brief Builds a full Welcome packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgWelcomeSize> makeWelcomePacket(const MsgWelcome& welcome, uint32_t sequence = 0, uint32_t tick = 0)
    {
        PacketHeader header{};
        header.type = EMsgType::Welcome;
        header.payloadSize = static_cast<uint16_t>(kMsgWelcomeSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;

        std::array<uint8_t, kPacketHeaderSize + kMsgWelcomeSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgWelcome(welcome, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full Reject packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgRejectSize> makeRejectPacket(const MsgReject& reject, uint32_t sequence = 0, uint32_t tick = 0)
    {
        PacketHeader header{};
        header.type = EMsgType::Reject;
        header.payloadSize = static_cast<uint16_t>(kMsgRejectSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;

        std::array<uint8_t, kPacketHeaderSize + kMsgRejectSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgReject(reject, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full Input packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgInputSize> makeInputPacket(const MsgInput& input, uint32_t sequence, uint32_t tick)
    {
        PacketHeader header{};
        header.type = EMsgType::Input;
        header.payloadSize = static_cast<uint16_t>(kMsgInputSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;

        std::array<uint8_t, kPacketHeaderSize + kMsgInputSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgInput(input, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full LevelInfo packet (header + payload). Sent reliably by server after Welcome. */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgLevelInfoSize> makeLevelInfoPacket(const MsgLevelInfo& info, uint32_t sequence = 0, uint32_t tick = 0)
    {
        PacketHeader header{};
        header.type = EMsgType::LevelInfo;
        header.payloadSize = static_cast<uint16_t>(kMsgLevelInfoSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;

        std::array<uint8_t, kPacketHeaderSize + kMsgLevelInfoSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgLevelInfo(info, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

    /** @brief Builds a full State packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgStateSize> makeStatePacket(const MsgState& state, uint32_t sequence, uint32_t tick)
    {
        PacketHeader header{};
        header.type = EMsgType::State;
        header.payloadSize = static_cast<uint16_t>(kMsgStateSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;

        std::array<uint8_t, kPacketHeaderSize + kMsgStateSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgState(state, bytes.data() + kPacketHeaderSize);

        return bytes;
    }


} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETCOMMON_H
