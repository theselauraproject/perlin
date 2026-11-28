#include "protocol/Packets.hpp"
#include "world/movement/PerlinStatusEffect.hpp"
#include "world/movement/PlayerPhysics.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
    using Modifier = perlin::proto::PlayerAttributesEntryModifiersEntry;
    using perlin::physics::bds::kEffectSpeedModifiers;
    using perlin::physics::bds::SyncEffectSpeedModifier;

    int failures = 0;

    void Check(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }

    bool Near(float a, float b) {
        return std::fabs(a - b) < 1e-6f;
    }

    Modifier Sprint() {
        Modifier m;
        m.id = "d208fc00-42aa-4aad-9276-d5446530de43";
        m.name = "Sprinting speed boost";
        m.amount = 0.3f;
        m.operation = 2;
        m.operand = 2;
        return m;
    }

    float Speed(const std::vector<Modifier>& modifiers, float current = 0.1f) {
        std::vector<perlin::physics::AttributeModifierValue> values;
        for (const auto& m : modifiers)
            values.push_back({m.amount, m.operation, m.operand});
        std::array<float, 2> limits{};
        return perlin::physics::RecalculateAttribute({0.0f, 3.4028235e38f, 0.1f}, values, current, limits);
    }

    // A server that sends only the final movement value (PocketMine and most custom cores):
    // the modifier list is empty, so the effect's modifier exists only on the client.
    void ServerWithoutModifiers() {
        const auto& speed = kEffectSpeedModifiers[0];
        std::vector<Modifier> modifiers;
        bool owned = false;
        Check(SyncEffectSpeedModifier(modifiers, owned, speed, 2) && owned && modifiers.size() == 1,
              "speed II adds its modifier when the server sent none");
        Check(modifiers[0].operation == 2 && modifiers[0].operand == 2 && Near(modifiers[0].amount, 0.4f),
              "speed II multiplies the total by 1.4");
        Check(Near(Speed(modifiers), 0.14f), "speed II walks 40% faster");
        modifiers.push_back(Sprint());
        Check(Near(Speed(modifiers), 0.1f * 1.4f * 1.3f), "speed II survives sprinting");

        Check(!SyncEffectSpeedModifier(modifiers, owned, speed, 2), "an unchanged level changes nothing");
        Check(SyncEffectSpeedModifier(modifiers, owned, speed, 1) && Near(modifiers[0].amount, 0.2f),
              "a new level updates the added modifier");
        Check(SyncEffectSpeedModifier(modifiers, owned, speed, 0) && !owned && modifiers.size() == 1 &&
                  Near(Speed(modifiers), 0.13f),
              "removing the effect removes only its modifier");

        const auto& slowness = kEffectSpeedModifiers[1];
        modifiers.clear();
        Check(SyncEffectSpeedModifier(modifiers, owned, slowness, 1) && Near(Speed(modifiers), 0.085f),
              "slowness I takes 15% off");
    }

    // BDS sends the effect's modifier itself: it is used as sent and never removed here.
    void ServerWithModifier() {
        const auto& speed = kEffectSpeedModifiers[0];
        Modifier sent;
        sent.id = "91AEAA56-376B-4498-935B-2F7F68070635";
        sent.name = "MovementSpeed";
        sent.amount = 0.2f;
        sent.operation = 2;
        sent.operand = 2;
        sent.serializable = true;
        std::vector<Modifier> modifiers{sent};
        bool owned = false;
        Check(!SyncEffectSpeedModifier(modifiers, owned, speed, 1) && !owned && modifiers.size() == 1,
              "a server-sent modifier is not duplicated, whatever the case of its id");
        Check(!SyncEffectSpeedModifier(modifiers, owned, speed, 2) && Near(modifiers[0].amount, 0.2f),
              "a server-sent modifier keeps the server's amount");
        Check(!SyncEffectSpeedModifier(modifiers, owned, speed, 0) && modifiers.size() == 1,
              "a server-sent modifier is left for the server to remove");
    }
}

int main() {
    ServerWithoutModifiers();
    ServerWithModifier();
    if (failures == 0)
        std::puts("movement_speed_effect_selftest: ok");
    return failures == 0 ? 0 : 1;
}
