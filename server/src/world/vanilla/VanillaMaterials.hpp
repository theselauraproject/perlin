#pragma once
#include "world/vanilla/DensityFunction.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

    class VanillaMaterials {
    public:
        using Temperature = std::function<float(int biome, int x, int y, int z)>;
        using BiomeAt = std::function<int(int x, int y, int z)>;

        static std::unique_ptr<VanillaMaterials> Load(const DataPack& pack, DensityGraph& graph, int seaLevel);
        ~VanillaMaterials();

        void Apply(int chunkX, int chunkZ, const uint8_t* biomes, uint16_t* blocks, const Temperature& temperature,
                   const BiomeAt& biomeAt) const;
        bool TopMaterial(int chunkX, int chunkZ, int x, int z, int y, const uint8_t* biomes, const uint16_t* blocks,
                         bool underFluid, const Temperature& temperature, const BiomeAt& biomeAt, uint16_t& out) const;

        uint16_t Stone() const { return stone; }

        struct Cond;
        struct Rule;

    private:
        VanillaMaterials();
        int RuleFrom(const json::Value& v, int depth);
        int CondFrom(const json::Value& v, int depth);
        uint16_t BlockOf(const std::string& javaName);

        const DataPack* pack = nullptr;
        DensityGraph* graph = nullptr;
        std::vector<Rule> rules;
        std::vector<Cond> conds;
        int root = -1;
        int seaLevel = 63;
        uint16_t stone = 0, water = 0, lava = 0, packedIce = 0, snowBlock = 0;
        std::array<uint16_t, 192> bands{};
        Xoroshiro::Factory noiseRandom;
        Xoroshiro::Factory oreRandom;
        const NormalNoise* surfaceNoise = nullptr;
        const NormalNoise* surfaceSecondary = nullptr;
        const NormalNoise* bandOffset = nullptr;
        const NormalNoise* badlandsPillar = nullptr;
        const NormalNoise* badlandsPillarRoof = nullptr;
        const NormalNoise* badlandsSurface = nullptr;
        const NormalNoise* icebergPillar = nullptr;
        const NormalNoise* icebergPillarRoof = nullptr;
        const NormalNoise* icebergSurface = nullptr;
        DfPtr preliminary;
        int erodedBadlands = -1, frozenOcean = -1, deepFrozenOcean = -1;

        friend struct MaterialColumn;
    };
}
