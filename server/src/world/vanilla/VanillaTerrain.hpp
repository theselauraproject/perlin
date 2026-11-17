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

    class VanillaCpuBackend final : public TerrainBackend {
    public:

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

        int ColumnBiome(int x, int z, int* groundY = nullptr) const;

        float Density(int x, int y, int z) const { return finalDensity->Eval(x, y, z); }

        const DfPtr& FinalDensity() const { return finalDensity; }
        const VanillaClimate& Climate() const { return *climate; }
        const VanillaSurface& Surface() const { return *surface; }
        const VanillaFeatures& Features() const { return *features; }
        DensityGraph& Graph() const { return *graph; }
        const AquiferFunctions* Aquifers() const { return aquifers.ok ? &aquifers : nullptr; }

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
