/**
 * @file ServerHandlers.cpp
 * @brief Authoritative server receive-path validation and typed packet handling.
 */

#include "ServerHandlers.h"

#include <optional>
#include <string_view>

#include "Const.h"
#include "Net/NetSend.h"
#include "Net/PacketDispatch.h"
#include "ServerState.h"
#include "Util/Log.h"

using namespace bomberman::net;

namespace bomberman::server
{
    // =================================================================================================================
    // ===== Internal Helpers ==========================================================================================
    // =================================================================================================================

    namespace
    {
        // ----- Temporary match bootstrap constants -----

        /** @brief Current temporary authoritative spawn used for newly accepted peers. */
        constexpr sim::TilePos kDefaultSpawnPos{
            playerStartX * 256 + 128,
            playerStartY * 256 + 128
        };

        // ----- Diagnostics helpers -----

        constexpr std::string_view rejectReasonName(const MsgReject::EReason reason)
        {
            using enum MsgReject::EReason;

            switch (reason)
            {
                case VersionMismatch: return "version mismatch";
                case ServerFull:      return "server full";
                case Banned:          return "banned";
                case Other:           return "other";
                default:              return "unknown";
            }
        }

        /** @brief Records a peer lifecycle event when server diagnostics are enabled. */
        void recordPeerLifecycle(NetDiagnostics* diag,
                                 const NetPeerLifecycleType type,
                                 const uint8_t peerId,
                                 const uint32_t transportPeerId,
                                 const std::string_view note = {})
        {
            if (!diag)
                return;

            diag->recordPeerLifecycle(type, peerId, transportPeerId, note);
        }

        /** @brief Records one reliable control-packet send attempt for diagnostics. */
        void recordControlPacketSent(const PacketDispatchContext& ctx,
                                     const EMsgType type,
                                     const uint8_t peerId,
                                     const std::size_t payloadSize,
                                     const NetPacketResult result = NetPacketResult::Ok)
        {
            if (!ctx.diag)
            {
                return;
            }

            ctx.diag->recordPacketSent(type,
                                       peerId,
                                       static_cast<uint8_t>(EChannel::ControlReliable),
                                       kPacketHeaderSize + payloadSize,
                                       result);
        }

        // ----- Handshake helpers -----

        /**
         * @brief Sends a Reject packet and starts a graceful disconnect.
         *
         * @warning Use this only before any handshake-accept packet has been
         * queued for the peer. Once `Welcome` has been queued, the peer must be
         * reset instead so a partial handshake is not delivered.
         */
        void sendReject(const PacketDispatchContext& ctx, MsgReject::EReason reason)
        {
            MsgReject reject{};
            reject.reason = reason;
            reject.expectedProtocolVersion = reason == MsgReject::EReason::VersionMismatch ? kProtocolVersion : 0;

            recordPeerLifecycle(ctx.diag,
                                NetPeerLifecycleType::PeerRejected,
                                ctx.recordedPlayerId.value_or(0xFF),
                                ctx.peer->incomingPeerID,
                                rejectReasonName(reason));

            if (const bool sent = queueReliableControl(ctx.peer, makeRejectPacket(reject)); sent)
            {
                flush(ctx.state.host);
                LOG_NET_CONN_INFO("Sent Reject (reason={}) to peer {}",
                                  static_cast<int>(reason), ctx.peer->incomingPeerID);
                recordControlPacketSent(ctx, EMsgType::Reject, ctx.recordedPlayerId.value_or(0xFF), kMsgRejectSize);
            }
            else
            {
                recordControlPacketSent(ctx,
                                        EMsgType::Reject,
                                        ctx.recordedPlayerId.value_or(0xFF),
                                        kMsgRejectSize,
                                        NetPacketResult::Dropped);
            }

            enet_peer_disconnect_later(ctx.peer, 0);
        }

