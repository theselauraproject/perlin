#include "world/vanilla/VanillaAquifer.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace perlin::server::vanilla {
    namespace {
        constexpr int kWayBelowMinY = -32512;
        constexpr int kLavaLevel = -54;
        constexpr int kOffsets[13][2] = {{0, 0},  {-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {-3, 0}, {-2, 0},
                                         {-1, 0}, {1, 0},   {-2, 1},  {-1, 1}, {0, 1},  {1, 1}};

        int FloorDiv(int a, int b) {
            int q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
                q--;
            return q;
        }
        int GridX(int v) { return v >> 4; }
        int GridY(int v) { return FloorDiv(v, 12); }
        int FromGridX(int g, int o) { return (g << 4) + o; }
        int FromGridY(int g, int o) { return g * 12 + o; }
        int Floor(double v) {
            const int i = static_cast<int>(v);
            return v < static_cast<double>(i) ? i - 1 : i;
        }
        int Adjust(int level) { return level + 8; }
        double Similarity(int a, int b) { return 1.0 - static_cast<double>(b - a) / 25.0; }
        double ClampedLerp(double t, double a, double b) {
            if (t < 0.0)
                return a;
            if (t > 1.0)
                return b;
            return a + t * (b - a);
        }
        double InverseLerp(double v, double a, double b) { return (v - a) / (b - a); }
        double Map(double v, double a, double b, double c, double d) { return c + InverseLerp(v, a, b) * (d - c); }
        double ClampedMap(double v, double a, double b, double c, double d) {
            return ClampedLerp(InverseLerp(v, a, b), c, d);
        }
    }

    AquiferFunctions AquiferFunctions::Load(const DataPack& pack, DensityGraph& graph) {
        AquiferFunctions a;
        json::Value settings;
        if (!json::Parse(pack.Read("worldgen/noise_settings/overworld.json"), settings))
            return a;
        const json::Value& q = settings["aquifers"];
        if (!q.IsObject())
            return a;
        a.barrier = graph.Inline(q["barrier"]);
        a.floodedness = graph.Inline(q["fluid_level_floodedness"]);
        a.spread = graph.Inline(q["fluid_level_spread"]);
        a.lava = graph.Inline(q["lava"]);
        a.exclusion = graph.Inline(q["exclusion"]);
        a.surfaceLevel = graph.Inline(q["surface_level"]);
        a.seaLevel = static_cast<int>(settings["sea_level"].Number(63));
        a.random = graph.Positional().FromHashOf("minecraft:aquifer").ForkPositional();
        a.ok = a.barrier && a.floodedness && a.spread && a.lava && a.exclusion && a.surfaceLevel;
        return a;
    }

    VanillaAquifer::VanillaAquifer(const AquiferFunctions& functions, int chunkX, int chunkZ, int minY, int height)
        : f(functions) {
        if (f.disabled)
            return;
        const int minX = chunkX * 16, maxX = minX + 15, minZ = chunkZ * 16, maxZ = minZ + 15;
        const int maxY = minY + height - 1;
        minGridX = GridX(minX - 5);
        const int maxGridX = GridX(maxX - 5) + 1;
        sizeX = maxGridX - minGridX + 1;
        minGridY = GridY(minY + 1) - 1;
        const int maxGridY = GridY(maxY + 1) + 1;
        sizeY = maxGridY - minGridY + 1;
        minGridZ = GridX(minZ - 5);
        const int maxGridZ = GridX(maxZ - 5) + 1;
        sizeZ = maxGridZ - minGridZ + 1;
        const size_t total = static_cast<size_t>(sizeX) * static_cast<size_t>(sizeY) * static_cast<size_t>(sizeZ);
        locations.assign(total * 3, 0);
        located.assign(total, 0);
        statuses.assign(total, {});
        known.assign(total, 0);

        const int qx0 = FromGridX(minGridX, 0) >> 2, qz0 = FromGridX(minGridZ, 0) >> 2;
        const int qx1 = FromGridX(maxGridX, 9) >> 2, qz1 = FromGridX(maxGridZ, 9) >> 2;
        int highest = INT_MIN;
        for (int qz = qz0; qz <= qz1; qz++)
            for (int qx = qx0; qx <= qx1; qx++)
                highest = std::max(highest, SurfaceLevel(qx << 2, qz << 2));
        const int skipGridY = GridY(Adjust(highest) + 12) + 1;
        skipAboveY = FromGridY(skipGridY, 11) - 1;
    }

    VanillaAquifer::Fluid VanillaAquifer::Global(int y) const {
        if (y < std::min(kLavaLevel, f.seaLevel))
            return {kLavaLevel, Substance::Lava};
        return {f.seaLevel, Substance::Water};
    }

    int VanillaAquifer::Index(int gx, int gy, int gz) const {
        return ((gy - minGridY) * sizeZ + (gz - minGridZ)) * sizeX + (gx - minGridX);
    }

    int VanillaAquifer::SurfaceLevel(int x, int z) {
        const int qx = (x >> 2) << 2, qz = (z >> 2) << 2;
        const int64_t key = (static_cast<int64_t>(qx) << 32) ^ static_cast<uint32_t>(qz);
        if (const auto it = surfaceCache.find(key); it != surfaceCache.end())
            return it->second;
        const int level = Floor(static_cast<double>(f.surfaceLevel->Eval(qx, 0, qz)));
        surfaceCache.emplace(key, level);
        return level;
    }

    VanillaAquifer::Fluid VanillaAquifer::Status(int index) {
        const size_t i = static_cast<size_t>(index);
        if (!known[i]) {
            statuses[i] = ComputeFluid(static_cast<int>(locations[i * 3]), static_cast<int>(locations[i * 3 + 1]),
                                       static_cast<int>(locations[i * 3 + 2]));
            known[i] = 1;
        }
        return statuses[i];
    }

    VanillaAquifer::Fluid VanillaAquifer::ComputeFluid(int x, int y, int z) {
        const Fluid global = Global(y);
        int lowest = INT_MAX;
        const int top = y + 12, bottom = y - 12;
        bool centerUnder = false;
        for (const auto& o : kOffsets) {
            const int sx = x + (o[0] << 4), sz = z + (o[1] << 4);
            const int level = SurfaceLevel(sx, sz);
            const int adjusted = Adjust(level);
            const bool start = o[0] == 0 && o[1] == 0;
            if (start && bottom > adjusted)
                return global;
            const bool pokes = top > adjusted;
            if (pokes || start) {
                const Fluid atSurface = Global(adjusted);
                if (atSurface.At(adjusted) != Substance::Air) {
                    if (start)
                        centerUnder = true;
                    if (pokes)
                        return atSurface;
                }
            }
            lowest = std::min(lowest, level);
        }
        const int surface = ComputeSurfaceLevel(x, y, z, global, lowest, centerUnder);
        return {surface, ComputeFluidType(x, y, z, global, surface)};
    }

    int VanillaAquifer::ComputeSurfaceLevel(int x, int y, int z, const Fluid& global, int lowest, bool centerUnder) {
        double partially, fully;
        if (static_cast<double>(f.exclusion->Eval(x, y, z)) > 0.0) {
            partially = -1.0;
            fully = -1.0;
        } else {
            const int below = Adjust(lowest) - y;
            const double factor = centerUnder ? ClampedMap(static_cast<double>(below), 0.0, 64.0, 1.0, 0.0) : 0.0;
            const double noise = std::clamp(static_cast<double>(f.floodedness->Eval(x, y, z)), -1.0, 1.0);
            const double fullThreshold = Map(factor, 1.0, 0.0, -0.3, 0.8);
            const double partialThreshold = Map(factor, 1.0, 0.0, -0.8, 0.4);
            partially = noise - partialThreshold;
            fully = noise - fullThreshold;
        }
        if (fully > 0.0)
            return global.level;
        if (partially > 0.0) {
            const int cx = FloorDiv(x, 16), cy = FloorDiv(y, 40), cz = FloorDiv(z, 16);
            const int middle = cy * 40 + 20;
            const double spread = static_cast<double>(f.spread->Eval(cx, cy, cz) * 10.0f);
            const int quantized = Floor(spread / 3.0) * 3;
            return std::min(lowest, middle + quantized);
        }
        return kWayBelowMinY;
    }

    VanillaAquifer::Substance VanillaAquifer::ComputeFluidType(int x, int y, int z, const Fluid& global, int surface) {
        if (surface <= -10 && surface != kWayBelowMinY && global.type != Substance::Lava) {
            const double v =
                static_cast<double>(f.lava->Eval(FloorDiv(x, 64), FloorDiv(y, 40), FloorDiv(z, 64)));
            if (std::abs(v) > 0.3)
                return Substance::Lava;
        }
        return global.type;
    }

    double VanillaAquifer::Pressure(int x, int y, int z, double& barrierValue, const Fluid& a, const Fluid& b) {
        const Substance ta = a.At(y), tb = b.At(y);
        if ((ta == Substance::Lava && tb == Substance::Water) || (ta == Substance::Water && tb == Substance::Lava))
            return 2.0;
        const int diff = std::abs(a.level - b.level);
        if (diff == 0)
            return 0.0;
        const double average = 0.5 * static_cast<double>(a.level + b.level);
        const double above = static_cast<double>(y) + 0.5 - average;
        const double base = static_cast<double>(diff) / 2.0;
        const double toMiddle = base - std::abs(above);
        double gradient;
        if (above > 0.0) {
            const double center = toMiddle;
            gradient = center > 0.0 ? center / 1.5 : center / 2.5;
        } else {
            const double center = 3.0 + toMiddle;
            gradient = center > 0.0 ? center / 3.0 : center / 10.0;
        }
        double noise = 0.0;
        if (gradient >= -2.0 && gradient <= 2.0) {
            if (std::isnan(barrierValue))
                barrierValue = static_cast<double>(f.barrier->Eval(x, y, z));
            noise = barrierValue;
        }
        return 2.0 * (noise + gradient);
    }

    VanillaAquifer::Substance VanillaAquifer::Compute(int x, int y, int z, double density) {
        if (density > 0.0)
            return Substance::Solid;
        if (f.disabled)
            return y < f.seaLevel ? static_cast<Substance>(f.defaultFluid) : Substance::Air;
        const Fluid global = Global(y);
        if (y > skipAboveY)
            return global.At(y);
        if (global.At(y) == Substance::Lava)
            return Substance::Lava;
        const int ax = GridX(x - 5), ay = GridY(y + 1), az = GridX(z - 5);
        int d1 = INT_MAX, d2 = INT_MAX, d3 = INT_MAX;
        int i1 = 0, i2 = 0, i3 = 0;
        for (int ox = 0; ox <= 1; ox++)
            for (int oy = -1; oy <= 1; oy++)
                for (int oz = 0; oz <= 1; oz++) {
                    const int gx = ax + ox, gy = ay + oy, gz = az + oz;
                    const int index = Index(gx, gy, gz);
                    const size_t li = static_cast<size_t>(index) * 3;
                    if (!located[static_cast<size_t>(index)]) {
                        WorldRandom r = f.random.At(gx, gy, gz);
                        locations[li] = FromGridX(gx, r.NextInt(10));
                        locations[li + 1] = FromGridY(gy, r.NextInt(9));
                        locations[li + 2] = FromGridX(gz, r.NextInt(10));
                        located[static_cast<size_t>(index)] = 1;
                    }
                    const int dx = locations[li] - x, dy = locations[li + 1] - y, dz = locations[li + 2] - z;
                    const int d = dx * dx + dy * dy + dz * dz;
                    if (d1 >= d) {
                        i3 = i2, i2 = i1, i1 = index;
                        d3 = d2, d2 = d1, d1 = d;
                    } else if (d2 >= d) {
                        i3 = i2, i2 = index;
                        d3 = d2, d2 = d;
                    } else if (d3 >= d) {
                        i3 = index;
                        d3 = d;
                    }
                }
        const Fluid s1 = Status(i1);
        const double sim12 = Similarity(d1, d2);
        const Substance fluid = s1.At(y);
        if (sim12 <= 0.0)
            return fluid;
        if (fluid == Substance::Water && Global(y - 1).At(y - 1) == Substance::Lava)
            return fluid;
        double barrier = std::nan("");
        const Fluid s2 = Status(i2);
        if (density + sim12 * Pressure(x, y, z, barrier, s1, s2) > 0.0)
            return Substance::Solid;
        const Fluid s3 = Status(i3);
        const double sim13 = Similarity(d1, d3);
        if (sim13 > 0.0 && density + sim12 * sim13 * Pressure(x, y, z, barrier, s1, s3) > 0.0)
            return Substance::Solid;
        const double sim23 = Similarity(d2, d3);
        if (sim23 > 0.0 && density + sim12 * sim23 * Pressure(x, y, z, barrier, s2, s3) > 0.0)
            return Substance::Solid;
        return fluid;
    }
}
