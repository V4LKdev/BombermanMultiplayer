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
 * 1. `kProtocolVersion`
 * 2. Size constants and `static_assert` checks
 * 3. `minPayloadSize()`
 * 4. Affected serializers and deserializers
 */

namespace bomberman::net
{
    // =================================================================================================================
    // ===== Protocol Constants ========================================================================================
    // =================================================================================================================

    constexpr uint16_t kProtocolVersion = 1;
    constexpr std::size_t kPlayerNameMax = 16;
    constexpr std::size_t kMaxPacketSize = 1400; ///< Upper packet size bound (below typical 1500-byte MTU).
    constexpr uint32_t kInputLogEveryN = 30;     ///< Input log sampling interval on client and server. (TODO: replace with proper telemetry)

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


    /** @brief Header wire size in bytes. */
    constexpr std::size_t kPacketHeaderSize =
        sizeof(uint8_t) +  // type
        sizeof(uint16_t) + // payloadSize
        sizeof(uint32_t) + // sequence
        sizeof(uint32_t) + // tick
        sizeof(uint8_t);   // flags

    /** @brief Hello payload wire size in bytes. */
    constexpr std::size_t kMsgHelloSize =
        sizeof(uint16_t) + // protocolVersion
        kPlayerNameMax;    // name (fixed-size field)

    /** @brief Welcome payload wire size in bytes. */
    constexpr std::size_t kMsgWelcomeSize =
        sizeof(uint16_t) + // protocolVersion
        sizeof(uint32_t) + // clientId
        sizeof(uint16_t);  // serverTickRate

    /** @brief Input payload wire size in bytes. */
    constexpr std::size_t kMsgInputSize =
        sizeof(int8_t) +   // moveX
        sizeof(int8_t) +   // moveY
        sizeof(uint16_t);  // bombCommandId

    /** @brief Compile-time checks for expected wire sizes. */
    static_assert(sizeof(char) == 1, "Unexpected char size");

    static_assert(kPacketHeaderSize == 12, "PacketHeader size mismatch");
    static_assert(kMsgHelloSize == 18, "MsgHello size mismatch");
    static_assert(kMsgWelcomeSize == 8, "MsgWelcome size mismatch");
    static_assert(kMsgInputSize == 4, "MsgInput size mismatch");

    // =================================================================================================================
    // ===== Message Types =============================================================================================
    // =================================================================================================================

    /** @brief Message type identifiers used in packet headers. */
    enum class EMsgType : uint8_t
    {
        Hello = 0x01,
        Welcome = 0x02,
        // TODO: Add Reject = 0x03: server sends this on handshake failure
        Input = 0x10
    };

    /** @brief Checks if a raw byte value corresponds to a valid EMsgType. */
    inline bool isValidMsgType(uint8_t raw) {
        return raw == static_cast<uint8_t>(EMsgType::Hello) ||
               raw == static_cast<uint8_t>(EMsgType::Welcome) ||
               raw == static_cast<uint8_t>(EMsgType::Input);
    }

    /** @brief Returns minimum payload size for a message type/version. */
    constexpr std::size_t minPayloadSize(EMsgType type, [[maybe_unused]] uint16_t version = kProtocolVersion)
    {
        switch (type)
        {
            case EMsgType::Hello:   return kMsgHelloSize;     // v1: 18 bytes
            case EMsgType::Welcome: return kMsgWelcomeSize;   // v1: 8 bytes
            case EMsgType::Input:   return kMsgInputSize;     // v1: 4 bytes
            default:                return 0;
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
         * `kPlayerNameMax - 1`.
         */
        char name[kPlayerNameMax];
    };

    /** @brief Welcome payload sent by server in response to Hello. */
    struct MsgWelcome
    {
        uint16_t protocolVersion; ///< Server protocol version.
        uint32_t clientId;        ///< Server-assigned client identifier.
        uint16_t serverTickRate;  ///< Authoritative server simulation tick rate.
    };

    /** @brief Input payload sent by client each simulation tick. */
    struct MsgInput
    {
        int8_t moveX; ///< Horizontal movement in {-1, 0, 1}.
        int8_t moveY; ///< Vertical movement in {-1, 0, 1}.

        /**
         * @brief Monotonically increasing bomb-placement command identifier.
         *
         * Incremented on each bomb key edge and sent every input tick.
         * The server applies a new request when this value changes.
         */
        uint16_t bombCommandId = 0;
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
    inline std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> makeHelloPacket(std::string_view name, uint16_t protocolVersion, uint32_t sequence, uint32_t tick)
    {
        MsgHello hello{};
        hello.protocolVersion = protocolVersion;
        setHelloName(hello, name);
        return makeHelloPacket(hello, sequence, tick);
    }

    /** @brief Builds a full Welcome packet (header + payload). */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgWelcomeSize> makeWelcomePacket(const MsgWelcome& welcome, uint32_t sequence, uint32_t tick)
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


} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETCOMMON_H
