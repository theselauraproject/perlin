#pragma once
#include "world/level/Level.hpp"
#include "world/level/lighting/ChunkLighting.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace perlin {
    class LightBandCache {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        explicit LightBandCache(const LightRegistry& registry) : registry(registry) {}

        uint8_t At(Level& level, int bx, int by, int bz, TimePoint now);

    private:
        struct Band {
            ChunkLighting::Result lit;
            TimePoint at;
            uint64_t counter = 0;
            uint64_t area = 0;
        };

        const LightRegistry& registry;
        std::unordered_map<uint64_t, Band> bands;
        const Level* owner = nullptr;
        TimePoint budgetFrame{};
        int budget = 0;
    };
}
