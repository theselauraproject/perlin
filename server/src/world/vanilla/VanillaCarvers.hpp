#pragma once
#include "world/vanilla/DataPack.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace perlin::server::vanilla {

    class VanillaAquifer;
    class VanillaMaterials;

    class VanillaCarvers {
    public:
        struct Uniform {
            float lo = 0, hi = 0;
            bool trapezoid = false;
            float plateau = 0;
            float constant = 1;
            bool isConstant = true;
        };
        struct Carver {
            bool canyon = false;
            float probability = 0;
            int yMin = 0, yMax = 0;
            int countMin = 0, countMax = 0;
            Uniform thickness, roomVertical, horizontal, vertical, startVertical, floor;
            bool weirdBias = false;
            Uniform verticalRotation, distanceFactor, horizontalFactor, yScale;
            int widthSmoothness = 3;
            float verticalDefault = 1, verticalCenter = 0;
        };

        static std::unique_ptr<VanillaCarvers> Load(const DataPack& pack, int64_t seed);

        void Carve(int chunkX, int chunkZ, uint16_t* blocks, VanillaAquifer& aquifer, const VanillaMaterials* materials,
                   const uint8_t* biomes, const std::function<float(int, int, int, int)>& temperature,
                   const std::function<int(int, int, int)>& biomeAt) const;

    private:
        std::vector<Carver> carvers;
        int64_t seed = 0;
        uint16_t bedrock = 0, grass = 0, mycelium = 0, dirt = 0, water = 0, lava = 0;
    };
}
