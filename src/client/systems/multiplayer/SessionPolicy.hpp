#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace perlin::session {

    enum class BatchCompression : uint8_t { Zlib = 0x00, Snappy = 0x01, None = 0xFF };

    BatchCompression SelectBatchCompression(uint16_t algorithm, uint16_t threshold, size_t size);

    enum class PlayStatusKind { LoginSuccess, FailedClient, FailedSpawn, PlayerSpawn, Other };

    enum class Phase { WaitingPlayStatus, InGame, Other };

    enum class OutdatedSide { None, Client, Server };

    struct PlayStatusDecision {
        enum class Action { Ignore, Fail, SendCacheStatus, EnterGame };

        Action action = Action::Ignore;
        std::string error;
        OutdatedSide outdated = OutdatedSide::None;
    };

    PlayStatusDecision DecidePlayStatus(PlayStatusKind status, int statusCode, Phase phase,
                                        bool cacheStatusSent);

    std::string DescribeDisconnect(int reason, const std::string& message);

}
