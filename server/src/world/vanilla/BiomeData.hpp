#pragma once

#include <cstdint>

namespace perlin::server::vanilla {

    struct BiomeMaterials {
        const char* java;
        const char* bedrock;
        const char* top;
        const char* mid;
        const char* foundation;
        const char* seaFloor;
        const char* sea;
        int seaFloorDepth;

        int builder;
    };

    constexpr int kBiomePointStride = 13;
    extern const int kBiomePointCount;
    extern const int16_t kBiomePointData[];
    extern const uint8_t kBiomePointBiome[];
    extern const int kBiomeCount;
    extern const BiomeMaterials kBiomes[];
}
