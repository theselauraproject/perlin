#pragma once

#include "util/Strings.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::physics::bds {
    struct StatusEffectValue {
        int32_t amplifier = 0;
        int32_t duration = 0;
        bool ambient = false;
        bool particles = true;

        bool operator==(const StatusEffectValue&) const = default;
    };

    // Actor::addEffect and MobEffectsReplay merge an existing effect. A weaker
    // update is not saved to become active after the stronger effect expires.
    constexpr StatusEffectValue MergeStatusEffect(StatusEffectValue current,
                                                   const StatusEffectValue& incoming) {
        const bool replace = incoming.amplifier > current.amplifier ||
                             (incoming.amplifier == current.amplifier && current.duration != -1 &&
                              (incoming.duration == -1 || incoming.duration > current.duration));
        if (replace) {
            current.amplifier = incoming.amplifier;
            current.duration = incoming.duration;
            current.ambient = incoming.ambient;
        } else if (!incoming.ambient) {
            current.ambient = false;
        }
        current.particles = incoming.particles;
        return current;
    }

    constexpr bool ReplaysStatusEffectAt(uint64_t appliedTick, uint64_t recordTick) {
        return appliedTick != 0 && recordTick > appliedTick;
    }

    inline int32_t StatusEffectTicksAt(int32_t duration, uint64_t appliedTick, uint64_t movementTick) {
        if (duration == -1)
            return -1;
        if (duration <= 0)
            return 0;
        const uint64_t elapsed = movementTick > appliedTick ? movementTick - appliedTick : 0;
        if (elapsed >= static_cast<uint64_t>(duration))
            return 0;
        return duration - static_cast<int32_t>(elapsed);
    }

    // The game puts these on the movement attribute itself when the effect is applied, so
    // they hold even when a server's attribute update carries only the final value.
    struct EffectSpeedModifier {
        int32_t effectId = 0;
        std::string_view id;
        std::string_view name;
        float amountPerLevel = 0;
    };

    inline constexpr std::array kEffectSpeedModifiers{
        EffectSpeedModifier{1, "91aeaa56-376b-4498-935b-2f7f68070635", "MovementSpeed", 0.2f},
        EffectSpeedModifier{2, "7107de5e-7ce8-4030-940e-514c1f160890", "MovementSlowdown", -0.15f},
    };

    // level is the amplifier plus one, or 0 without the effect. A modifier the server sent
    // is left as it is; owned marks the one added here. Returns whether the list changed.
    template <class Modifier>
    bool SyncEffectSpeedModifier(std::vector<Modifier>& modifiers, bool& owned,
                                 const EffectSpeedModifier& effect, int32_t level) {
        const auto found = std::ranges::find_if(modifiers, [&](const Modifier& m) {
            return std::ranges::equal(m.id, effect.id, {}, str::AsciiLower, str::AsciiLower);
        });
        if (level <= 0) {
            const bool remove = owned && found != modifiers.end();
            owned = false;
            if (remove)
                modifiers.erase(found);
            return remove;
        }
        const float amount = effect.amountPerLevel * static_cast<float>(level);
        if (found == modifiers.end()) {
            Modifier added{};
            added.id = std::string(effect.id);
            added.name = std::string(effect.name);
            added.amount = amount;
            added.operation = 2;
            added.operand = 2;
            added.serializable = false;
            modifiers.push_back(std::move(added));
            owned = true;
            return true;
        }
        if (!owned || found->amount == amount)
            return false;
        found->amount = amount;
        return true;
    }
}
