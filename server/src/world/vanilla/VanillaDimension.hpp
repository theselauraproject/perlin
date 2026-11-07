#pragma once
#include "world/Generator.hpp"
#include "world/vanilla/DensityFunction.hpp"
#include "world/vanilla/VanillaAquifer.hpp"
#include "world/vanilla/VanillaCarvers.hpp"
#include "world/vanilla/VanillaMaterials.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

    class VanillaDimensionBackend final : public TerrainBackend {
    public:
        static std::unique_ptr<VanillaDimensionBackend> Create(const ServerConfig& config, int dimension, std::string& error);

        const char* Name() const override { return dimension == kNether ? "vanilla/nether" : "vanilla/end"; }
        void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override;
        size_t PreferredBatch() const override { return 1; }
        bool ThreadSafe() const override { return true; }
        bool HasBiomes() const override { return true; }
        bool LocateBiome(int biome, int fromX, int fromZ, int radius, const std::atomic<bool>& cancel, int& x, int& y,
                         int& z) override;

        int BiomeAt(int x, int y, int z) const;
        float Density(int x, int y, int z) const { return finalDensity->Eval(x, y, z); }
        int DimensionId() const { return dimension; }
        const DfPtr& FinalDensity() const { return finalDensity; }
        std::array<DfPtr, 6> ClimateRoots() const { return {temperature, vegetation, zero, erosion, zero, zero}; }
        int GenMinY() const { return genMinY; }
        int GenHeight() const { return genHeight; }
        void Build(int chunkX, int chunkZ, const float* density, ChunkColumn& column) const;
        void PrimeChunkFill(int chunkX, int chunkZ, const int32_t* targets) const;

    private:
        VanillaDimensionBackend() = default;
        const uint8_t* ChunkFill(int chunkX, int chunkZ, const int32_t* primed = nullptr) const;
        int NoiseBiome(int quartX, int quartY, int quartZ) const;
        int SearchNether(const int32_t target[2], int& hint) const;
        int EndBiome(int quartX, int quartY, int quartZ) const;

        int dimension = kNether;
        std::unique_ptr<DataPack> pack;
        std::unique_ptr<DensityGraph> graph;
        DfPtr finalDensity, temperature, vegetation, erosion, zero;
        AquiferFunctions aquifers;
        std::unique_ptr<VanillaMaterials> materials;
        std::unique_ptr<VanillaCarvers> carvers;
        int64_t zoomSeed = 0;
        uint64_t instance = 0;
        int seaLevel = 32;
        int genMinY = 0, genHeight = 128;
        uint16_t defaultBlock = 0, lava = 0;
        struct NetherPoint {
            int32_t temperature, humidity, offset;
            int biome;
        };
        std::vector<NetherPoint> netherPoints;
        int endCenter = 0, endHighlands = 0, endMidlands = 0, endIslands = 0, endBarrens = 0;
    };
}
