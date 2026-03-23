#ifndef BOMBERMAN_NET_PACKETDISPATCH_H
#define BOMBERMAN_NET_PACKETDISPATCH_H

#include <cstddef>

#include "NetCommon.h"

/**
 * @file PacketDispatch.h
 * @brief Shared packet parsing and type-safe dispatch helpers.
 */

namespace bomberman::net
{
    // ----- Shared Receive Helper -----

    /**
     * @brief Validates and parses a raw packet into header and payload view.
     *
     * Wraps `deserializeHeader()` and computes the payload pointer.
     *
     * @note This validates only the packet header and payload-length contract.
     * Channel checks and typed payload deserialization stay with the caller.
     *
     * @param data Raw packet bytes (header + payload).
     * @param dataLength Total packet size in bytes.
     * @param outHeader Parsed and validated packet header.
     * @param outPayload Pointer to payload start on success.
     * @param outPayloadSize Payload size on success.
     * @return `true` if packet is valid and ready for dispatch.
     */
    [[nodiscard]]
    inline bool tryParsePacket(const uint8_t* data,
                               std::size_t dataLength,
                               PacketHeader& outHeader,
                               const uint8_t*& outPayload,
                               std::size_t& outPayloadSize)
    {
        if (!deserializeHeader(data, dataLength, outHeader))
            return false;

        outPayload = data + kPacketHeaderSize;
        outPayloadSize = outHeader.payloadSize;
        return true;
    }
    // ----- Type-Safe Packet Dispatcher -----

    /**
     * @brief Function signature for a typed message handler.
     *
     * @tparam TContext Context type forwarded to handlers.
     *
     * @note Handlers can assume header and payload length were validated by
     * @ref tryParsePacket. They must still validate typed payload contents.
     */
    template<typename TContext>
    using PacketHandlerFn = void(*)(TContext& context, const PacketHeader& header, const uint8_t* payload, std::size_t payloadSize);

    /**
     * @brief Fixed-size message type to handler lookup table.
     * @tparam TContext Context type shared by all bound handlers.
     *
     * Indexes by raw `uint8_t` message type. Unbound slots are `nullptr`.
     */
    template<typename TContext>
    struct PacketDispatcher
    {
        static constexpr std::size_t kTableSize = 256; // Full uint8_t range.
        PacketHandlerFn<TContext> table[kTableSize] {};

        /** @brief Registers a handler for a message type. Overwrites existing binding. */
        void bind(EMsgType type, PacketHandlerFn<TContext> fn)
        {
            table[static_cast<uint8_t>(type)] = fn;
        }

        /**
         * @brief Looks up and invokes the handler for `header.type`.
         * @return `true` if a handler was found and called.
         */
        bool dispatch(TContext& context, const PacketHeader& header, const uint8_t* payload, std::size_t payloadSize) const
        {
            const auto handler = table[static_cast<uint8_t>(header.type)];
            if (!handler) return false;

            handler(context, header, payload, payloadSize);
            return true;
        }
    };

} // namespace bomberman::net

#endif // BOMBERMAN_NET_PACKETDISPATCH_H
