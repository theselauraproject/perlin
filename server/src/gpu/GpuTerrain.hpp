#pragma once
#include "world/Generator.hpp"

#include <span>
#include <vector>

namespace perlin::server {

    std::unique_ptr<TerrainBackend> CreateGpuTerrainBackend(const ServerConfig& config, std::string& note);

    class DensityBackend {
    public:
        virtual ~DensityBackend() = default;
        virtual void Density(std::span<const ChunkPos> positions, std::vector<float>& out) = 0;
    };

    std::unique_ptr<TerrainBackend> CreateGpuVanillaBackend(const ServerConfig& config, std::string& note);
    std::unique_ptr<TerrainBackend> CreateGpuDimensionBackend(const ServerConfig& config, int dimension, std::string& note);

    bool BackendsAgree(TerrainBackend& candidate, TerrainBackend& reference, std::string& detail);
}
