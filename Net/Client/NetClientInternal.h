#ifndef BOMBERMAN_NET_NETCLIENTINTERNAL_H
#define BOMBERMAN_NET_NETCLIENTINTERNAL_H

#include "Net/Client/NetClient.h"

#include "Net/ClientDiagnostics.h"
#include "Net/PacketDispatch.h"

#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <enet/enet.h>

namespace bomberman::net::net_client_internal
{
    constexpr int kConnectTimeoutMs = 5000;
    constexpr int kDisconnectTimeoutMs = 5000;
    constexpr int kDisconnectPollTimeoutMs = 100;
    // Preserve the previous combined backlog headroom (64 per event type)
    // while moving both reliable gameplay types into one ordered queue.
    constexpr std::size_t kMaxPendingGameplayEvents = 128;

    using SteadyClock = std::chrono::steady_clock;
    using TimePoint = SteadyClock::time_point;

    inline int elapsedMs(const TimePoint& since)
    {
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - since).count());
    }

    inline uint32_t elapsedSinceOrZero(const TimePoint& latest, const TimePoint& fallback)
    {
        const TimePoint since = latest != TimePoint{} ? latest : fallback;
        if (since == TimePoint{})
        {
            return 0;
        }

        return static_cast<uint32_t>(elapsedMs(since));
    }

    inline std::string currentLocalTimeTagForFilename()
    {
        const auto now = std::chrono::system_clock::now();
        const auto nowTimeT = std::chrono::system_clock::to_time_t(now);

        std::tm localTm{};
#if defined(_WIN32)
        localtime_s(&localTm, &nowTimeT);
#else
        localtime_r(&nowTimeT, &localTm);
#endif

        std::ostringstream out;
        out << std::put_time(&localTm, "%H%M%S");
        return out.str();
    }

    inline std::string makeUniqueJsonReportPath(const std::string_view basePathWithoutExtension)
    {
        std::string candidate = std::string(basePathWithoutExtension) + ".json";
        if (!std::filesystem::exists(candidate))
        {
            return candidate;
        }

        for (uint32_t suffix = 2; suffix < 1000; ++suffix)
        {
            candidate = std::string(basePathWithoutExtension) + "_" + std::to_string(suffix) + ".json";
            if (!std::filesystem::exists(candidate))
            {
                return candidate;
            }
        }

        return std::string(basePathWithoutExtension) + "_overflow.json";
    }

    constexpr bool isHandshakeControlMessage(EMsgType type)
    {
        using enum EMsgType;

        switch (type)
        {
            case Welcome:
            case Reject:
                return true;
            case Hello:
            case Input:
            case Snapshot:
            case Correction:
            case BombPlaced:
            case ExplosionResolved:
            case LobbyState:
            case LevelInfo:
            case MatchLoaded:
            case MatchStart:
            case MatchCancelled:
            case MatchResult:
            default:
                return false;
        }
    }
} // namespace bomberman::net::net_client_internal

namespace bomberman::net
{
    using net_client_internal::SteadyClock;
    using net_client_internal::TimePoint;

    struct NetClient::Impl
    {
        ENetHost* host = nullptr;
        ENetPeer* peer = nullptr;
        PacketDispatcher<NetClient> dispatcher;

        // Store the next seq as first-valid minus one so the first sent input is exactly kFirstInputSeq.
        uint32_t nextInputSeq = kFirstInputSeq - 1u;
        uint8_t inputHistory[kMaxInputBatchSize]{};

        // Async connect and handshake state. Cleared by resetSessionState().
        std::string pendingPlayerName;
        TimePoint connectStartTime;
        TimePoint handshakeStartTime;
        TimePoint disconnectStartTime;
        TimePoint connectedStartTime;
        TimePoint lastLobbyStateReceiveTime;
        TimePoint lastGameplayReceiveTime;
        TimePoint lastSnapshotReceiveTime;
        TimePoint lastCorrectionReceiveTime;
        TimePoint lastTransportSampleTime;
        TimePoint lastInputSendTime;

        ClientDiagnostics diagnostics{};
        ClientLiveStats liveStats{};
        bool diagnosticsEnabled = false;
        bool diagnosticsSessionActive = false;
        bool diagnosticsPredictionEnabled = true;
        bool diagnosticsRemoteSmoothingEnabled = true;

