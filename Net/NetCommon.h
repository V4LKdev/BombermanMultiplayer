
#ifndef BOMBERMAN_NETCOMMON_H
#define BOMBERMAN_NETCOMMON_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bomberman::net
{
    constexpr uint16_t kProtocolVersion = 1;
    constexpr uint16_t kServerTickRate = 60;
    constexpr std::size_t kPlayerNameMax = 16;
    constexpr std::size_t kPacketHeaderSize = 12;
    constexpr std::size_t kMsgHelloSize = 18;
    constexpr std::size_t kMsgWelcomeSize = 8;

    enum class EMsgType : uint8_t
    {
        Hello = 0x01,
        Welcome = 0x02
    };

    struct PacketHeader
    {
        EMsgType type;   // EMsgType
        uint16_t payloadSize;  // Size of the payload (not whole packet)
        uint32_t sequence;  // Sequence number for ordering
        uint32_t tick;  // Game tick for sync
        uint8_t flags;  // Bit flags for future use
    };

    struct MsgHello
    {
        uint16_t protocolVersion;
        char name[kPlayerNameMax];
    };

    struct MsgWelcome
    {
        uint16_t protocolVersion;
        uint32_t clientId;
        uint16_t serverTickRate;
    };

    static_assert(sizeof(char) == 1, "Unexpected char size");

    inline void writeU16LE(uint8_t* out, uint16_t value)
    {
        out[0] = static_cast<uint8_t>(value & 0xFFu);
        out[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    }

    inline void writeU32LE(uint8_t* out, uint32_t value)
    {
        out[0] = static_cast<uint8_t>(value & 0xFFu);
        out[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
        out[2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
        out[3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
    }

    inline uint16_t readU16LE(const uint8_t* in)
    {
        return static_cast<uint16_t>(in[0]) |
               (static_cast<uint16_t>(in[1]) << 8u);
    }

    inline uint32_t readU32LE(const uint8_t* in)
    {
        return static_cast<uint32_t>(in[0]) |
               (static_cast<uint32_t>(in[1]) << 8u) |
               (static_cast<uint32_t>(in[2]) << 16u) |
               (static_cast<uint32_t>(in[3]) << 24u);
    }

    inline void serializeHeader(const PacketHeader& header, uint8_t* out)
    {
        out[0] = static_cast<uint8_t>(header.type);
        writeU16LE(out + 1, header.payloadSize);
        writeU32LE(out + 3, header.sequence);
        writeU32LE(out + 7, header.tick);
        out[11] = header.flags;
    }

    inline bool deserializeHeader(const uint8_t* in, std::size_t inSize, PacketHeader& outHeader)
    {
        if(inSize < kPacketHeaderSize)
        {
            return false;
        }
        outHeader.type = static_cast<EMsgType>(in[0]);
        outHeader.payloadSize = readU16LE(in + 1);
        outHeader.sequence = readU32LE(in + 3);
        outHeader.tick = readU32LE(in + 7);
        outHeader.flags = in[11];
        return true;
    }

    inline void serializeMsgHello(const MsgHello& hello, uint8_t* out)
    {
        writeU16LE(out, hello.protocolVersion);
        std::memcpy(out + 2, hello.name, kPlayerNameMax);
    }

    inline bool deserializeMsgHello(const uint8_t* in, std::size_t inSize, MsgHello& outHello)
    {
        if(inSize < kMsgHelloSize)
        {
            return false;
        }
        outHello.protocolVersion = readU16LE(in);
        std::memcpy(outHello.name, in + 2, kPlayerNameMax);
        return true;
    }

    inline void serializeMsgWelcome(const MsgWelcome& welcome, uint8_t* out)
    {
        writeU16LE(out, welcome.protocolVersion);
        writeU32LE(out + 2, welcome.clientId);
        writeU16LE(out + 6, welcome.serverTickRate);
    }

    inline bool deserializeMsgWelcome(const uint8_t* in, std::size_t inSize, MsgWelcome& outWelcome)
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

    inline std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> makeHelloPacket(const MsgHello& hello, uint32_t sequence, uint32_t tick)
    {
        std::array<uint8_t, kPacketHeaderSize + kMsgHelloSize> bytes{};
        PacketHeader header{};
        header.type = EMsgType::Hello;
        header.payloadSize = static_cast<uint16_t>(kMsgHelloSize);
        header.sequence = sequence;
        header.tick = tick;
        header.flags = 0;
        serializeHeader(header, bytes.data());
        serializeMsgHello(hello, bytes.data() + kPacketHeaderSize);
        return bytes;
    }

} // namespace bomberman::net

#endif //BOMBERMAN_NETCOMMON_H
