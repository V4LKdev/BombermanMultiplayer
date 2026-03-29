/**
 * @file NetClient.Protocol.cpp
 * @brief Authoritative message caching and protocol handlers for the client-side multiplayer connection hub.
 * @ingroup net_client
 */

#include "Net/Client/NetClientInternal.h"

#include "Util/Log.h"

namespace bomberman::net
{
    using namespace net_client_internal;

    bool NetClient::tryGetLatestSnapshot(MsgSnapshot& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.snapshot.has_value())
        {
            return false;
        }

        out = *impl_->matchFlow.snapshot;
        return true;
    }

    uint32_t NetClient::lastSnapshotTick() const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.snapshot.has_value())
        {
            return 0;
        }

        return impl_->matchFlow.snapshot->serverTick;
    }

    bool NetClient::tryGetLatestCorrection(MsgCorrection& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.correction.has_value())
        {
            return false;
        }

        out = *impl_->matchFlow.correction;
        return true;
    }

    uint32_t NetClient::lastCorrectionTick() const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.correction.has_value())
        {
            return 0;
        }

        return impl_->matchFlow.correction->serverTick;
    }

    bool NetClient::tryGetLatestLobbyState(MsgLobbyState& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->cachedLobbyState.has_value())
        {
            return false;
        }

        out = *impl_->cachedLobbyState;
        return true;
    }

    uint32_t NetClient::lobbySilenceMs() const
    {
        if (impl_ == nullptr || !isConnected())
        {
            return 0;
        }

        return elapsedSinceOrZero(impl_->lastLobbyStateReceiveTime, impl_->connectedStartTime);
    }

    bool NetClient::consumePendingLevelInfo(MsgLevelInfo& out)
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.hasLevelInfo || !impl_->matchFlow.levelInfoPending)
        {
            return false;
        }

        out = impl_->matchFlow.levelInfo;
        impl_->matchFlow.levelInfoPending = false;
        return true;
    }

    bool NetClient::tryDequeueGameplayEvent(GameplayEvent& out)
    {
        if (impl_ == nullptr ||
            !isConnected() ||
            impl_->matchFlow.brokenGameplayEventStream ||
            impl_->matchFlow.pendingGameplayEvents.empty())
        {
            return false;
        }

        out = impl_->matchFlow.pendingGameplayEvents.front();
        impl_->matchFlow.pendingGameplayEvents.pop_front();
        return true;
    }

    bool NetClient::hasBrokenGameplayEventStream() const
    {
        return impl_ != nullptr &&
               isConnected() &&
               impl_->matchFlow.hasLevelInfo &&
               impl_->matchFlow.brokenGameplayEventStream;
    }

    uint32_t NetClient::gameplaySilenceMs() const
    {
        if (impl_ == nullptr || !isConnected())
        {
            return 0;
        }

        return elapsedSinceOrZero(impl_->lastGameplayReceiveTime, impl_->connectedStartTime);
    }

    bool NetClient::tryGetLatestMatchStart(MsgMatchStart& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.matchStart.has_value())
        {
            return false;
        }

        out = *impl_->matchFlow.matchStart;
        return true;
    }

    bool NetClient::hasMatchStarted(const uint32_t matchId) const
    {
        return impl_ != nullptr &&
               matchId != 0 &&
               impl_->matchFlow.matchStart.has_value() &&
               impl_->matchFlow.matchStart->matchId == matchId;
    }

    bool NetClient::isMatchCancelled(const uint32_t matchId) const
    {
        return impl_ != nullptr &&
               matchId != 0 &&
               impl_->matchFlow.cancelledMatchId.has_value() &&
               impl_->matchFlow.cancelledMatchId.value() == matchId;
    }

    bool NetClient::tryGetLatestMatchResult(MsgMatchResult& out) const
    {
        if (impl_ == nullptr || !isConnected() || !impl_->matchFlow.matchResult.has_value())
        {
            return false;
        }

        out = *impl_->matchFlow.matchResult;
        return true;
    }

    bool NetClient::tryGetMapSeed(uint32_t& outSeed) const
    {
        if (impl_ == nullptr || !impl_->matchFlow.hasLevelInfo)
        {
            return false;
        }
        outSeed = impl_->matchFlow.levelInfo.mapSeed;
        return true;
    }

    void NetClient::handleWelcome(const uint8_t* payload, std::size_t payloadSize)
    {
        if (state_ != EConnectState::Handshaking)
        {
            LOG_NET_PROTO_WARN("Welcome received in invalid state {} - ignoring", connectStateName(state_));
            return;
        }

        MsgWelcome welcome{};
        if (!deserializeMsgWelcome(payload, payloadSize, welcome))
        {
            LOG_NET_PROTO_WARN("Failed to parse Welcome payload");
            state_ = EConnectState::FailedHandshake;
            return;
        }
        if (welcome.protocolVersion != kProtocolVersion)
        {
            LOG_NET_PROTO_ERROR("Protocol mismatch (server={}, client={})", welcome.protocolVersion, kProtocolVersion);
            state_ = EConnectState::FailedProtocol;
            return;
        }
        playerId_ = welcome.playerId;
        serverTickRate_ = welcome.serverTickRate;
        state_ = EConnectState::Connected;
        impl_->connectedStartTime = SteadyClock::now();
        impl_->diagnostics.recordWelcome(welcome.playerId,
                                         welcome.serverTickRate,
                                         impl_->handshakeStartTime != TimePoint{}
                                             ? static_cast<uint64_t>(elapsedMs(impl_->handshakeStartTime))
                                             : 0u,
                                         impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0);
        LOG_NET_CONN_INFO("Welcome: playerId={}, tickRate={} - session connected, waiting for lobby or next round start",
                          welcome.playerId,
                          welcome.serverTickRate);
    }

    void NetClient::handleReject(const uint8_t* payload, std::size_t payloadSize)
    {
        if (state_ != EConnectState::Handshaking && state_ != EConnectState::Connecting)
        {
            LOG_NET_PROTO_WARN("Reject received in invalid state {} - ignoring", connectStateName(state_));
            return;
        }

        MsgReject reject{};
        if (!deserializeMsgReject(payload, payloadSize, reject))
        {
            LOG_NET_PROTO_WARN("Failed to parse Reject payload - treating as generic handshake failure");
            lastRejectReason_.reset();
            state_ = EConnectState::FailedHandshake;
            return;
        }

        lastRejectReason_ = reject.reason;
        std::string rejectNote = "server reject";
        switch (reject.reason)
        {
            using enum MsgReject::EReason;
            using enum EConnectState;

            case VersionMismatch:
                rejectNote = "server reject version mismatch";
                LOG_NET_CONN_ERROR("Server rejected: version mismatch (server expects v{})", reject.expectedProtocolVersion);
                state_ = FailedProtocol;
                break;
            case ServerFull:
                rejectNote = "server reject full";
                LOG_NET_CONN_WARN("Server rejected: server is full");
                state_ = FailedHandshake;
                break;
            case Banned:
                rejectNote = "server reject banned";
                LOG_NET_CONN_WARN("Server rejected: banned");
                state_ = FailedHandshake;
                break;
            case GameInProgress:
                rejectNote = "server reject game in progress";
                LOG_NET_CONN_WARN("Server rejected: game already in progress");
                state_ = FailedHandshake;
                break;
            case Other:
            default:
                rejectNote = "server reject other";
                LOG_NET_CONN_WARN("Server rejected: reason={}", static_cast<int>(reject.reason));
                state_ = FailedHandshake;
                break;
        }
        impl_->diagnostics.recordPeerLifecycle(NetPeerLifecycleType::PeerRejected,
                                               std::nullopt,
                                               impl_->peer != nullptr ? impl_->peer->incomingPeerID : 0,
                                               rejectNote);
    }

    void NetClient::handleLevelInfo(const uint8_t* payload, std::size_t payloadSize)
    {
        if (impl_ == nullptr)
        {
            return;
        }
        if (state_ != EConnectState::Connected)
        {
            LOG_NET_PROTO_WARN("LevelInfo received in invalid state {} - ignoring", connectStateName(state_));
            return;
        }
        if (playerId_ == kInvalidPlayerId || serverTickRate_ == 0)
        {
            LOG_NET_PROTO_WARN("LevelInfo received without a valid connected session - ignoring");
            return;
        }

        MsgLevelInfo levelInfo{};
        if (!deserializeMsgLevelInfo(payload, payloadSize, levelInfo))
        {
            LOG_NET_PROTO_WARN("Failed to parse LevelInfo payload");
            return;
        }

        if (impl_->matchFlow.hasLevelInfo &&
            levelInfo.matchId <= impl_->matchFlow.levelInfo.matchId)
        {
            LOG_NET_CONN_DEBUG("Ignoring stale or duplicate LevelInfo matchId={} latestMatchId={}",
                               levelInfo.matchId,
                               impl_->matchFlow.levelInfo.matchId);
            return;
        }

        impl_->matchFlow.beginBootstrap(levelInfo);
        impl_->cachedLobbyState.reset();
        resetLocalMatchBootstrapState();
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = playerId_;
            event.detailA = levelInfo.matchId;
            event.detailB = levelInfo.mapSeed;
            event.note = "level info received";
            impl_->diagnostics.recordEvent(event);
        }
        LOG_NET_CONN_INFO("Received LevelInfo matchId={} seed={}",
                          levelInfo.matchId,
                          levelInfo.mapSeed);
    }

    void NetClient::handleLobbyState(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_CONN_DEBUG("Received LobbyState payload while not connected - ignoring");
            return;
        }

        MsgLobbyState lobbyState{};
        if (!deserializeMsgLobbyState(payload, payloadSize, lobbyState))
        {
            LOG_NET_PROTO_WARN("Failed to parse LobbyState payload");
            return;
        }

        if (impl_->matchFlow.hasLevelInfo)
        {
            resetCurrentMatchSession();
        }

        impl_->cachedLobbyState = lobbyState;
        impl_->lastLobbyStateReceiveTime = SteadyClock::now();
        LOG_NET_CONN_TRACE("Received LobbyState update");
    }

    void NetClient::handleMatchStart(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_CONN_DEBUG("Received MatchStart payload while not connected - ignoring");
            return;
        }

        MsgMatchStart matchStart{};
        if (!deserializeMsgMatchStart(payload, payloadSize, matchStart))
        {
            LOG_NET_PROTO_WARN("Failed to parse MatchStart payload");
            return;
        }

        if (impl_->matchFlow.matchStart.has_value() &&
            matchStart.matchId <= impl_->matchFlow.matchStart->matchId)
        {
            LOG_NET_CONN_DEBUG("Ignoring stale or duplicate MatchStart matchId={} latestMatchId={}",
                               matchStart.matchId,
                               impl_->matchFlow.matchStart->matchId);
            return;
        }

        if (!impl_->matchFlow.isActiveMatch(matchStart.matchId))
        {
            LOG_NET_CONN_DEBUG("Ignoring MatchStart for inactive matchId={} currentMatchId={}",
                               matchStart.matchId,
                               impl_->matchFlow.currentMatchId());
            return;
        }

        impl_->matchFlow.matchStart = matchStart;
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = playerId_;
            event.detailA = matchStart.matchId;
            event.detailB = matchStart.unlockServerTick;
            event.note = "match start received";
            impl_->diagnostics.recordEvent(event);
        }
        LOG_NET_CONN_INFO("Received MatchStart matchId={} goTick={} unlockTick={}",
                          matchStart.matchId,
                          matchStart.goShowServerTick,
                          matchStart.unlockServerTick);
    }

    void NetClient::handleMatchCancelled(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_CONN_DEBUG("Received MatchCancelled payload while not connected - ignoring");
            return;
        }

        MsgMatchCancelled matchCancelled{};
        if (!deserializeMsgMatchCancelled(payload, payloadSize, matchCancelled))
        {
            LOG_NET_PROTO_WARN("Failed to parse MatchCancelled payload");
            return;
        }

        if (impl_->matchFlow.cancelledMatchId.has_value() &&
            matchCancelled.matchId <= impl_->matchFlow.cancelledMatchId.value())
        {
            LOG_NET_CONN_DEBUG("Ignoring stale or duplicate MatchCancelled matchId={} latestMatchId={}",
                               matchCancelled.matchId,
                               impl_->matchFlow.cancelledMatchId.value());
            return;
        }

        if (impl_->matchFlow.isActiveMatch(matchCancelled.matchId))
        {
            resetCurrentMatchSession();
        }

        impl_->matchFlow.cancelledMatchId = matchCancelled.matchId;
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = playerId_;
            event.detailA = matchCancelled.matchId;
            event.note = "match cancelled received";
            impl_->diagnostics.recordEvent(event);
        }
        LOG_NET_CONN_INFO("Received MatchCancelled matchId={}", matchCancelled.matchId);
    }

    void NetClient::handleMatchResult(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_CONN_DEBUG("Received MatchResult payload while not connected - ignoring");
            return;
        }

        MsgMatchResult matchResult{};
        if (!deserializeMsgMatchResult(payload, payloadSize, matchResult))
        {
            LOG_NET_PROTO_WARN("Failed to parse MatchResult payload");
            return;
        }

        if (!impl_->matchFlow.isActiveMatch(matchResult.matchId))
        {
            LOG_NET_CONN_DEBUG("Ignoring MatchResult for inactive matchId={} currentMatchId={}",
                               matchResult.matchId,
                               impl_->matchFlow.currentMatchId());
            return;
        }

        if (impl_->matchFlow.matchResult.has_value() &&
            matchResult.matchId <= impl_->matchFlow.matchResult->matchId)
        {
            LOG_NET_CONN_DEBUG("Ignoring stale or duplicate MatchResult matchId={} latestMatchId={}",
                               matchResult.matchId,
                               impl_->matchFlow.matchResult->matchId);
            return;
        }

        impl_->matchFlow.matchResult = matchResult;
        const bool localWon = matchResult.result == MsgMatchResult::EResult::Win &&
                              playerId_ != kInvalidPlayerId &&
                              matchResult.winnerPlayerId == playerId_;
        const char* localResultName = matchResult.result == MsgMatchResult::EResult::Draw
                                          ? "draw"
                                          : (localWon ? "win" : "loss");
        {
            NetEvent event{};
            event.type = NetEventType::Flow;
            event.peerId = matchResult.result == MsgMatchResult::EResult::Win
                               ? matchResult.winnerPlayerId
                               : kInvalidPlayerId;
            event.detailA = matchResult.matchId;
            event.note = std::string("match result received: ") + localResultName;
            impl_->diagnostics.recordEvent(event);
        }
        LOG_NET_CONN_INFO("Received MatchResult matchId={} result={}",
                          matchResult.matchId,
                          localResultName);
    }

    void NetClient::handleSnapshot(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Snapshot payload while not connected - ignoring");
            return;
        }

        MsgSnapshot snapshot;
        if (!deserializeMsgSnapshot(payload, payloadSize, snapshot))
        {
            LOG_NET_PROTO_WARN("Failed to parse Snapshot payload");
            return;
        }
        if (!impl_->matchFlow.isActiveMatch(snapshot.matchId))
        {
            LOG_NET_SNAPSHOT_DEBUG("Ignoring Snapshot for inactive matchId={} currentMatchId={}",
                                   snapshot.matchId,
                                   impl_->matchFlow.currentMatchId());
            return;
        }

        if (impl_->matchFlow.snapshot.has_value() && snapshot.serverTick <= impl_->matchFlow.snapshot->serverTick)
        {
            impl_->diagnostics.recordStaleSnapshotIgnored(snapshot.serverTick);
            return;
        }

        impl_->matchFlow.snapshot = snapshot;
        impl_->lastSnapshotReceiveTime = SteadyClock::now();
        impl_->lastGameplayReceiveTime = impl_->lastSnapshotReceiveTime;

        if (snapshot.serverTick % kSnapshotLogEveryN == 0)
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Snapshot tick={} playerCount={} bombCount={}",
                                   snapshot.serverTick,
                                   snapshot.playerCount,
                                   snapshot.bombCount);
        }
    }

    void NetClient::handleCorrection(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Correction payload while not connected - ignoring");
            return;
        }

        MsgCorrection correction{};
        if (!deserializeMsgCorrection(payload, payloadSize, correction))
        {
            LOG_NET_PROTO_WARN("Failed to parse Correction payload");
            return;
        }
        if (!impl_->matchFlow.isActiveMatch(correction.matchId))
        {
            LOG_NET_SNAPSHOT_DEBUG("Ignoring Correction for inactive matchId={} currentMatchId={}",
                                   correction.matchId,
                                   impl_->matchFlow.currentMatchId());
            return;
        }

        if (impl_->matchFlow.correction.has_value() && correction.serverTick <= impl_->matchFlow.correction->serverTick)
        {
            impl_->diagnostics.recordStaleCorrectionIgnored(correction.serverTick, correction.lastProcessedInputSeq);
            return;
        }

        impl_->matchFlow.correction = correction;
        impl_->lastCorrectionReceiveTime = SteadyClock::now();
        impl_->lastGameplayReceiveTime = impl_->lastCorrectionReceiveTime;

        if (correction.serverTick % kSnapshotLogEveryN == 0)
        {
            LOG_NET_SNAPSHOT_DEBUG("Received Correction tick={} lastProcessed={} pos=({}, {})",
                                   correction.serverTick,
                                   correction.lastProcessedInputSeq,
                                   correction.xQ,
                                   correction.yQ);
        }
    }

    void NetClient::handleBombPlaced(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received BombPlaced payload while not connected - ignoring");
            return;
        }

        MsgBombPlaced bombPlaced{};
        if (!deserializeMsgBombPlaced(payload, payloadSize, bombPlaced))
        {
            LOG_NET_PROTO_WARN("Failed to parse BombPlaced payload");
            return;
        }
        if (!impl_->matchFlow.isActiveMatch(bombPlaced.matchId))
        {
            LOG_NET_SNAPSHOT_DEBUG("Ignoring BombPlaced for inactive matchId={} currentMatchId={}",
                                   bombPlaced.matchId,
                                   impl_->matchFlow.currentMatchId());
            return;
        }

        enqueueGameplayEvent(GameplayEvent::fromBombPlaced(bombPlaced));

        LOG_NET_SNAPSHOT_DEBUG("Received BombPlaced tick={} ownerId={} cell=({}, {}) radius={} explodeTick={}",
                               bombPlaced.serverTick,
                               bombPlaced.ownerId,
                               bombPlaced.col,
                               bombPlaced.row,
                               bombPlaced.radius,
                               bombPlaced.explodeTick);
    }

    void NetClient::handleExplosionResolved(const uint8_t* payload, std::size_t payloadSize)
    {
        if (!impl_ || !isConnected())
        {
            LOG_NET_SNAPSHOT_DEBUG("Received ExplosionResolved payload while not connected - ignoring");
            return;
        }

        MsgExplosionResolved explosion{};
        if (!deserializeMsgExplosionResolved(payload, payloadSize, explosion))
        {
            LOG_NET_PROTO_WARN("Failed to parse ExplosionResolved payload");
            return;
        }
        if (!impl_->matchFlow.isActiveMatch(explosion.matchId))
        {
            LOG_NET_SNAPSHOT_DEBUG("Ignoring ExplosionResolved for inactive matchId={} currentMatchId={}",
                                   explosion.matchId,
                                   impl_->matchFlow.currentMatchId());
            return;
        }

        enqueueGameplayEvent(GameplayEvent::fromExplosionResolved(explosion));

        LOG_NET_SNAPSHOT_DEBUG("Received ExplosionResolved tick={} ownerId={} origin=({}, {}) blastCells={} destroyedBricks={} killedMask=0x{:02x}",
                               explosion.serverTick,
                               explosion.ownerId,
                               explosion.originCol,
                               explosion.originRow,
                               explosion.blastCellCount,
                               explosion.destroyedBrickCount,
                               explosion.killedPlayerMask);
    }

    void NetClient::enqueueGameplayEvent(const GameplayEvent& event)
    {
        if (impl_ == nullptr)
        {
            return;
        }

        if (impl_->matchFlow.brokenGameplayEventStream)
        {
            return;
        }

        if (impl_->matchFlow.pendingGameplayEvents.size() >= kMaxPendingGameplayEvents)
        {
            LOG_NET_SNAPSHOT_ERROR(
                "Reliable gameplay event queue overflow for matchId={} - invalidating gameplay event stream",
                impl_->matchFlow.currentMatchId());
            impl_->diagnostics.recordBrokenGameplayEventStream(impl_->matchFlow.currentMatchId());
            impl_->matchFlow.brokenGameplayEventStream = true;
            impl_->matchFlow.pendingGameplayEvents.clear();
            return;
        }

        impl_->matchFlow.pendingGameplayEvents.push_back(event);
        impl_->lastGameplayReceiveTime = SteadyClock::now();
        impl_->diagnostics.samplePendingGameplayEventDepth(impl_->matchFlow.pendingGameplayEvents.size());
    }
} // namespace bomberman::net
