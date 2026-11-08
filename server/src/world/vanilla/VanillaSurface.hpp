#pragma once

#include "world/Chunk.hpp"
#include "world/vanilla/BiomeData.hpp"
#include "world/vanilla/DensityFunction.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace perlin::server::vanilla {

    constexpr int kSeaLevel = 63;

    constexpr int kMidLayers = 3;

    constexpr uint32_t kMaxClimateDistance = 20000;

    int FindBiome(const int32_t target[6]);
    int FindBiomePoint(const int32_t target[6], int hint);
    int FindBiomeLinear(const int32_t target[6]);

    class VanillaClimate {
    public:

        explicit VanillaClimate(DensityGraph& graph);
        bool Ok() const { return ok; }

        void Sample(int x, int y, int z, int32_t out[6]) const;
        void SampleColumn(int x, int z, const int* ys, int count, int32_t* out) const;

        const std::array<DfPtr, 6>& Roots() const { return roots; }

    private:
        std::array<DfPtr, 6> roots;
        bool ok = false;
    };

    struct TerrainStages {
        const struct AquiferFunctions* aquifers = nullptr;
        const class VanillaMaterials* materials = nullptr;
        const class VanillaCarvers* carvers = nullptr;
        std::function<float(int, int, int, int)> temperature;
        std::function<int(int, int, int)> biome;
    };

    struct SurfaceMaterial {
        uint16_t top, mid, foundation, seaFloor, sea;
        int32_t seaFloorDepth;
        int32_t builder;
    };

    class VanillaSurface {
    public:
        VanillaSurface();
        const std::vector<SurfaceMaterial>& Materials() const { return materials; }
        uint16_t Stone() const { return stone; }
        uint16_t Deepslate() const { return deepslate; }
        uint16_t Bedrock() const { return bedrock; }
        uint16_t Dirt() const { return dirt; }
        uint16_t GrassBlock() const { return grass; }

        static int TopOf(const float* columnDensity);

        uint16_t BlockAt(const SurfaceMaterial& m, int idx, int top, bool solid) const;

        void Fill(const float* density, const uint8_t* biomes, ChunkColumn& column,
                  const class VanillaFeatures* features = nullptr, int chunkX = 0, int chunkZ = 0,
                  const struct TerrainStages* stages = nullptr) const;

        static void Store(const uint16_t* blocks, ChunkColumn& column);
        static void StoreBiomes(const uint8_t* biomes, ChunkColumn& column);

    private:
        std::vector<SurfaceMaterial> materials;
        uint16_t stone, deepslate, bedrock, dirt, grass, water, lava;
    };

    struct BiomeSite {
        int x, y, z;
    };
    BiomeSite SiteOf(int worldX, int worldZ, int top);
    uint8_t NetworkBiome(int tableIndex);
}
