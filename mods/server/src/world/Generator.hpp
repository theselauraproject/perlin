#pragma once
#include "core/Config.hpp"
#include "core/Json.hpp"
#include "world/Chunk.hpp"

#include <atomic>
#include <memory>
#include <span>
#include <string>

namespace perlin::server {

    // one batch in, one column per position out; implementations are free to run the whole batch at once
    // (the gpu backend does) and must give identical blocks for the same seed and position
    class TerrainBackend {
    public:
        virtual ~TerrainBackend() = default;
        virtual const char* Name() const = 0;
        virtual void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) = 0;
        // largest batch worth submitting at once
        virtual size_t PreferredBatch() const { return 1; }
        // Generate may be called from several threads at once (cpu backends); the gpu backend owns a device queue and is not
        virtual bool ThreadSafe() const { return false; }
        // the world spawn: the dry land nearest the origin, y being the first block above the ground. false = the backend has no opinion (flat worlds: the origin)
        virtual bool FindSpawn(int& x, int& y, int& z) {
            (void)x;
            (void)y;
            (void)z;
            return false;
        }
        // the nearest place within `radius` blocks of (fromX, fromZ) whose biome is table index `biome` (see world/vanilla/BiomeData.hpp), searched in rings and stopped
        // by `cancel`. false when there is none, the search was cancelled, or this backend has no biomes
        virtual bool LocateBiome(int biome, int fromX, int fromZ, int radius, const std::atomic<bool>& cancel,
                                 int& x, int& y, int& z) {
            (void)biome;
            (void)fromX;
            (void)fromZ;
            (void)radius;
            (void)cancel;
            (void)x;
            (void)y;
            (void)z;
            return false;
        }
        // whether LocateBiome can answer at all
        virtual bool HasBiomes() const { return false; }
        virtual bool Decorates() const { return false; }
        virtual const json::Value* Spawns(int tableIndex) const {
            (void)tableIndex;
            return nullptr;
        }
        virtual void Decorate(ChunkPos pos, const ChunkColumn* const region[9], ChunkColumn& out) const {
            (void)pos;
            (void)region;
            (void)out;
        }
    };

    // flat layers on the cpu: also the reference the gpu backend is checked against
    class FlatCpuBackend final : public TerrainBackend {
    public:
        explicit FlatCpuBackend(const ServerConfig& config);
        const char* Name() const override { return "flat/cpu"; }
        void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override;
        size_t PreferredBatch() const override { return 4; }
        bool ThreadSafe() const override { return true; }

        // block id per world y from the floor up; air above the last layer
        const std::vector<uint16_t>& Profile() const { return profile; }

    private:
        std::vector<uint16_t> profile;
    };

    struct BackendChoice {
        std::unique_ptr<TerrainBackend> backend;
        std::string note;
    };

    // "auto": gpu when it starts and matches the cpu reference on a probe batch, otherwise cpu
    BackendChoice CreateTerrainBackend(const ServerConfig& config);

    // cells of a flat world, also used for the spawn height
    int FlatSurfaceY(const ServerConfig& config);
}
