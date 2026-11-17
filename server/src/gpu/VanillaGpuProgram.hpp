#pragma once

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

        static constexpr uint32_t kMaxNodes = 512;

        static constexpr uint32_t kMaxSplineDepth = 7;

        static constexpr uint32_t kCellXz = 4, kCellY = 8, kCornersXz = 5, kCornersY = 49;

        std::vector<uint32_t> nodes;
        std::vector<uint32_t> aux;
        std::vector<uint32_t> noises;
        std::vector<uint32_t> layers;
        std::vector<uint32_t> perms;
        std::vector<uint32_t> blends;
        std::vector<uint32_t>
            order;
        std::vector<uint32_t>
            interpolatedInputs;
        uint32_t nodeCount = 0, innerCount = 0, outerCount = 0, root = 0;
        uint32_t cellXz = kCellXz, cellY = kCellY;

        std::vector<const DfNode*> source;

        std::array<uint32_t, 6> climateRoots{};
        std::vector<uint32_t> climateOrder;

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

        std::vector<uint32_t> Pack() const;

        static std::unique_ptr<GpuProgram> Build(const DfPtr& root, const std::array<DfPtr, 6>& climate,
                                                 std::string& error);
    };

    std::vector<uint32_t> PackTables(const GpuProgram& program, const VanillaSurface& surface);
}
