#pragma once

#include <cstdint>
#include <string>

namespace perlin::ui {
    struct EffectInfo {
        const char* texture = nullptr;
        const char* name = nullptr;
        bool harmful = false;
        uint32_t color = 0xFFFFFF;
    };

    struct HudEffect {
        int id = 0;
        int amplifier = 0;
        int ticks = 0;
        bool ambient = false;
        bool operator==(const HudEffect& o) const {
            return id == o.id && amplifier == o.amplifier && ticks == o.ticks && ambient == o.ambient;
        }
    };

    bool GetEffectInfo(int id, EffectInfo& out);
    std::string EffectLabel(const HudEffect& effect);
    std::string EffectDuration(const HudEffect& effect);
}
