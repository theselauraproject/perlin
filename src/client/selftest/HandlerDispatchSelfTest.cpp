#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"
#include "network/Stream.hpp"
#include "client/selftest/SelfTests.hpp"

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
        // a server that sends only the movement value, with no modifier list (PocketMine and most custom cores)
        perlin::BedrockSession session;
        perlin::proto::UpdateAttributesPacket attrs;
        attrs.runtimeEntityId = 0;
        perlin::proto::PlayerAttributesEntry movement;
        movement.name = "minecraft:movement";
        movement.min = 0.0f;
        movement.max = 3.4028235e38f;
        movement.current = movement.default_ = 0.1f;
        movement.defaultMax = 3.4028235e38f;
        movement.defaultMin = 0.0f;
        attrs.attributes.push_back(movement);
        Inject(session, attrs);
        const float walk = session.PredictedMovementSpeed(false);
        const float sprint = session.PredictedMovementSpeed(true);

        perlin::proto::MobEffectPacket speed;
        speed.runtimeEntityId = 0;
        speed.eventId = perlin::proto::MobEffectPacketEventId::Add;
        speed.effectId = 1;
        speed.amplifier = 1;
        speed.duration = 600;
        Inject(session, speed);
        const float speedWalk = session.PredictedMovementSpeed(false);
        const float speedSprint = session.PredictedMovementSpeed(true);
        Check(std::abs(speedWalk - walk * 1.4f) < 1e-5f, "speed II adds 40% to walking");
        Check(speedSprint > sprint * 1.3f, "speed II survives sprinting");

        speed.eventId = perlin::proto::MobEffectPacketEventId::Remove;
        Inject(session, speed);
        Check(std::abs(session.PredictedMovementSpeed(false) - walk) < 1e-6f, "removing speed restores the walk speed");

        perlin::proto::MobEffectPacket slow = speed;
        slow.eventId = perlin::proto::MobEffectPacketEventId::Add;
        slow.effectId = 2;
        slow.amplifier = 0;
        Inject(session, slow);
        Check(std::abs(session.PredictedMovementSpeed(false) - walk * 0.85f) < 1e-5f, "slowness I takes 15% off");
    }
    if (failures == 0)
        std::puts("handler_dispatch_test: ok");
    return failures == 0 ? 0 : 1;
}
}
