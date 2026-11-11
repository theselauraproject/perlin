#pragma once
// ore placement from the Bedrock feature rules: for each rule, a number of iterations at a random place in each chunk, and at each one a blob of ore that
// replaces the blocks the feature names. the rules, counts, distributions and biome filters are Bedrock's data; the blob shape is the classic
// capsule-chain one, which is an approximation (Bedrock's own ore code is not published), and the random numbers are our own
#include "world/vanilla/FeatureData.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace perlin::server::vanilla {

    class VanillaFeatures {
    public:
        explicit VanillaFeatures(int64_t seed);

        // blocks of a chunk, [(x * 16 + z) * kHeight + y], with one biome index per column [x * 16 + z]. every blob that starts in this chunk or a neighbour
        // and reaches into this chunk is placed; the blob's random numbers do not depend on the chunk being built, so neighbours agree on the seams.
        // a rule's biome filter is tested at the blob's start column, or at the nearest column of this chunk when the start is outside it
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
