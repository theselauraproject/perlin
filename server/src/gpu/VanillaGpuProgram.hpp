#pragma once
// the density graph flattened for the compute kernel: node records, noise layers with their permutation tables, splines and the evaluation order
#include "world/vanilla/DensityFunction.hpp"
#include "world/vanilla/VanillaSurface.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

    struct GpuProgram {
        static constexpr uint32_t kNodeStride = 14;
        static constexpr uint32_t kLayerStride = 12;
        static constexpr uint32_t kBlendStride = 16;
        // the kernel keeps every node's value in a per-thread array of this size
        static constexpr uint32_t kMaxNodes = 512;
        // spline levels the kernel has functions for (shaders cannot recurse)
        static constexpr uint32_t kMaxSplineDepth = 7;
        // the corner grid is fixed: cells 4 blocks across and 8 up, so a chunk has 5 x 5 x 49 corners
        static constexpr uint32_t kCellXz = 4, kCellY = 8, kCornersXz = 5, kCornersY = 49;

        std::vector<uint32_t> nodes;  // kNodeStride words per node, in dependency order
        std::vector<uint32_t> aux;    // splines and interval-select tables
        std::vector<uint32_t> noises; // per noise: first layer, layer count
        std::vector<uint32_t> layers; // kLayerStride words per perlin layer
        std::vector<uint32_t> perms;  // 256 words per layer
        std::vector<uint32_t> blends; // kBlendStride words per old_blended_noise
        std::vector<uint32_t>
            order; // node indices: first `innerCount` run at the cell corners, the rest per block
        std::vector<uint32_t>
            interpolatedInputs; // node whose value each interpolated node samples at the corners
        uint32_t nodeCount = 0, innerCount = 0, outerCount = 0, root = 0;
        uint32_t cellXz = kCellXz, cellY = kCellY;
        // the graph node behind each flattened node (tests and debugging; valid while the graph lives)
        std::vector<const DfNode*> source;

        // the six climate functions (temperature, vegetation, continents, erosion, depth, ridges), evaluated per column for the biome stage
        std::array<uint32_t, 6> climateRoots{};
        std::vector<uint32_t> climateOrder;

        // header words of the packed tables: where each section starts (the kernel's H_* constants)
        static constexpr uint32_t kHeaderWords = 16;
        enum Header : uint32_t {
            HNodes,
            HAux,
            HNoises,
            HLayers,
            HPerms,
            HBlends,
            HOrder,
            HInterp,
            HClimate,
            HBiomes,
            HMaterials
        };

        // everything the kernel reads in one buffer: the header, then nodes, aux, noises, layers, perms, blends, order, interpolated inputs and the climate list
        // ([count, order..., six roots]). the biome table and materials are appended by the host (see GpuVanillaRhi.cpp)
        std::vector<uint32_t> Pack() const;

        // null with `error` set when the graph uses something the kernel cannot do
        static std::unique_ptr<GpuProgram> Build(const DfPtr& root, const std::array<DfPtr, 6>& climate,
                                                 std::string& error);
    };

    // the whole tables buffer the kernel reads: the program's own sections, then the biome placement table ([count], then per point the 13 climate words and
    // the biome index) and the biome materials (stone, deepslate, bedrock, dirt and grass block ids, then 8 words per biome: top, mid, foundation, sea floor,
    // sea, sea floor depth, builder type, spare). shared by the gpu backend and the cpu harness so both read the same bytes
    std::vector<uint32_t> PackTables(const GpuProgram& program, const VanillaSurface& surface);
}
