#ifndef BOMBERMAN_NET_PACKETDISPATCH_H
#define BOMBERMAN_NET_PACKETDISPATCH_H

#include <cstddef>

#include "NetCommon.h"
#include "Util/Log.h"

/**
 * @brief Shared packet parsing and type-safe dispatch helpers.
 *
 * Provides:
 * 1. `tryParsePacket()` for header validation plus payload extraction.
 * 2. `PacketDispatcher<TContext>` for O(1) typed message dispatch.
 */


namespace bomberman::net
{

     // ================================================================================================================
     // ==== SHARED RECEIVE HELPER =====================================================================================
     // ================================================================================================================

    /**
     * @brief Validates and parses a raw packet into header and payload view.
     *
     * Wraps `deserializeHeader()` and computes the payload pointer.
     *
     * @param data Raw packet bytes (header + payload).
     * @param dataLength Total packet size in bytes.
     * @param outHeader Parsed and validated packet header.
     * @param outPayload Pointer to payload start on success.
     * @param outPayloadSize Payload size on success.
     * @return `true` if packet is valid and ready for dispatch.
     */
    [[nodiscard]]
    inline bool tryParsePacket(const uint8_t* data, std::size_t dataLength, PacketHeader& outHeader,
        const uint8_t*& outPayload, std::size_t& outPayloadSize)
    {
        if (!deserializeHeader(data, dataLength, outHeader))
            return false;

        outPayload = data + kPacketHeaderSize;
        outPayloadSize = outHeader.payloadSize;
        return true;
    }


     // ================================================================================================================
     // ==== TYPE-SAFE PACKET DISPATCHER ===============================================================================
     // ================================================================================================================


    /**
     * @brief Function signature for a typed message handler.
     * @tparam TContext Context type forwarded to handlers.
     *
     * Handlers can assume header and payload were validated by `tryParsePacket()`.
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
        static constexpr std::size_t kTableSize = 256; // uint8_t range
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
            const auto fn = table[static_cast<uint8_t>(header.type)];
            if (!fn) return false;

            fn(context, header, payload, payloadSize);
            return true;
        }
    };


     // ================================================================================================================
     // ==== DISPATCH + RECEIVE CONVENIENCE ============================================================================
     // ================================================================================================================


    /**
     * @brief Parses a raw packet and dispatches it via the given dispatcher.
     *
     * Combines `tryParsePacket()` and `PacketDispatcher::dispatch()`.
     * Logs malformed packets and unhandled types through protocol logging.
     *
     * @tparam TContext Context type for dispatcher and handler calls.
     * @return `true` if parse and dispatch both succeeded.
     */
    template<typename TContext>
    bool dispatchPacket(const PacketDispatcher<TContext>& dispatcher, TContext& context, const uint8_t* data, std::size_t dataLength)
    {
        PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;

        if (!tryParsePacket(data, dataLength, header, payload, payloadSize))
        {
            LOG_PROTO_WARN("Failed to deserialize PacketHeader (malformed or truncated, {} bytes)",
                           dataLength);
            return false;
        }

        if (!dispatcher.dispatch(context, header, payload, payloadSize))
        {
            LOG_PROTO_WARN("No handler for message type {}", static_cast<int>(header.type));
            return false;
        }

        return true;
    }

} // namespace bomberman::net

#endif // BOMBERMAN_NET_PACKETDISPATCH_H
