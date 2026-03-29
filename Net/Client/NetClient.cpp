/**
 * @file NetClient.cpp
 * @brief Shared construction and common state access for the client-side multiplayer connection hub.
 * @ingroup net_client
 */

#include "Net/Client/NetClientInternal.h"

namespace bomberman::net
{
    NetClient::NetClient()
    {
        using enum EMsgType;

        impl_ = std::make_unique<Impl>();

        impl_->dispatcher.bind(Welcome, &Impl::onWelcome);
        impl_->dispatcher.bind(Reject, &Impl::onReject);
        impl_->dispatcher.bind(LevelInfo, &Impl::onLevelInfo);
        impl_->dispatcher.bind(LobbyState, &Impl::onLobbyState);
        impl_->dispatcher.bind(MatchStart, &Impl::onMatchStart);
        impl_->dispatcher.bind(MatchCancelled, &Impl::onMatchCancelled);
        impl_->dispatcher.bind(MatchResult, &Impl::onMatchResult);
        impl_->dispatcher.bind(Snapshot, &Impl::onSnapshot);
        impl_->dispatcher.bind(Correction, &Impl::onCorrection);
        impl_->dispatcher.bind(BombPlaced, &Impl::onBombPlaced);
        impl_->dispatcher.bind(ExplosionResolved, &Impl::onExplosionResolved);
    }

    NetClient::~NetClient() noexcept
    {
        try
        {
            disconnectBlocking();
        }
        catch (...)
        {
            destroyTransport();
            resetState();
        }

        shutdownENet();
    }

    void NetClient::setDiagnosticsConfig(const bool enabled,
                                         const bool predictionEnabled,
                                         const bool remoteSmoothingEnabled)
    {
        if (impl_ == nullptr)
            return;

        impl_->diagnosticsEnabled = enabled;
        impl_->diagnosticsPredictionEnabled = predictionEnabled;
        impl_->diagnosticsRemoteSmoothingEnabled = remoteSmoothingEnabled;
    }

    ClientDiagnostics& NetClient::clientDiagnostics()
    {
        return impl_->diagnostics;
    }

    const ClientDiagnostics& NetClient::clientDiagnostics() const
    {
        return impl_->diagnostics;
    }

    void NetClient::updateLiveTransportStats(const uint32_t rttMs,
                                             const uint32_t rttVarianceMs,
                                             const uint32_t lossPermille,
                                             const uint32_t lastSnapshotTick,
                                             const uint32_t lastCorrectionTick,
                                             const uint32_t snapshotAgeMs,
                                             const uint32_t gameplaySilenceMs)
    {
        if (impl_ == nullptr)
            return;

        impl_->liveStats.rttMs = rttMs;
        impl_->liveStats.rttVarianceMs = rttVarianceMs;
        impl_->liveStats.lossPermille = lossPermille;
        impl_->liveStats.lastSnapshotTick = lastSnapshotTick;
        impl_->liveStats.lastCorrectionTick = lastCorrectionTick;
        impl_->liveStats.snapshotAgeMs = snapshotAgeMs;
        impl_->liveStats.gameplaySilenceMs = gameplaySilenceMs;
    }

    void NetClient::updateLivePredictionStats(const bool predictionActive,
                                              const bool recoveryActive,
                                              const uint32_t correctionCount,
                                              const uint32_t mismatchCount,
                                              const uint32_t lastCorrectionDeltaQ,
                                              const uint32_t maxPendingInputDepth)
    {
        if (impl_ == nullptr)
            return;

        impl_->liveStats.predictionActive = predictionActive;
        impl_->liveStats.recoveryActive = recoveryActive;
        impl_->liveStats.correctionCount = correctionCount;
        impl_->liveStats.mismatchCount = mismatchCount;
        impl_->liveStats.lastCorrectionDeltaQ = lastCorrectionDeltaQ;
        impl_->liveStats.maxPendingInputDepth = maxPendingInputDepth;
    }

    const NetClient::ClientLiveStats& NetClient::liveStats() const
    {
        return impl_->liveStats;
    }
} // namespace bomberman::net
