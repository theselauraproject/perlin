#pragma once
#include "world/Generator.hpp"

#include <span>
#include <vector>

namespace perlin::server {

    // null with `note` saying why when the gpu path is not built, no device starts, or its output differs from the cpu reference
    std::unique_ptr<TerrainBackend> CreateGpuTerrainBackend(const ServerConfig& config, std::string& note);

    // backends that can hand out the raw density of a chunk (vanilla terrain): [chunk][(x * 16 + z) * kHeight + y], what the tests compare bit for bit
    class DensityBackend {
    public:
        virtual ~DensityBackend() = default;
        virtual void Density(std::span<const ChunkPos> positions, std::vector<float>& out) = 0;
    };

    // java terrain with the density graph on the gpu; null with `note` saying why when there is no device, the graph does not fit the kernel,
    // or its density is not bit-identical to the cpu's on the probe chunks
    std::unique_ptr<TerrainBackend> CreateGpuVanillaBackend(const ServerConfig& config, std::string& note);
    std::unique_ptr<TerrainBackend> CreateGpuDimensionBackend(const ServerConfig& config, int dimension, std::string& note);

    // the cpu/gpu agreement check, exposed so tools and tests can run it on any backend
    bool BackendsAgree(TerrainBackend& candidate, TerrainBackend& reference, std::string& detail);
}
