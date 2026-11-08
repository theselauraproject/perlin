#pragma once
#include "core/Config.hpp"
#include "core/Json.hpp"
#include "world/Chunk.hpp"

#include <atomic>
#include <memory>
#include <span>
#include <string>

namespace perlin::server {

    class TerrainBackend {
    public:
        virtual ~TerrainBackend() = default;
        virtual const char* Name() const = 0;
        virtual void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) = 0;

        virtual size_t PreferredBatch() const { return 1; }

        virtual bool ThreadSafe() const { return false; }

        virtual bool FindSpawn(int& x, int& y, int& z) {
            (void)x;
            (void)y;
            (void)z;
            return false;
        }

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

    class FlatCpuBackend final : public TerrainBackend {
    public:
        explicit FlatCpuBackend(const ServerConfig& config);
        const char* Name() const override { return "flat/cpu"; }
        void Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) override;
        size_t PreferredBatch() const override { return 4; }
        bool ThreadSafe() const override { return true; }

        const std::vector<uint16_t>& Profile() const { return profile; }

    private:
        std::vector<uint16_t> profile;
    };

    struct BackendChoice {
        std::unique_ptr<TerrainBackend> backend;
        std::string note;
    };

    BackendChoice CreateTerrainBackend(const ServerConfig& config);

    int FlatSurfaceY(const ServerConfig& config);
}