        /**
         * @brief Resets a peer after `Welcome` was already queued but the full handshake could not be completed.
         *
         * This intentionally avoids sending `Reject`, because `Welcome` may
         * already be queued on the reliable channel and a later reject would
         * produce a contradictory partial handshake on the client.
         */
        void abortPartialHandshake(PacketDispatchContext& ctx,
                                   const uint8_t playerId,
                                   const EMsgType failedType,
                                   const std::size_t failedPayloadSize,
                                   std::string_view failedLabel)
        {
            recordControlPacketSent(ctx, failedType, playerId, failedPayloadSize, NetPacketResult::Dropped);
            releasePlayerId(ctx.state, playerId);
            ctx.receiveResult = NetPacketResult::Rejected;

            LOG_NET_CONN_ERROR(
                "Failed to queue {} for peer {} after Welcome was already queued; resetting peer to drop the partial handshake",
                failedLabel,
                ctx.peer->incomingPeerID);

            enet_peer_reset(ctx.peer);
        }

        /** @brief Returns true once a live peer session exists and Hello has been accepted for it. */
        bool hasAcceptedPlayer(const ENetPeer* peer)
        {
            const auto* session = getPeerSession(peer);
            return session != nullptr && session->playerId.has_value();
        }

        /** @brief Returns the active in-match authoritative state for an accepted peer session, if any. */
        MatchPlayerState* getAcceptedMatchPlayerState(ServerState& state, const PeerSession& session)
        {
            if (!session.playerId.has_value())
                return nullptr;

            auto& matchEntry = state.matchPlayers[session.playerId.value()];
            return matchEntry.has_value() ? &matchEntry.value() : nullptr;
        }

        /** @brief Returns the accepted player seat for a peer, if Hello has already been accepted. */
        std::optional<uint8_t> acceptedPlayerId(const ENetPeer* peer)
        {
            const auto* session = getPeerSession(peer);
            return (session != nullptr) ? session->playerId : std::nullopt;
        }

        struct BufferedInputStats
        {
            uint8_t tooLateCount = 0;
            uint8_t tooLateDirectCount = 0;
            uint8_t tooLateBufferedCount = 0;
            uint8_t tooFarAheadCount = 0;
            uint32_t firstTooFarAheadSeq = 0;
            uint32_t lastTooFarAheadSeq = 0;
        };

