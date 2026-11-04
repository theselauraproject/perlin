#pragma once
#include "world/vanilla/DensityFunction.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace perlin::server::vanilla {

    struct AquiferFunctions {
        DfPtr barrier, floodedness, spread, lava, exclusion, surfaceLevel;
        Xoroshiro::Factory random;
        int seaLevel = 63;
        bool ok = false;

        static AquiferFunctions Load(const DataPack& pack, DensityGraph& graph);
    };

    class VanillaAquifer {
    public:
        enum class Substance : uint8_t { Solid, Air, Water, Lava };

        VanillaAquifer(const AquiferFunctions& functions, int chunkX, int chunkZ, int minY, int height);
        Substance Compute(int x, int y, int z, double density);

    private:
        struct Fluid {
            int level = 0;
            Substance type = Substance::Air;
            Substance At(int y) const { return y < level ? type : Substance::Air; }
            bool operator==(const Fluid&) const = default;
        };

        Fluid Global(int y) const;
        int Index(int gx, int gy, int gz) const;
        Fluid Status(int index);
        Fluid ComputeFluid(int x, int y, int z);
        int SurfaceLevel(int x, int z);
        int ComputeSurfaceLevel(int x, int y, int z, const Fluid& global, int lowest, bool centerUnder);
        Substance ComputeFluidType(int x, int y, int z, const Fluid& global, int surface);
        double Pressure(int x, int y, int z, double& barrierValue, const Fluid& a, const Fluid& b);

        const AquiferFunctions& f;
        int minGridX = 0, minGridY = 0, minGridZ = 0, sizeX = 0, sizeY = 0, sizeZ = 0;
        int skipAboveY = 0;
        std::vector<int> locations;
        std::vector<uint8_t> located;
        std::vector<Fluid> statuses;
        std::vector<uint8_t> known;
        std::unordered_map<int64_t, int> surfaceCache;
    };
}
