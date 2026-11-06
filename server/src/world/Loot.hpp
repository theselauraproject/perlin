#pragma once
#include "world/Blocks.hpp"

#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace perlin::server {

    struct LootSource {
        const char* key;
        const char* json;
    };

    extern const LootSource kLootSources[];
    extern const int kLootSourceCount;

    struct LootDrop {
        std::string item;
        int count = 0;
    };

    struct LootBlock {
        std::string java;
        std::string bedrock;
        StateList states;
    };

    struct LootContext {
        std::mt19937* rng = nullptr;
        std::string tool;
        int silkTouch = 0;
        int fortune = 0;
        int looting = 0;
        float explosionRadius = 0;
        bool killedByPlayer = false;
        bool onFire = false;
        bool baby = false;
        int cubeSize = 1;
        std::string attackerType;
        std::string attackerEntity;
        LootBlock block;
        std::function<LootBlock(int dx, int dy, int dz)> neighbor;
    };

    class LootTables {
    public:
        static LootTables& Instance();

        bool Has(const std::string& key) const;
        std::vector<LootDrop> Roll(const std::string& key, LootContext& ctx) const;

        static LootBlock ViewOf(const std::string& bedrockName, const StateList& states);
        static std::string JavaEntityName(const std::string& bedrockId);
        static std::string BedrockItemName(const std::string& javaName);
        static bool CanHarvest(const std::string& bedrockBlock, const std::string& toolName);
        static int ToolTier(const std::string& toolName);
        static int BlockXp(const std::string& javaBlock, int silkTouch, std::mt19937& rng);
        static int MobXp(const std::string& bedrockId, bool baby, bool hostile, std::mt19937& rng);
        static float Hardness(const std::string& bedrockBlock);

    private:
        LootTables();
        struct Impl;
        Impl* impl;
    };
}
