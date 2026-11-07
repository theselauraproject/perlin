#pragma once
#include <cstdint>

namespace perlin::server {

    namespace mobflag {
        constexpr uint32_t Hostile = 1, Panic = 2, Fly = 4, Aquatic = 8, Burns = 16, Explodes = 32, Ranged = 64,
                           Neutral = 128, BabyOk = 256, Spider = 512;
    }

    struct MobDef {
        const char* id;
        float width, height, health, speed, damage;
        uint32_t flags;
        const char* tempt;
        const char* drops;
        const char* families;
    };

    extern const MobDef kMobDefs[];
    extern const int kMobDefCount;

    const MobDef* FindMob(const char* identifier);
}
