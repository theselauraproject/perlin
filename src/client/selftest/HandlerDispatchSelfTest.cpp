#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"
#include "network/Stream.hpp"
#include "client/selftest/SelfTests.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            failures++;
        }
    }

    template <class P> void Inject(perlin::BedrockSession& s, const P& packet) {
        s.InjectIncoming(P::kId, perlin::net::Encode(packet));
    }
}

namespace perlin::selftest {
int HandlerDispatch() {
    {
        perlin::proto::MetadataDictionaryEntry e;
        e.value.emplace<1>(7);
        double d = 0;
        Check(perlin::handlers::MetaNumber(e, d) && d == 7.0, "MetaNumber reads integer metadata");
        Check(perlin::handlers::SaneWorldPos(1, 2, 3), "sane position accepted");
        Check(!perlin::handlers::SaneWorldPos(1e9f, 0, 0), "huge position rejected");
        Check(!perlin::handlers::SaneWorldPos(0, std::nanf(""), 0), "nan rejected");
        Check(perlin::handlers::EqualsIgnoreCase("AbC", "aBc"), "case-insensitive equal");
        Check(!perlin::handlers::EqualsIgnoreCase("abc", "abd"), "different strings unequal");
        Check(!perlin::handlers::EqualsIgnoreCase("ab", "abc"), "different lengths unequal");
        perlin::proto::Uuid u{};
        u.bytes[0] = 0xab;
        u.bytes[15] = 0x01;
        const std::string key = perlin::handlers::UuidKey(u);
        Check(key.size() == 32 && key.substr(0, 2) == "ab" && key.substr(30) == "01", "uuid key format");
    }
    {
        perlin::BedrockSession session;
        Check(session.World().difficulty == 2, "difficulty default");
        perlin::proto::SetDifficultyPacket diff;
        diff.difficulty = 3;
        Inject(session, diff);
        Check(session.World().difficulty == 3, "level handler: set_difficulty applied");

        perlin::proto::SetPlayerGameTypePacket gt;
        gt.gamemode = static_cast<decltype(gt.gamemode)>(1);
        Inject(session, gt);
        Check(session.GameType() == 1, "player-rule handler: set_player_game_type applied");

        perlin::proto::SetTimePacket t;
        t.time = 6000;
        Inject(session, t);
        int64_t day = 0;
        int ticks = 0;
        Check(session.GetWorldTime(day, ticks) && day == 0 && ticks >= 6000 && ticks < 6100,
              "world-state handler: set_time applied");

        session.InjectIncoming(perlin::proto::SetDifficultyPacket::kId, {});
        Check(session.World().difficulty == 3, "truncated packet leaves state unchanged");
        session.InjectIncoming(0x7fffffff, {1, 2, 3});
        Check(session.World().difficulty == 3, "unknown packet id ignored");

        perlin::proto::SetDifficultyPacket again;
        again.difficulty = 0;
        Inject(session, again);
        Check(session.World().difficulty == 0, "later packet supersedes earlier state");
    }
    {
        perlin::BedrockSession session;
        perlin::BedrockSession::ServerMotion motion;
        perlin::proto::MovePlayerPacket move;
        move.runtimeId = 7;
        move.position = {120.5f, 64.0f + 1.62f, -30.5f};
        move.yaw = 90.0f;
        move.mode = perlin::proto::MovePlayerPacketMode::Normal;
        Inject(session, move);
        Check(session.TakeServerMotion(motion) && motion.teleport && motion.pos[0] == 120.5 &&
                  std::abs(motion.pos[1] - 64.0) < 1e-4 && motion.pos[2] == -30.5 && !motion.filterTravel,
              "normal move_player places the local player");

        move.mode = perlin::proto::MovePlayerPacketMode::Rotation;
        move.position = {0.0f, 80.0f, 0.0f};
        Inject(session, move);
        Check(!session.TakeServerMotion(motion), "rotation-only move_player leaves the position alone");

        move.runtimeId = 8;
        move.mode = perlin::proto::MovePlayerPacketMode::Normal;
        Inject(session, move);
        Check(!session.TakeServerMotion(motion), "another player's move_player does not move us");
    }
    if (failures == 0)
        std::puts("handler_dispatch_test: ok");
    return failures == 0 ? 0 : 1;
}
}
