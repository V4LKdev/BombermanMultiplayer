#ifndef BOMBERMAN_NETCOMMON_H
#define BOMBERMAN_NETCOMMON_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

/**
 *  @brief NetCommon.h - Common definitions and utilities for the Bomberman network protocol.
 *
 *  This header defines packet structure, message types, and serialization/deserialization helpers for the Bomberman game network protocol.
 */
namespace bomberman::net
{
    constexpr uint16_t kProtocolVersion = 1;
    constexpr uint16_t kServerTickRate = 60;
    constexpr std::size_t kPlayerNameMax = 16;
    constexpr std::size_t kMaxPacketSize = 1400; // Must be less than typical MTU (1500 bytes)

    /** ---- Wire Sizes ---------------------------- */
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

    /** ---- Static Assertions ----------------------- */
    static_assert(sizeof(char) == 1, "Unexpected char size");

    static_assert(kPacketHeaderSize == 12, "PacketHeader size mismatch");
    static_assert(kMsgHelloSize == 18, "MsgHello size mismatch");
    static_assert(kMsgWelcomeSize == 8, "MsgWelcome size mismatch");

    /**
     *  @brief Enumeration of message types in the Bomberman network protocol.
     */
    enum class EMsgType : uint8_t
    {
        Hello = 0x01,
        Welcome = 0x02
    };

    /** @brief Checks if a raw byte value corresponds to a valid EMsgType. */
    inline bool isValidMsgType(uint8_t raw) {
        return raw == static_cast<uint8_t>(EMsgType::Hello) ||
               raw == static_cast<uint8_t>(EMsgType::Welcome);
    }

    /** ================================================================================================================
     *  ==== PACKET STRUCTS ============================================================================================
     * =================================================================================================================
     */

    /**
    *  @brief Represents the header of a network packet in the Bomberman protocol.
    *
    *  The PacketHeader contains metadata for routing, sequencing, and interpreting the packet payload.
    */
    struct PacketHeader
    {
        EMsgType type;          ///< The type of the message
        uint16_t payloadSize;   ///< The size of the payload in bytes, not including the header
        uint32_t sequence;      ///< A sequence number for reliable packets to allow for ordering and duplicate detection on application level
        uint32_t tick;          ///< The server tick at which the packet was sent (for client packets, this is the client's current tick)
        uint8_t flags;          ///< Additional flags for the packet
    };

    /**
     *  @brief Represents the payload of a Hello message sent by the client to the server during the handshake process.
     */
    struct MsgHello
    {
        uint16_t protocolVersion;   ///< The protocol version the client is using

        /**
        * @brief Player display name (fixed-size wire field).
        *
        * Wire format: exactly @ref kPlayerNameMax bytes.
        * The name MUST be NUL-terminated within the buffer and
        * the remaining bytes are zero-padded.
        *
        * Max usable bytes for the name content is (kPlayerNameMax - 1).
        */
        char name[kPlayerNameMax];
    };

    /**
     *  @brief Represents the payload of a Welcome message sent by the server to the client in response to a Hello message during the handshake process.
     */
    struct MsgWelcome
    {
        uint16_t protocolVersion;   ///< The protocol version the server is using
        uint32_t clientId;          ///< A unique identifier assigned by the server to the client
        uint16_t serverTickRate;    ///< The tick rate of the server, which the client should use for its internal timing and synchronization
    };


    /** ================================================================================================================
     *  ==== SERIALIZATION HELPERS =====================================================================================
     * =================================================================================================================
     */