        /** @brief Reserves a player id for a validated Hello or rejects the peer if no slot is available. */
        [[nodiscard]]
        std::optional<uint8_t> reserveHelloPlayerId(PacketDispatchContext& ctx)
        {
            if (ctx.state.playerIdPoolSize == 0)
            {
                LOG_NET_CONN_WARN("Server full ({}/{}) - rejecting peer {}",
                                  static_cast<int>(kMaxPlayers),
                                  static_cast<int>(kMaxPlayers),
                                  ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Rejected;
                sendReject(ctx, MsgReject::EReason::ServerFull);
                return std::nullopt;
            }

            const auto reservedPlayerId = acquirePlayerId(ctx.state);
            if (!reservedPlayerId.has_value())
            {
                ctx.receiveResult = NetPacketResult::Rejected;
                sendReject(ctx, MsgReject::EReason::ServerFull);
                return std::nullopt;
            }

            ctx.recordedPlayerId = reservedPlayerId;
            return reservedPlayerId;
        }

        /** @brief Queues the authoritative Welcome response for a validated Hello. */
        [[nodiscard]]
        bool queueWelcomeForHello(PacketDispatchContext& ctx, const uint8_t playerId)
        {
            MsgWelcome welcome{};
            welcome.protocolVersion = kProtocolVersion;
            welcome.playerId = playerId;
            welcome.serverTickRate = sim::kTickRate;

            if (!queueReliableControl(ctx.peer, makeWelcomePacket(welcome)))
            {
                LOG_NET_CONN_ERROR("Failed to send Welcome to peer {} - rejecting", ctx.peer->incomingPeerID);
                recordControlPacketSent(ctx,
                                        EMsgType::Welcome,
                                        playerId,
                                        kMsgWelcomeSize,
                                        NetPacketResult::Dropped);
                releasePlayerId(ctx.state, playerId);
                ctx.receiveResult = NetPacketResult::Rejected;
                sendReject(ctx, MsgReject::EReason::Other);
                return false;
            }

            LOG_NET_CONN_INFO("Queued Welcome to playerId={}", playerId);
            recordControlPacketSent(ctx, EMsgType::Welcome, playerId, kMsgWelcomeSize);
            return true;
        }

        /**
         * @brief Queues the temporary immediate `LevelInfo` bootstrap after a successful Welcome.
         *
         * This is intentionally separate from acceptance semantics so the later
         * lobby/gameflow state machine can move the call site without
         * reworking Hello validation and Welcome handling again.
         */
        [[nodiscard]]
        bool queueImmediateLevelBootstrap(PacketDispatchContext& ctx, const uint8_t playerId)
        {
            MsgLevelInfo levelInfo{};
            levelInfo.mapSeed = ctx.state.mapSeed;
            if (!queueReliableControl(ctx.peer, makeLevelInfoPacket(levelInfo)))
            {
                abortPartialHandshake(ctx, playerId, EMsgType::LevelInfo, kMsgLevelInfoSize, "LevelInfo");
                return false;
            }

            recordControlPacketSent(ctx, EMsgType::LevelInfo, playerId, kMsgLevelInfoSize);
            return true;
        }

        /** @brief Commits the accepted peer into stable server storage and records the acceptance lifecycle event. */
        void finalizeAcceptedHello(PacketDispatchContext& ctx, const uint8_t playerId, std::string_view playerName)
        {
            auto* session = getPeerSession(ctx.peer);
            if (session == nullptr)
            {
                LOG_NET_CONN_ERROR("Peer {} lost its live session before Hello accept finalization", ctx.peer->incomingPeerID);
                releasePlayerId(ctx.state, playerId);
                ctx.receiveResult = NetPacketResult::Rejected;
                enet_peer_reset(ctx.peer);
                return;
            }

            // TODO: individual spawn points per player.
            acceptPeerSession(ctx.state, *session, playerId, playerName);
            createMatchPlayerState(ctx.state, playerId, kDefaultSpawnPos);

            recordPeerLifecycle(ctx.diag,
                                NetPeerLifecycleType::PlayerAccepted,
                                playerId,
                                static_cast<uint32_t>(ctx.peer->incomingPeerID));
            ctx.receiveResult = NetPacketResult::Ok;
        }

        // ----- Input receive helpers -----

        /** @brief Returns the accepted in-match player state for an Input packet or classifies the packet as rejected. */
        [[nodiscard]]
        MatchPlayerState* requireAcceptedMatchPlayer(PacketDispatchContext& ctx)
        {
            auto* session = getPeerSession(ctx.peer);
            if (session == nullptr)
            {
                LOG_NET_INPUT_ERROR("Input peer {} has no live peer session - ignoring", ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            if (!session->playerId.has_value())
            {
                LOG_NET_INPUT_WARN("Input from non-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            auto* matchPlayer = getAcceptedMatchPlayerState(ctx.state, *session);
            if (matchPlayer == nullptr)
            {
                LOG_NET_INPUT_ERROR("Input playerId={} has no active match state - ignoring", *session->playerId);
                ctx.receiveResult = NetPacketResult::Rejected;
                return nullptr;
            }

            ctx.recordedPlayerId = session->playerId;
            return matchPlayer;
        }

        /**
         * @brief Parses a fixed-size Input payload or classifies the packet as malformed.
         *
         * @note Successful parse guarantees `msgInput.count >= 1`, so helpers
         * that derive the newest batch sequence can safely subtract one.
         */
        [[nodiscard]]
        bool parseInputMessage(PacketDispatchContext& ctx, const uint8_t* payload, const std::size_t size, MsgInput& msgInput)
        {
            if (!deserializeMsgInput(payload, size, msgInput))
            {
                LOG_NET_PROTO_WARN("Failed to parse Input payload from peer {}", ctx.peer->incomingPeerID);
                ctx.receiveResult = NetPacketResult::Malformed;
                return false;
            }

            return true;
        }

        /** @brief Returns the newest absolute input sequence carried by one successfully parsed batch. */
        [[nodiscard]]
        uint32_t highestSeqInBatch(const MsgInput& msgInput)
        {
            return msgInput.baseInputSeq + static_cast<uint32_t>(msgInput.count) - 1u;
        }

        /** @brief Records and consumes a fully stale batch whose newest sequence was already processed. */
        [[nodiscard]]
        bool handleFullyStaleBatch(PacketDispatchContext& ctx,
                                   const MatchPlayerState& matchPlayer,
                                   const MsgInput& msgInput,
                                   const uint32_t highestSeq)
        {
            if (highestSeq > matchPlayer.lastProcessedInputSeq)
                return false;

            if (ctx.diag)
            {
                ctx.diag->recordInputPacketFullyStale();
                ctx.diag->recordInputEntriesTooLate(msgInput.count);
                ctx.diag->recordInputEntriesTooLateDirect(1);
                ctx.diag->recordInputEntriesTooLateBuffered(msgInput.count - 1u);
            }

            ctx.receiveResult = NetPacketResult::Ok;
            return true;
        }

        /** @brief Stores accepted input entries into the authoritative ring while counting discarded ones. */
        [[nodiscard]]
        BufferedInputStats bufferInputBatch(MatchPlayerState& matchPlayer,
                                            const MsgInput& msgInput,
                                            const uint32_t highestSeq,
                                            const uint32_t maxAcceptableSeq)
        {
            BufferedInputStats stats{};

            for (uint8_t i = 0; i < msgInput.count; ++i)
            {
                const uint32_t seq = msgInput.baseInputSeq + i;
                const uint8_t buttons = msgInput.inputs[i];
                const bool isDirectEntry = (seq == highestSeq);

                if (seq <= matchPlayer.lastProcessedInputSeq)
                {
                    ++stats.tooLateCount;
                    if (isDirectEntry)
                        ++stats.tooLateDirectCount;
                    else
                        ++stats.tooLateBufferedCount;
                    continue;
                }

                if (seq > maxAcceptableSeq)
                {
                    if (stats.tooFarAheadCount == 0)
                        stats.firstTooFarAheadSeq = seq;
                    stats.lastTooFarAheadSeq = seq;
                    ++stats.tooFarAheadCount;
                    continue;
                }

                auto& slot = matchPlayer.inputRing[seq % kServerInputBufferSize];
                const bool sameSeqAlreadyBuffered = slot.valid && slot.seq == seq;
                const bool seenDirect = sameSeqAlreadyBuffered ? slot.seenDirect : false;
                const bool seenBuffered = sameSeqAlreadyBuffered ? slot.seenBuffered : false;

                slot.seq = seq;
                slot.buttons = buttons;
                slot.valid = true;
                slot.seenDirect = seenDirect || isDirectEntry;
                slot.seenBuffered = seenBuffered || !isDirectEntry;

                if (seq > matchPlayer.lastReceivedInputSeq)
                    matchPlayer.lastReceivedInputSeq = seq;
            }

            return stats;
        }

        /** @brief Records per-batch discard counts for diagnostics. */
        void recordBufferedInputDiagnostics(PacketDispatchContext& ctx, const BufferedInputStats& stats)
        {
            if (!ctx.diag)
                return;

            ctx.diag->recordInputEntriesTooLate(stats.tooLateCount);
            ctx.diag->recordInputEntriesTooLateDirect(stats.tooLateDirectCount);
            ctx.diag->recordInputEntriesTooLateBuffered(stats.tooLateBufferedCount);
            ctx.diag->recordInputEntriesTooFarAhead(stats.tooFarAheadCount);
        }

        /** @brief Updates the repeated too-far-ahead warning state after one accepted Input batch. */
        void updateTooFarAheadBatchWarning(PacketDispatchContext& ctx,
                                           MatchPlayerState& matchPlayer,
                                           const MsgInput& msgInput,
                                           const uint32_t highestSeq,
                                           const uint32_t maxAcceptableSeq,
                                           const BufferedInputStats& stats)
        {
            if (stats.tooFarAheadCount == 0)
            {
                matchPlayer.consecutiveTooFarAheadBatches = 0;
                return;
            }

            ++matchPlayer.consecutiveTooFarAheadBatches;
            const uint16_t streak = matchPlayer.consecutiveTooFarAheadBatches;
            if (streak >= kRepeatedInputWarnThreshold
                && ctx.state.serverTick >= matchPlayer.nextTooFarAheadWarnTick)
            {
                LOG_NET_INPUT_WARN(
                    "Repeated too-far-ahead input rejections playerId={} streak={} latestRejectedSeqs=[{}..{}] count={} batch=[{}..{}] maxAcceptable={} lastRecv={} lastProcessed={}",
                    matchPlayer.playerId,
                    streak,
                    stats.firstTooFarAheadSeq,
                    stats.lastTooFarAheadSeq,
                    stats.tooFarAheadCount,
                    msgInput.baseInputSeq,
                    highestSeq,
                    maxAcceptableSeq,
                    matchPlayer.lastReceivedInputSeq,
                    matchPlayer.lastProcessedInputSeq);

                matchPlayer.nextTooFarAheadWarnTick = ctx.state.serverTick + kRepeatedInputWarnCooldownTicks;
            }
        }

        /** @brief Emits the periodic accepted-input trace for one match-player batch. */
        void logAcceptedInputBatch(const MatchPlayerState& matchPlayer, const MsgInput& msgInput, const uint32_t highestSeq)
        {
            if ((highestSeq % kServerInputBatchLogIntervalTicks) != 0)
                return;

            LOG_NET_INPUT_DEBUG("Input playerId={} batch=[{}..{}] lastRecv={} lastProcessed={}",
                                matchPlayer.playerId,
                                msgInput.baseInputSeq,
                                highestSeq,
                                matchPlayer.lastReceivedInputSeq,
                                matchPlayer.lastProcessedInputSeq);
        }

    } // namespace

    // =================================================================================================================
    // ===== Message Handlers ==========================================================================================
    // =================================================================================================================

    void onHello(PacketDispatchContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t payloadSize)
    {
        // Ignore duplicate Hello from an already-handshaked peer.
        if (hasAcceptedPlayer(ctx.peer))
        {
            LOG_NET_CONN_DEBUG("Duplicate Hello from already-handshaked peer {} - ignoring", ctx.peer->incomingPeerID);
            ctx.receiveResult = NetPacketResult::Rejected;
            ctx.recordedPlayerId = acceptedPlayerId(ctx.peer);
            return;
        }

        MsgHello msgHello{};
        if (!deserializeMsgHello(payload, payloadSize, msgHello))
        {
            LOG_NET_PROTO_WARN("Failed to parse Hello payload from peer {}", ctx.peer->incomingPeerID);
            ctx.receiveResult = NetPacketResult::Malformed;
            return;
        }

        // Reject on protocol version mismatch.
        if (msgHello.protocolVersion != kProtocolVersion)
        {
            LOG_NET_PROTO_ERROR("Protocol mismatch: peer {} sent version {}, expected {}",
                                ctx.peer->incomingPeerID, msgHello.protocolVersion, kProtocolVersion);
            ctx.receiveResult = NetPacketResult::Rejected;
            sendReject(ctx, MsgReject::EReason::VersionMismatch);
            return;
        }

        const std::string_view playerName(msgHello.name, boundedStrLen(msgHello.name, kPlayerNameMax));
        LOG_NET_CONN_INFO("Hello from \"{}\" (peer {})", playerName, ctx.peer->incomingPeerID);

        const std::optional<uint8_t> reservedPlayerId = reserveHelloPlayerId(ctx);
        if (!reservedPlayerId.has_value())
            return;

        const auto playerId = reservedPlayerId.value();

        if (!queueWelcomeForHello(ctx, playerId))
            return;

        if (!queueImmediateLevelBootstrap(ctx, playerId))
            return;

        flush(ctx.state.host);
        LOG_NET_CONN_INFO("Sent immediate post-accept LevelInfo bootstrap (seed={}) to playerId={}",
                          ctx.state.mapSeed,
                          playerId);

        finalizeAcceptedHello(ctx, playerId, playerName);
    }

    void onInput(PacketDispatchContext& ctx, const PacketHeader& /*header*/, const uint8_t* payload, std::size_t size)
    {
        auto* matchPlayer = requireAcceptedMatchPlayer(ctx);
        if (matchPlayer == nullptr)
            return;

        MsgInput msgInput{};
        if (!parseInputMessage(ctx, payload, size, msgInput))
            return;

        const uint32_t highestSeq = highestSeqInBatch(msgInput);
        const uint32_t maxAcceptableSeq = matchPlayer->lastProcessedInputSeq + kMaxBufferedInputLead;

        if (ctx.diag)
            ctx.diag->recordInputPacketReceived();

        if (handleFullyStaleBatch(ctx, *matchPlayer, msgInput, highestSeq))
            return;

        const BufferedInputStats stats = bufferInputBatch(*matchPlayer, msgInput, highestSeq, maxAcceptableSeq);
        recordBufferedInputDiagnostics(ctx, stats);
        updateTooFarAheadBatchWarning(ctx, *matchPlayer, msgInput, highestSeq, maxAcceptableSeq, stats);
        logAcceptedInputBatch(*matchPlayer, msgInput, highestSeq);

        ctx.receiveResult = NetPacketResult::Ok;
    }

    // =================================================================================================================
    // ===== Packet Dispatcher =========================================================================================
    // =================================================================================================================

    namespace
    {
        PacketDispatcher<PacketDispatchContext> makeServerDispatcher()
        {
            PacketDispatcher<PacketDispatchContext> d{};
            d.bind(EMsgType::Hello, &onHello);
            d.bind(EMsgType::Input, &onInput);
            return d;
        }

        const PacketDispatcher<PacketDispatchContext> gDispatcher = makeServerDispatcher();
    } // namespace

    void handleReceiveEvent(const ENetEvent& event, ServerState& state)
    {
        const std::size_t dataLength = event.packet->dataLength;
        const uint8_t channelId = event.channelID;

        LOG_NET_PACKET_TRACE("Received {} bytes on channel {}", dataLength, channelName(channelId));

        PacketDispatchContext ctx{state, event.peer, &state.diag};
        ctx.receiveResult = NetPacketResult::Rejected;
        ctx.recordedPlayerId = acceptedPlayerId(event.peer);

        PacketHeader header{};
        const uint8_t* payload = nullptr;
        std::size_t payloadSize = 0;

        if (!tryParsePacket(event.packet->data, dataLength, header, payload, payloadSize))
        {
            LOG_NET_PACKET_WARN("Failed to deserialize PacketHeader (malformed or truncated, {} bytes)", dataLength);
            state.diag.recordMalformedPacketRecv(ctx.recordedPlayerId.value_or(0xFF), channelId, dataLength, "header parse failed");
            return;
        }

        if (!isExpectedChannelFor(header.type, channelId))
        {
            LOG_NET_PACKET_WARN("Rejected {} on wrong channel: got {}, expected {}",
                                msgTypeName(header.type),
                                channelName(channelId),
                                channelName(static_cast<uint8_t>(expectedChannelFor(header.type))));
            state.diag.recordPacketRecv(header.type, ctx.recordedPlayerId.value_or(0xFF), channelId, dataLength, NetPacketResult::Rejected);
            return;
        }

        if (!gDispatcher.dispatch(ctx, header, payload, payloadSize))
        {
            LOG_NET_PACKET_TRACE("No handler for message type 0x{:02x}", static_cast<int>(header.type));
            state.diag.recordPacketRecv(header.type, ctx.recordedPlayerId.value_or(0xFF), channelId, dataLength, NetPacketResult::Rejected);
            return;
        }

        state.diag.recordPacketRecv(header.type, ctx.recordedPlayerId.value_or(0xFF), channelId, dataLength, ctx.receiveResult);
    }

} // namespace bomberman::server
