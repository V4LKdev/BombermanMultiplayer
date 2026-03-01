#ifndef BOMBERMAN_NET_PACKETDISPATCH_H
#define BOMBERMAN_NET_PACKETDISPATCH_H

#include <cstddef>
#include <iostream>

#include "Net/NetCommon.h"

/**
 *  @brief PacketDispatch.h - Type-safe packet dispatching and shared receive helpers.
 *
 *  Provides:
 *  - `PacketDispatcher<TContext>`: a templated, type-safe O(1) dispatch table that eliminates void* casts from handler code.
 *  - `tryParsePacket()`: shared validation helper used by both client and server receive paths to avoid duplicating header-parse + pointer-math logic.
 */
namespace bomberman::net
{

     // ================================================================================================================
     // ==== SHARED RECEIVE HELPER =====================================================================================
     // ================================================================================================================

    /**
     *  @brief Validates and parses a raw packet buffer into a header + payload pointer.
     *
     *  This is the single shared entry point for both client and server receive paths.
     *  It wraps `deserializeHeader()` and computes the payload pointer, so callers don't duplicate that logic.
     *
     *  @param data        Raw packet bytes (header + payload).
     *  @param dataLength  Total number of bytes in the packet.
     *  @param outHeader   On success, populated with the deserialized and validated header.
     *  @param outPayload  On success, points to the first payload byte (past the header).
     *  @param outPayloadSize  On success, set to the payload size from the header.
     *
     *  @return true if the packet is valid and ready for dispatch, false otherwise.
     *
     *  @note On success, all `deserializeHeader()` invariants hold and the caller can
     *        pass (outPayload, outPayloadSize) directly to a dispatcher or payload deserializer.
     */
    [[nodiscard]] inline bool tryParsePacket(const uint8_t* data,
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


     // ================================================================================================================
     // ==== TYPE-SAFE PACKET DISPATCHER ===============================================================================
     // ================================================================================================================


    /**
     *  @brief Signature for a per-message-type handler function.
     *
     *  @tparam TContext  The context type passed to handlers (e.g. NetClient, ServerContext).
     *
     *  Handlers receive a typed reference instead of void*, eliminating casts
     *  and preventing accidental context-type mismatches at compile time.
     *
     *  Handlers can assume safely:
     *    - The header has passed all `deserializeHeader` checks.
     *    - `payloadSize >= minPayloadSize(header.type)`.
     *    - The buffer is valid for `payloadSize` bytes starting at `payload`.
     */
    template<typename TContext>
    using PacketHandlerFn = void(*)(TContext& context,
                                    const PacketHeader& header,
                                    const uint8_t* payload,
                                    std::size_t payloadSize);

    /**
     *  @brief A type-safe, fixed-size lookup table mapping EMsgType → handler function.
     *
     *  @tparam TContext  The context type that all bound handlers expect.
     *                    This is enforced at compile time — you cannot accidentally
     *                    bind a handler that expects a different context type.
     *
     *  Index by the raw uint8_t value of EMsgType.  Unregistered slots are nullptr.
     *  This keeps dispatch O(1) and avoids any heap allocation or dynamic containers.
     *
     *  Usage:
     *  @code
     *      PacketDispatcher<ServerContext> dispatcher;
     *      dispatcher.bind(EMsgType::Hello, &onHello);   // onHello(ServerContext&, ...)
     *
     *      // In receive loop — no casts needed:
     *      dispatcher.dispatch(myServerCtx, header, payload, payloadSize);
     *  @endcode
     */
    template<typename TContext>
    struct PacketDispatcher
    {
        static constexpr std::size_t kTableSize = 256; // uint8_t range
        PacketHandlerFn<TContext> table[kTableSize]{};  // value-init → all nullptr

        /** @brief Register a handler for a specific message type. Overwrites any previous binding. */
        void bind(EMsgType type, PacketHandlerFn<TContext> fn)
        {
            table[static_cast<uint8_t>(type)] = fn;
        }

        /**
         *  @brief Look up and invoke the handler for the given header's message type.
         *
         *  @param context     Typed reference forwarded to the handler.
         *  @param header      The deserialized (and validated) packet header.
         *  @param payload     Pointer to the payload bytes.
         *  @param payloadSize Size of the payload in bytes.
         *
         *  @return true if a handler was found and invoked, false if no handler is bound for this message type.
         */
        bool dispatch(TContext& context,
                      const PacketHeader& header,
                      const uint8_t* payload,
                      std::size_t payloadSize) const
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
     *  @brief Parses a raw packet and dispatches it through the given dispatcher.
     *
     *  Combines `tryParsePacket()` + `dispatcher.dispatch()` into one call.
     *  Logs errors for malformed packets and unhandled message types.
     *
     *  @tparam TContext   The context type for the dispatcher.
     *  @param tag         A log prefix string (e.g. "[client]" or "[server]") for error messages.
     *  @param dispatcher  The dispatcher to route the packet through.
     *  @param context     The context reference forwarded to the matched handler.
     *  @param data        Raw packet bytes.
     *  @param dataLength  Total number of bytes in the packet.
     *
     *  @return true if the packet was successfully parsed and a handler was invoked.
     */
    template<typename TContext>
    bool dispatchPacket(const char* tag, const PacketDispatcher<TContext>& dispatcher, TContext& context, const uint8_t* data, std::size_t dataLength)
    {
        PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;

        if (!tryParsePacket(data, dataLength, header, payload, payloadSize))
        {
            std::cerr << tag << " Failed to deserialize PacketHeader (malformed or truncated packet, "
                      << dataLength << " bytes)\n";
            return false;
        }

        if (!dispatcher.dispatch(context, header, payload, payloadSize))
        {
            std::cerr << tag << " No handler for message type "
                      << static_cast<int>(header.type) << '\n';
            return false;
        }

        return true;
    }

} // namespace bomberman::net

#endif // BOMBERMAN_NET_PACKETDISPATCH_H