        std::optional<MsgLobbyState> cachedLobbyState{};

        struct MatchFlowState
        {
            MsgLevelInfo levelInfo{};
            bool hasLevelInfo = false;
            bool levelInfoPending = false;
            bool brokenGameplayEventStream = false;
            std::optional<MsgMatchStart> matchStart{};
            std::optional<uint32_t> cancelledMatchId{};
            std::optional<MsgMatchResult> matchResult{};
            std::optional<MsgSnapshot> snapshot{};
            std::optional<MsgCorrection> correction{};
            std::deque<GameplayEvent> pendingGameplayEvents{};

            void resetRuntimeState()
            {
                snapshot = {};
                correction = {};
                matchStart.reset();
                cancelledMatchId.reset();
                matchResult.reset();
                brokenGameplayEventStream = false;
                pendingGameplayEvents.clear();
            }

            void beginBootstrap(const MsgLevelInfo& newLevelInfo)
            {
                resetRuntimeState();
                levelInfo = newLevelInfo;
                hasLevelInfo = true;
                levelInfoPending = true;
            }

            [[nodiscard]]
            bool isActiveMatch(const uint32_t matchId) const
            {
                return hasLevelInfo && levelInfo.matchId == matchId;
            }

            [[nodiscard]]
            uint32_t currentMatchId() const
            {
                return hasLevelInfo ? levelInfo.matchId : 0u;
            }

            void reset()
            {
                resetRuntimeState();
                levelInfo = {};
                hasLevelInfo = false;
                levelInfoPending = false;
            }
        };
        MatchFlowState matchFlow{};

        static void onWelcome(NetClient& client,
                              const PacketHeader& /*header*/,
                              const uint8_t* payload,
                              std::size_t payloadSize)
        {
            client.handleWelcome(payload, payloadSize);
        }

        static void onReject(NetClient& client,
                             const PacketHeader& /*header*/,
                             const uint8_t* payload,
                             std::size_t payloadSize)
        {
            client.handleReject(payload, payloadSize);
        }

        static void onLevelInfo(NetClient& client,
                                const PacketHeader& /*header*/,
                                const uint8_t* payload,
                                std::size_t payloadSize)
        {
            client.handleLevelInfo(payload, payloadSize);
        }

        static void onLobbyState(NetClient& client,
                                 const PacketHeader& /*header*/,
                                 const uint8_t* payload,
                                 std::size_t payloadSize)
        {
            client.handleLobbyState(payload, payloadSize);
        }

        static void onMatchStart(NetClient& client,
                                 const PacketHeader& /*header*/,
                                 const uint8_t* payload,
                                 std::size_t payloadSize)
        {
            client.handleMatchStart(payload, payloadSize);
        }

        static void onMatchCancelled(NetClient& client,
                                     const PacketHeader& /*header*/,
                                     const uint8_t* payload,
                                     std::size_t payloadSize)
        {
            client.handleMatchCancelled(payload, payloadSize);
        }

        static void onMatchResult(NetClient& client,
                                  const PacketHeader& /*header*/,
                                  const uint8_t* payload,
                                  std::size_t payloadSize)
        {
            client.handleMatchResult(payload, payloadSize);
        }

        static void onSnapshot(NetClient& client,
                               const PacketHeader& /*header*/,
                               const uint8_t* payload,
                               std::size_t payloadSize)
        {
            client.handleSnapshot(payload, payloadSize);
        }

        static void onCorrection(NetClient& client,
                                 const PacketHeader& /*header*/,
                                 const uint8_t* payload,
                                 std::size_t payloadSize)
        {
            client.handleCorrection(payload, payloadSize);
        }

        static void onBombPlaced(NetClient& client,
                                 const PacketHeader& /*header*/,
                                 const uint8_t* payload,
                                 std::size_t payloadSize)
        {
            client.handleBombPlaced(payload, payloadSize);
        }

        static void onExplosionResolved(NetClient& client,
                                        const PacketHeader& /*header*/,
                                        const uint8_t* payload,
                                        std::size_t payloadSize)
        {
            client.handleExplosionResolved(payload, payloadSize);
        }
    };
} // namespace bomberman::net

#endif // BOMBERMAN_NET_NETCLIENTINTERNAL_H
