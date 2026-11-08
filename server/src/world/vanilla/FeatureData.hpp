#pragma once

#include <cstdint>

namespace perlin::server::vanilla {

    struct OreReplaceDef {
        const char* place;
        const char* mayReplace0;
        const char* mayReplace1;
    };

    struct OreFeatureDef {
        const char* name;
        int count;
        float discardChanceOnAirExposure;
        int replaceCount;
        OreReplaceDef replace[2];
    };

    struct DistDef {
        int kind;
        int lo;
        int hi;
    };

    struct OreRuleDef {
        const char* name;
        int feature;

        int pass;
        int iterations;
        DistDef x, y, z;

        const char* order;
        int scatterNumerator, scatterDenominator;

        uint64_t biomeMask[2];
    };

    extern const int kOreFeatureCount;
    extern const OreFeatureDef kOreFeatures[];
    extern const int kOreRuleCount;
    extern const OreRuleDef kOreRules[];
}