    /**
    *  @brief Writes a 16-bit unsigned integer to a byte array in little-endian format.
    *
    *  @param out The output byte array where the value will be written.
    *  @param value The 16-bit unsigned integer value to write.
    */
    constexpr inline void writeU16LE(uint8_t* out, uint16_t value)
    {
        out[0] = static_cast<uint8_t>(value & 0xFFu);
        out[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    }

    /**
     * @brief Writes a 32-bit unsigned integer to a byte array in little-endian format.
     *
     * @param out The output byte array where the value will be written.
     * @param value The 32-bit unsigned integer value to write.
     */
    constexpr inline void writeU32LE(uint8_t* out, uint32_t value)
    {
        out[0] = static_cast<uint8_t>(value & 0xFFu);
        out[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
        out[2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
        out[3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
    }

    /**
     * @brief Reads a 16-bit unsigned integer from a byte array in little-endian format.
     *
     * @param in The input byte array from which the value will be read.
     * @return The 16-bit unsigned integer value read from the byte array.
     */
    constexpr inline uint16_t readU16LE(const uint8_t* in)
    {
        return static_cast<uint16_t>(in[0]) |
               (static_cast<uint16_t>(in[1]) << 8u);
    }

    /**
     * @brief Reads a 32-bit unsigned integer from a byte array in little-endian format.
     *
     * @param in The input byte array from which the value will be read.
     * @return The 32-bit unsigned integer value read from the byte array.
     */
    constexpr inline uint32_t readU32LE(const uint8_t* in)
    {
        return static_cast<uint32_t>(in[0]) |
               (static_cast<uint32_t>(in[1]) << 8u) |
               (static_cast<uint32_t>(in[2]) << 16u) |
               (static_cast<uint32_t>(in[3]) << 24u);
    }

    /**
     * @brief Computes the length of a C-style string up to a maximum number of bytes.
     *
     * @param s The input C-style string pointer (may be null).
     * @param maxBytes The maximum number of bytes to examine in the string.
     *
     * @return The length of the string in bytes, not including the null terminator, but at most `maxBytes`.
     */
    inline std::size_t boundedStrLen(const char* s, std::size_t maxBytes)
    {
        if (!s) return 0;

        std::size_t n = 0;
        while(n < maxBytes && s[n] != '\0')
        {
            ++n;
        }
        return n;
    }


    /**
     * @brief Sets the name field of a MsgHello struct from a std::string_view, ensuring proper null-termination and padding.
     *
     * @param hello The MsgHello struct whose name field will be set.
     * @param name The input string view containing the player's name. If the name is longer than (kPlayerNameMax - 1), it will be truncated.
     *
     * This function ensures that the name field in the MsgHello struct is properly null-terminated and padded with zeros if the input name is shorter than kPlayerNameMax.
     */
    inline void setHelloName(MsgHello& hello, std::string_view name)
    {
        // Clear the name buffer first to ensure any unused bytes are zero-padded, and to guarantee null-termination
        std::memset(hello.name, 0, kPlayerNameMax);

        const std::size_t n = (name.size() < (kPlayerNameMax - 1)) ? name.size() : (kPlayerNameMax - 1);
        std::memcpy(hello.name, name.data(), n);
    }
    /** @brief Overload of setHelloName that accepts a C-style string. If the input pointer is null, the name will be set to an empty string. */
    inline void setHelloName(MsgHello& hello, const char* name)
    {
        if (!name) {
            std::memset(hello.name, 0, kPlayerNameMax);
            return;
        }

        const std::size_t n = boundedStrLen(name, kPlayerNameMax - 1);
        setHelloName(hello, std::string_view{name, n});
    }

    /** ================================================================================================================
     *  ==== (DE)SERIALIZATION FUNCTIONS ===============================================================================
     * =================================================================================================================
     */

    /**
     *  @brief Serializes a PacketHeader struct into a byte array in the Bomberman network protocol format.
     *
     *  @param header The PacketHeader struct to serialize.
     *  @param out The output byte array where the serialized header will be written. Must have at least kPacketHeaderSize bytes available.
     *
     *  @note Total size: 12 bytes
     */
    inline void serializeHeader(const PacketHeader& header, uint8_t* out) noexcept
    {
                                                        // Wire Pack Format:
        out[0] = static_cast<uint8_t>(header.type);     // offset 0: 1 byte  - message type
        writeU16LE(out + 1, header.payloadSize);        // offset 1: 2 bytes - payload size (excluding header)
        writeU32LE(out + 3, header.sequence);           // offset 3: 4 bytes - sequence number
        writeU32LE(out + 7, header.tick);               // offset 7: 4 bytes - tick
        out[11] = header.flags;                         // offset 11: 1 byte - flags
    }

    /**
     *  @brief Deserializes a PacketHeader struct from a byte array.
     *
     *  @param in The input byte array containing the serialized header. Must have at least kPacketHeaderSize bytes available.
     *  @param inSize The size of the input byte array in bytes.
     *  @param outHeader The output PacketHeader struct where the deserialized header will be stored.
     *
     *  @return true if deserialization was successful, false if the input data was too small to contain a valid PacketHeader.
     *
     *  @note Caller should verify 'inSize >= kPacketHeaderSize + outHeader.payloadSize' before attempting to deserialize the payload.
     */
    [[nodiscard]] inline bool deserializeHeader(const uint8_t* in, std::size_t inSize, PacketHeader& outHeader)
    {
        /**
         * Deserialization Steps:
         * 1. Check if input size is at least the size of the header.
         * 2. Read the message type and validate it against known EMsgType values.
         * 3. Read the payload size and validate it against the maximum allowed size.
         * 4. Check if the input size is sufficient to contain the entire packet (header + payload) based on the payload size.
         * 5. If all checks pass, populate the outHeader struct with the deserialized values and return true.
         */

        if(inSize < kPacketHeaderSize)
            return false;

        const uint8_t rawType = in[0];
        if (!isValidMsgType(rawType))
            return false;

        const uint16_t payloadSize = readU16LE(in + 1);
        if (payloadSize > (kMaxPacketSize - kPacketHeaderSize))
            return false;

        if (inSize < kPacketHeaderSize + payloadSize)
            return false;

        outHeader.type = static_cast<EMsgType>(rawType);
        outHeader.payloadSize = payloadSize;
        outHeader.sequence = readU32LE(in + 3);
        outHeader.tick = readU32LE(in + 7);
        outHeader.flags = in[11];
        return true;
    }

    /**
     *  @brief Serializes a MsgHello struct into a byte array.
     *
     *  @param hello The MsgHello struct to serialize.
     *  @param out The output byte array where the serialized message will be written. Must have at least kMsgHelloSize bytes available.
     *
     *  @note Total size: 18 bytes (with kPlayerNameMax = 16)
     */
    inline void serializeMsgHello(const MsgHello& hello, uint8_t* out) noexcept
    {
                                                                    // Wire Pack Format:
        writeU16LE(out, hello.protocolVersion);                     // offset 0: 2 bytes - protocol version
        std::memset(out + 2, 0, kPlayerNameMax);              // offset 2: 16 bytes - name (zero-padded)


        const std::size_t nameLen = boundedStrLen(hello.name, kPlayerNameMax - 1);
        std::memcpy(out + 2, hello.name, nameLen);           // copy the name content into the output buffer
    }

    /**
     *  @brief Deserializes a MsgHello struct from a byte array.
     *
     *  @param in The input byte array containing the serialized message. Must have at least kMsgHelloSize bytes available.
     *  @param inSize The size of the input byte array in bytes.
     *  @param outHello The output MsgHello struct where the deserialized message will be stored.
     *
     *  @return true if deserialization was successful, false if the input data was too small to contain a valid MsgHello.
     */
    [[nodiscard]] inline bool deserializeMsgHello(const uint8_t* in, std::size_t inSize, MsgHello& outHello)
    {
        if(inSize < kMsgHelloSize)
            return false;

        outHello.protocolVersion = readU16LE(in);
        std::memcpy(outHello.name, in + 2, kPlayerNameMax);

        // Guarantee C-string safety even if sender violated the contract.
        outHello.name[kPlayerNameMax - 1] = '\0';

        // Normalize to "zero-padded remainder" invariant in-memory.
        const std::size_t n = boundedStrLen(outHello.name, kPlayerNameMax - 1);
        std::memset(outHello.name + n, 0, kPlayerNameMax - n);

        return true;
    }

    /**
     *  @brief Serializes a MsgWelcome struct into a byte array.
     *
     *  @param welcome The MsgWelcome struct to serialize.
     *  @param out The output byte array where the serialized message will be written. Must have at least kMsgWelcomeSize bytes available.
     *
     *  @note Total size: 8 bytes
     */
    inline void serializeMsgWelcome(const MsgWelcome& welcome, uint8_t* out) noexcept
    {
                                                            // Wire Pack Format:
        writeU16LE(out, welcome.protocolVersion);           // offset 0: 2 bytes - protocol version
        writeU32LE(out + 2, welcome.clientId);              // offset 2: 4 bytes - client ID
        writeU16LE(out + 6, welcome.serverTickRate);        // offset 6: 2 bytes - server tick rate
    }

    /**
     *  @brief Deserializes a MsgWelcome struct from a byte array.
     *
     *  @param in The input byte array containing the serialized message. Must have at least kMsgWelcomeSize bytes available.
     *  @param inSize The size of the input byte array in bytes.
     *  @param outWelcome The output MsgWelcome struct where the deserialized message will be stored.
     *
     *  @return true if deserialization was successful, false if the input data was too small to contain a valid MsgWelcome.
     */
    [[nodiscard]] inline bool deserializeMsgWelcome(const uint8_t* in, std::size_t inSize, MsgWelcome& outWelcome)
    {
        if(inSize < kMsgWelcomeSize)
        {
            return false;
        }
        outWelcome.protocolVersion = readU16LE(in);
        outWelcome.clientId = readU32LE(in + 2);
        outWelcome.serverTickRate = readU16LE(in + 6);
        return true;
    }


    /** ================================================================================================================
     *  ==== PACKET CONSTRUCTION HELPERS ===============================================================================
     * =================================================================================================================
     */


    /**
     *  @brief Constructs a complete Hello packet (header + payload) as a byte array ready to be sent over the network.
     *
     *  @param hello The MsgHello struct containing the payload data for the Hello message.
     *  @param sequence The sequence number to use in the packet header for this Hello message.
     *  @param tick The tick value to use in the packet header for this Hello message.
     *
     *  @return A std::array containing the serialized Hello packet (header + payload)
     */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> makeHelloPacket(const MsgHello& hello, uint32_t sequence, uint32_t tick)
    {
        // Construct the packet header
        PacketHeader header{};
        header.type = EMsgType::Hello;
        header.payloadSize = static_cast<uint16_t>(kMsgHelloSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;

        // Serialize the header and payload into a byte array
        std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgHello(hello, bytes.data() + kPacketHeaderSize);

        return bytes;
    }
    /** @brief Overload of makeHelloPacket that constructs the MsgHello struct from a player name string view and protocol version, then creates the packet. */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> makeHelloPacket(std::string_view name, uint16_t protocolVersion, uint32_t sequence, uint32_t tick)
    {
        MsgHello hello{};
        hello.protocolVersion = protocolVersion;
        setHelloName(hello, name);
        return makeHelloPacket(hello, sequence, tick);
    }

    /**
     *  @brief Constructs a complete Welcome packet (header + payload) as a byte array ready to be sent over the network.
     *
     *  @param welcome The MsgWelcome struct containing the payload data for the Welcome message.
     *  @param sequence The sequence number to use in the packet header for this Welcome message.
     *  @param tick The tick value to use in the packet header for this Welcome message.
     *
     *  @return A std::array containing the serialized Welcome packet (header + payload)
     */
    inline std::array<uint8_t, kPacketHeaderSize + kMsgWelcomeSize> makeWelcomePacket(const MsgWelcome& welcome, uint32_t sequence, uint32_t tick)
    {
        // Construct the packet header
        PacketHeader header{};
        header.type = EMsgType::Welcome;
        header.payloadSize = static_cast<uint16_t>(kMsgWelcomeSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;

        // Serialize the header and payload into a byte array
        std::array<uint8_t, kPacketHeaderSize + kMsgWelcomeSize> bytes{};
        serializeHeader(header, bytes.data());
        serializeMsgWelcome(welcome, bytes.data() + kPacketHeaderSize);

        return bytes;
    }

} // namespace bomberman::net

#endif //BOMBERMAN_NETCOMMON_H
