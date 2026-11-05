#pragma once
// the stages after density: which biome a column is in (climate sampled where the ground is, then the nearest box of Java's placement table), and the
// blocks that follow (the biome's Bedrock surface materials from bedrock-samples). every stage has a cpu version here and a gpu version in
// gpu/shaders/terrain_vanilla.slang that must give the same answers; this file is the reference
#include "world/Chunk.hpp"
#include "world/vanilla/BiomeData.hpp"
#include "world/vanilla/DensityFunction.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace perlin::server::vanilla {

    constexpr int kSeaLevel = 63;
    // top layer 1 block, then this many of the biome's mid material, then the foundation
    constexpr int kMidLayers = 3;
    // a distance this far or more counts as this far, so squared sums fit in 32 bits
    constexpr uint32_t kMaxClimateDistance = 20000;

    // Java's climate lookup, as a plain nearest-box search: the box with the least squared distance to the sample (+ offset squared), the first one on a tie.
    // Java's own r-tree resolves ties by what it found last on that thread, so ties cannot be matched exactly; they need a sample exactly on a shared edge
    // target: temperature, humidity, continentalness, erosion, depth, weirdness, each as climate * 10000
    int FindBiome(const int32_t target[6]);
    int FindBiomePoint(const int32_t target[6], int hint);
    int FindBiomeLinear(const int32_t target[6]);

    // the six climate functions of the overworld router, sampled where the biome is wanted
    class VanillaClimate {
    public:
        // null graph members when the data lacks a function
        explicit VanillaClimate(DensityGraph& graph);
        bool Ok() const { return ok; }
        // climate * 10000 truncated, as Climate.quantizeCoord does
        void Sample(int x, int y, int z, int32_t out[6]) const;
        void SampleColumn(int x, int z, const int* ys, int count, int32_t* out) const;
        // temperature, vegetation, continents, erosion, depth, ridges
        const std::array<DfPtr, 6>& Roots() const { return roots; }

    private:
        std::array<DfPtr, 6> roots;
        bool ok = false;
    };

    // the block that goes where: ids resolved once from the materials' names
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

        // highest solid block of one column of densities (index from the floor), or -1
        static int TopOf(const float* columnDensity);
        // the block at one place; `top` is the column's top index, `idx` this block's index from the floor
        uint16_t BlockAt(const SurfaceMaterial& m, int idx, int top, bool solid) const;

        // blocks of a chunk from [(x * 16 + z) * kHeight + y] densities and one biome per column [x * 16 + z]
        // `features`, when given, places its ores into the blocks of chunk (chunkX, chunkZ) before they are stored
        void Fill(const float* density, const uint8_t* biomes, ChunkColumn& column,
                  const class VanillaFeatures* features = nullptr, int chunkX = 0, int chunkZ = 0,
                  const struct TerrainStages* stages = nullptr) const;
        // block ids already laid out [(x * 16 + z) * kHeight + y] into the chunk's sections (what the gpu surface stage returns)
        static void Store(const uint16_t* blocks, ChunkColumn& column);
        static void StoreBiomes(const uint8_t* biomes, ChunkColumn& column);

    private:
        std::vector<SurfaceMaterial> materials;
        uint16_t stone, deepslate, bedrock, dirt, grass, water, lava;
    };

    // the block position the biome of a column is sampled at: its quart cell, at the height of the ground
    struct BiomeSite {
        int x, y, z;
    };
    BiomeSite SiteOf(int worldX, int worldZ, int top);
    uint8_t NetworkBiome(int tableIndex);
}
