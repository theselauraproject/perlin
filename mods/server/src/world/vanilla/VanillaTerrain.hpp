#pragma once
#include "world/Generator.hpp"
#include "world/vanilla/DensityFunction.hpp"
#include "world/vanilla/VanillaAquifer.hpp"
#include "world/vanilla/VanillaCarvers.hpp"
#include "world/vanilla/VanillaMaterials.hpp"
#include "world/vanilla/JavaDecorator.hpp"
#include "world/vanilla/VanillaFeatures.hpp"
#include "world/vanilla/VanillaSurface.hpp"

namespace perlin::server::vanilla {

    // java terrain on the cpu, and the reference every gpu stage is checked against: final_density decides stone against air, the climate where the ground is
    // picks a biome from Java's placement table, and the biome's Bedrock surface materials (bedrock-samples) make the blocks; sea level fills the rest with water
    class VanillaCpuBackend final : public TerrainBackend {
    public:
        // null with `error` set when the data cannot be read
        static std::unique_ptr<VanillaCpuBackend> Create(const ServerConfig& config, std::string& error);

        const char* Name() const override { return "vanilla/cpu"; }
        void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override;
        size_t PreferredBatch() const override { return 1; }
        bool ThreadSafe() const override { return true; }
        bool FindSpawn(int& x, int& y, int& z) override;
        bool LocateBiome(int biome, int fromX, int fromZ, int radius, const std::atomic<bool>& cancel, int& x,
                         int& y, int& z) override;
        bool HasBiomes() const override { return true; }
        bool Decorates() const override { return decorator && decorator->Ok(); }
        void Decorate(ChunkPos pos, const ChunkColumn* const region[9], ChunkColumn& out) const override {
            decorator->Decorate(pos.x, pos.z, region, out);
        }
        const JavaDecorator* Decorator() const { return decorator.get(); }
        const json::Value* Spawns(int tableIndex) const override {
            const JavaBiome* b = decorator ? decorator->Biome(tableIndex) : nullptr;
            return b ? b->spawns : nullptr;
        }
        // the biome the generator puts in the column at (x, z): the ground found from the density, the climate there, the table
        // groundY, when given, receives the first free block above the ground
        int ColumnBiome(int x, int z, int* groundY = nullptr) const;

        // block density > 0 means solid; exposed for tests
        float Density(int x, int y, int z) const { return finalDensity->Eval(x, y, z); }
        // the graph and the block rules, for the gpu backend that shares both
        const DfPtr& FinalDensity() const { return finalDensity; }
        const VanillaClimate& Climate() const { return *climate; }
        const VanillaSurface& Surface() const { return *surface; }
        const VanillaFeatures& Features() const { return *features; }
        DensityGraph& Graph() const { return *graph; }
        const AquiferFunctions* Aquifers() const { return aquifers.ok ? &aquifers : nullptr; }
        // the biome stage on its own: one biome index per column [x * 16 + z] from a chunk's densities
        void BiomesOf(const float* density, int chunkX, int chunkZ, uint8_t* biomes) const;
        int NoiseBiome(int quartX, int quartY, int quartZ) const;
        const uint8_t* ChunkFill(int chunkX, int chunkZ) const;
        void PrimeChunkFill(int chunkX, int chunkZ, const int32_t* targets) const;
        int BiomeAt(int x, int y, int z) const;
        void QuartBiomes(int chunkX, int chunkZ, ChunkColumn& column) const;
        int64_t ZoomSeed() const { return zoomSeed; }

    private:
        VanillaCpuBackend() = default;
        const uint8_t* SearchFill(int chunkX, int chunkZ, const int32_t* targets) const;
        std::unique_ptr<DataPack> pack;
        std::unique_ptr<DensityGraph> graph;
        DfPtr finalDensity;
        std::unique_ptr<VanillaClimate> climate;
        std::unique_ptr<VanillaSurface> surface;
        std::unique_ptr<VanillaFeatures> features;
        std::unique_ptr<JavaDecorator> decorator;
        AquiferFunctions aquifers;
        int64_t zoomSeed = 0;
        uint64_t instance = 0;
        std::unique_ptr<VanillaMaterials> materials;
        std::unique_ptr<VanillaCarvers> carvers;
        TerrainStages stages;

    public:
        const TerrainStages* Stages() const { return &stages; }
    };
}
