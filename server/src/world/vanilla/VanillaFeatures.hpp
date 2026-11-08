#pragma once

#include "world/vanilla/FeatureData.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace perlin::server::vanilla {

    class VanillaFeatures {
    public:
        explicit VanillaFeatures(int64_t seed);

        void Apply(int chunkX, int chunkZ, const uint8_t* biomes, uint16_t* blocks) const;

        size_t RuleCount() const { return rules.size(); }

    private:
        struct Replace {
            uint16_t place, may0, may1;
        };
        struct Feature {
            int count;
            float discard;
            std::vector<Replace> replace;
        };
        struct Rule {
            const OreRuleDef* def;
            const Feature* feature;
            uint64_t key;
        };
        int64_t seed;
        std::vector<Feature> features;
        std::vector<Rule> rules;
    };
}
