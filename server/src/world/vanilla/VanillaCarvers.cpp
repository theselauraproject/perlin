#include "world/vanilla/VanillaCarvers.hpp"

#include "core/Json.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Chunk.hpp"
#include "world/vanilla/VanillaAquifer.hpp"
#include "world/vanilla/VanillaMaterials.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace perlin::server::vanilla {
    namespace {
        constexpr int kMaskMinY = kMinY + 1;
        constexpr int kMaskMaxY = kMinY + kHeight - 1 - 7;
        constexpr int kMaskHeight = kMaskMaxY - kMaskMinY + 1;
        constexpr float kPiF = 3.14159265358979323846f;

        const std::array<float, 65536>& SinTable() {
            static const std::array<float, 65536> table = [] {
                std::array<float, 65536> t{};
                for (size_t i = 0; i < t.size(); i++)
                    t[i] = static_cast<float>(std::sin(static_cast<double>(i) / 10430.378350470453));
                return t;
            }();
            return table;
        }
        float MthSin(double v) {
            return SinTable()[static_cast<size_t>(static_cast<int64_t>(v * 10430.378350470453) & 0xFFFF)];
        }
        float MthCos(double v) {
            return SinTable()[static_cast<size_t>(static_cast<int64_t>(v * 10430.378350470453 + 16384.0) & 0xFFFF)];
        }
        int FloorD(double v) {
            const int i = static_cast<int>(v);
            return v < static_cast<double>(i) ? i - 1 : i;
        }

        class Lcg {
        public:
            explicit Lcg(int64_t seed) { SetSeed(seed); }
            void SetSeed(int64_t seed) { s = (static_cast<uint64_t>(seed) ^ 0x5DEECE66Dull) & kMask; }
            int Next(int bits) {
                s = (s * 0x5DEECE66Dull + 0xBull) & kMask;
                return static_cast<int>(static_cast<int32_t>(static_cast<uint32_t>(s >> (48 - bits))));
            }
            int NextInt(int bound) {
                if ((bound & -bound) == bound)
                    return static_cast<int>((static_cast<int64_t>(bound) * Next(31)) >> 31);
                int bits, val;
                do {
                    bits = Next(31);
                    val = bits % bound;
                } while (bits - val + (bound - 1) < 0);
                return val;
            }
            int64_t NextLong() {
                const int64_t hi = static_cast<int64_t>(Next(32));
                const int64_t lo = static_cast<int64_t>(Next(32));
                return static_cast<int64_t>(static_cast<uint64_t>(hi) << 32) + lo;
            }
            float NextFloat() { return static_cast<float>(Next(24)) * 5.9604645E-8f; }
            void SetLargeFeatureSeed(int64_t seed, int x, int z) {
                SetSeed(seed);
                const int64_t a = NextLong(), b = NextLong();
                SetSeed(static_cast<int64_t>((static_cast<uint64_t>(static_cast<int64_t>(x)) * static_cast<uint64_t>(a)) ^
                                             (static_cast<uint64_t>(static_cast<int64_t>(z)) * static_cast<uint64_t>(b)) ^
                                             static_cast<uint64_t>(seed)));
            }

        private:
            static constexpr uint64_t kMask = (1ull << 48) - 1;
            uint64_t s = 0;
        };

        float Sample(const VanillaCarvers::Uniform& u, Lcg& r) {
            if (u.isConstant)
                return u.constant;
            if (u.trapezoid) {
                const float range = u.hi - u.lo;
                const float plateauStart = (range - u.plateau) / 2.0f;
                const float plateauEnd = range - plateauStart;
                return u.lo + r.NextFloat() * plateauEnd + r.NextFloat() * plateauStart;
            }
            return r.NextFloat() * (u.hi - u.lo) + u.lo;
        }

        VanillaCarvers::Uniform ReadFloat(const json::Value& v, float fallback) {
            VanillaCarvers::Uniform u;
            if (v.kind == json::Value::Kind::Null) {
                u.constant = fallback;
                return u;
            }
            if (v.kind == json::Value::Kind::Number) {
                u.constant = static_cast<float>(v.number);
                return u;
            }
            const std::string type = v["type"].String();
            u.isConstant = false;
            if (type.ends_with("trapezoid")) {
                u.trapezoid = true;
                u.lo = static_cast<float>(v["min"].Number());
                u.hi = static_cast<float>(v["max"].Number());
                u.plateau = static_cast<float>(v["plateau"].Number());
            } else if (type.ends_with("constant")) {
                u.isConstant = true;
                u.constant = static_cast<float>(v["value"].Number(fallback));
            } else {
                u.lo = static_cast<float>(v["min_inclusive"].Number());
                u.hi = static_cast<float>(v["max_exclusive"].Number());
            }
            return u;
        }

        int Anchor(const json::Value& v) {
            if (v["absolute"].kind != json::Value::Kind::Null)
                return static_cast<int>(v["absolute"].Int());
            if (v["above_bottom"].kind != json::Value::Kind::Null)
                return kMinY + static_cast<int>(v["above_bottom"].Int());
            if (v["below_top"].kind != json::Value::Kind::Null)
                return kMinY + kHeight - 1 - static_cast<int>(v["below_top"].Int());
            return 0;
        }

        struct Mask {
            std::vector<uint8_t> bits = std::vector<uint8_t>(static_cast<size_t>(256 * kMaskHeight), 0);
            bool any = false;
            void Set(int x, int y, int z) {
                bits[static_cast<size_t>(y - kMaskMinY + (z + (x << 4)) * kMaskHeight)] = 1;
                any = true;
            }
            bool Get(int x, int y, int z) const {
                return bits[static_cast<size_t>(y - kMaskMinY + (z + (x << 4)) * kMaskHeight)] != 0;
            }
        };

        template <class Skip>
        void Ellipsoid(int chunkX, int chunkZ, double x, double y, double z, double hr, double vr, Mask& out, const Skip& skip) {
            const double centerX = chunkX * 16 + 8, centerZ = chunkZ * 16 + 8;
            const double maxDelta = 16.0 + hr * 2.0;
            if (std::abs(x - centerX) > maxDelta || std::abs(z - centerZ) > maxDelta)
                return;
            const int minX = chunkX * 16, minZ = chunkZ * 16;
            const int x0 = std::max(FloorD(x - hr) - minX - 1, 0), x1 = std::min(FloorD(x + hr) - minX, 15);
            const int y0 = std::max(FloorD(y - vr) - 1, kMaskMinY), y1 = std::min(FloorD(y + vr) + 1, kMaskMaxY);
            const int z0 = std::max(FloorD(z - hr) - minZ - 1, 0), z1 = std::min(FloorD(z + hr) - minZ, 15);
            for (int xi = x0; xi <= x1; xi++) {
                const double xd = (static_cast<double>(minX + xi) + 0.5 - x) / hr;
                for (int zi = z0; zi <= z1; zi++) {
                    const double zd = (static_cast<double>(minZ + zi) + 0.5 - z) / hr;
                    if (xd * xd + zd * zd >= 1.0)
                        continue;
                    for (int wy = y1; wy > y0; wy--) {
                        const double yd = (static_cast<double>(wy) - 0.5 - y) / vr;
                        if (skip(xd, yd, zd, wy))
                            continue;
                        out.Set(xi, wy, zi);
                    }
                }
            }
        }

        bool CanReach(int chunkX, int chunkZ, double x, double z, int step, int total, float thickness) {
            const double xd = x - (chunkX * 16 + 8), zd = z - (chunkZ * 16 + 8);
            const double remaining = static_cast<double>(total - step);
            const double rr = static_cast<double>(thickness + 2.0f + 16.0f);
            return xd * xd + zd * zd - remaining * remaining <= rr * rr;
        }

        struct CaveRun {
            const VanillaCarvers::Carver& c;
            int chunkX, chunkZ;
            Mask& mask;

            void Tunnel(int64_t seed, double x, double y, double z, double hMul, double vMul, float thickness, float hRot,
                        float vRot, int step, int dist, double yScale, double floor) {
                Lcg random(seed);
                const int split = random.NextInt(dist / 2) + dist / 4;
                const bool steep = random.NextInt(6) == 0;
                float yRota = 0.0f, xRota = 0.0f;
                const auto skip = [floor](double xd, double yd, double zd, int) {
                    return yd <= floor || xd * xd + yd * yd + zd * zd >= 1.0;
                };
                for (int current = step; current < dist; current++) {
                    const double hr = 1.5 + static_cast<double>(
                                                MthSin(static_cast<double>(kPiF * static_cast<float>(current) / static_cast<float>(dist))) * thickness);
                    const double vr = hr * yScale;
                    const float cosX = MthCos(static_cast<double>(vRot));
                    x += static_cast<double>(MthCos(static_cast<double>(hRot)) * cosX);
                    y += static_cast<double>(MthSin(static_cast<double>(vRot)));
                    z += static_cast<double>(MthSin(static_cast<double>(hRot)) * cosX);
                    vRot *= steep ? 0.92f : 0.7f;
                    vRot += xRota * 0.1f;
                    hRot += yRota * 0.1f;
                    xRota *= 0.9f;
                    yRota *= 0.75f;
                    xRota += (random.NextFloat() - random.NextFloat()) * random.NextFloat() * 2.0f;
                    yRota += (random.NextFloat() - random.NextFloat()) * random.NextFloat() * 4.0f;
                    if (current == split && thickness > 1.0f) {
                        const int64_t s1 = random.NextLong();
                        const float t1 = random.NextFloat() * 0.5f + 0.5f;
                        Tunnel(s1, x, y, z, hMul, vMul, t1, hRot - 1.5707964f, vRot / 3.0f, current, dist, 1.0, floor);
                        const int64_t s2 = random.NextLong();
                        const float t2 = random.NextFloat() * 0.5f + 0.5f;
                        Tunnel(s2, x, y, z, hMul, vMul, t2, hRot + 1.5707964f, vRot / 3.0f, current, dist, 1.0, floor);
                        return;
                    }
                    if (random.NextInt(4) == 0)
                        continue;
                    if (!CanReach(chunkX, chunkZ, x, z, current, dist, thickness))
                        return;
                    Ellipsoid(chunkX, chunkZ, x, y, z, hr * hMul, vr * vMul, mask, skip);
                }
            }

            void Run(Lcg& random, int sourceX, int sourceZ) {
                const int maxDistance = (4 * 2 - 1) * 16;
                const int span = c.countMax - c.countMin + 1;
                const int count = c.countMin + random.NextInt(random.NextInt(random.NextInt(span) + 1) + 1);
                for (int cave = 0; cave < count; cave++) {
                    const double x = static_cast<double>(sourceX * 16 + random.NextInt(16));
                    const double y = static_cast<double>(random.NextInt(c.yMax - c.yMin + 1) + c.yMin);
                    const double z = static_cast<double>(sourceZ * 16 + random.NextInt(16));
                    const double hMul = static_cast<double>(Sample(c.horizontal, random));
                    const double vMul = static_cast<double>(Sample(c.vertical, random));
                    const double startV = static_cast<double>(Sample(c.startVertical, random));
                    const double floor = static_cast<double>(Sample(c.floor, random));
                    const auto skip = [floor](double xd, double yd, double zd, int) {
                        return yd <= floor || xd * xd + yd * yd + zd * zd >= 1.0;
                    };
                    int tunnels = 1;
                    if (random.NextInt(4) == 0) {
                        const double yScale = static_cast<double>(Sample(c.roomVertical, random));
                        const float thickness = 1.0f + random.NextFloat() * 6.0f;
                        const double hr = 1.5 + static_cast<double>(MthSin(1.5707963705062866) * thickness);
                        Ellipsoid(chunkX, chunkZ, x + 1.0, y, z, hr, hr * yScale, mask, skip);
                        tunnels += random.NextInt(4);
                    }
                    for (int i = 0; i < tunnels; i++) {
                        const float hRot = random.NextFloat() * (kPiF * 2.0f);
                        const float vRot = (random.NextFloat() - 0.5f) / 4.0f;
                        float thickness = Sample(c.thickness, random);
                        if (c.weirdBias && random.NextInt(10) == 0)
                            thickness *= random.NextFloat() * random.NextFloat() * 3.0f + 1.0f;
                        const int distance = maxDistance - random.NextInt(maxDistance / 4);
                        const int64_t seed = random.NextLong();
                        Tunnel(seed, x, y, z, hMul, vMul, thickness, hRot, vRot, 0, distance, startV, floor);
                    }
                }
            }
        };

        struct CanyonRun {
            const VanillaCarvers::Carver& c;
            int chunkX, chunkZ;
            Mask& mask;

            void Run(Lcg& random, int sourceX, int sourceZ) {
                const int maxDistance = (4 * 2 - 1) * 16;
                const double x = static_cast<double>(sourceX * 16 + random.NextInt(16));
                const int y = random.NextInt(c.yMax - c.yMin + 1) + c.yMin;
                const double z = static_cast<double>(sourceZ * 16 + random.NextInt(16));
                const float hRot = random.NextFloat() * (kPiF * 2.0f);
                const float vRot = Sample(c.verticalRotation, random);
                const double yScale = static_cast<double>(Sample(c.yScale, random));
                const float thickness = Sample(c.thickness, random);
                const int distance = static_cast<int>(static_cast<float>(maxDistance) * Sample(c.distanceFactor, random));
                const int64_t seed = random.NextLong();
                Carve(seed, x, static_cast<double>(y), z, thickness, hRot, vRot, 0, distance, yScale);
            }

            void Carve(int64_t seed, double x, double y, double z, float thickness, float hRot, float vRot, int step,
                       int distance, double yScale) {
                Lcg random(seed);
                std::vector<float> widths(static_cast<size_t>(kHeight));
                float factor = 1.0f;
                for (int i = 0; i < kHeight; i++) {
                    if (i == 0 || random.NextInt(c.widthSmoothness) == 0)
                        factor = 1.0f + random.NextFloat() * random.NextFloat();
                    widths[static_cast<size_t>(i)] = factor * factor;
                }
                float yRota = 0.0f, xRota = 0.0f;
                const auto skip = [&widths](double xd, double yd, double zd, int wy) {
                    const int index = wy - kMinY;
                    return (xd * xd + zd * zd) * static_cast<double>(widths[static_cast<size_t>(index - 1)]) + yd * yd / 6.0 >= 1.0;
                };
                for (int current = step; current < distance; current++) {
                    double hr = 1.5 + static_cast<double>(
                                          MthSin(static_cast<double>(static_cast<float>(current) * kPiF / static_cast<float>(distance))) * thickness);
                    double vr = hr * yScale;
                    hr *= static_cast<double>(Sample(c.horizontalFactor, random));
                    const float multiplier =
                        1.0f - std::abs(0.5f - static_cast<float>(current) / static_cast<float>(distance)) * 2.0f;
                    const float f = c.verticalDefault + c.verticalCenter * multiplier;
                    vr = static_cast<double>(f) * vr * static_cast<double>(random.NextFloat() * (1.0f - 0.75f) + 0.75f);
                    const float xc = MthCos(static_cast<double>(vRot)), xs = MthSin(static_cast<double>(vRot));
                    x += static_cast<double>(MthCos(static_cast<double>(hRot)) * xc);
                    y += static_cast<double>(xs);
                    z += static_cast<double>(MthSin(static_cast<double>(hRot)) * xc);
                    vRot *= 0.7f;
                    vRot += xRota * 0.05f;
                    hRot += yRota * 0.05f;
                    xRota *= 0.8f;
                    yRota *= 0.5f;
                    xRota += (random.NextFloat() - random.NextFloat()) * random.NextFloat() * 2.0f;
                    yRota += (random.NextFloat() - random.NextFloat()) * random.NextFloat() * 4.0f;
                    if (random.NextInt(4) == 0)
                        continue;
                    if (!CanReach(chunkX, chunkZ, x, z, current, distance, thickness))
                        return;
                    Ellipsoid(chunkX, chunkZ, x, y, z, hr, vr, mask, skip);
                }
            }
        };
    }

    std::unique_ptr<VanillaCarvers> VanillaCarvers::Load(const DataPack& pack, int64_t seed) {
        std::unique_ptr<VanillaCarvers> out(new VanillaCarvers());
        out->seed = seed;
        for (const char* name : {"cave", "cave_extra_underground", "canyon"}) {
            json::Value v;
            if (!json::Parse(pack.Read(std::string("worldgen/carver/") + name + ".json"), v))
                return nullptr;
            Carver c;
            c.canyon = v["type"].String().ends_with("canyon");
            c.probability = static_cast<float>(v["probability"].Number());
            c.yMin = Anchor(v["y"]["min_inclusive"]);
            c.yMax = Anchor(v["y"]["max_inclusive"]);
            if (c.canyon) {
                const json::Value& s = v["shape"];
                c.verticalRotation = ReadFloat(v["vertical_rotation"], 0);
                c.distanceFactor = ReadFloat(s["distance_factor"], 1);
                c.thickness = ReadFloat(s["thickness"], 0);
                c.widthSmoothness = static_cast<int>(s["width_smoothness"].Int(3));
                c.horizontalFactor = ReadFloat(s["horizontal_radius_factor"], 1);
                c.verticalDefault = static_cast<float>(s["vertical_radius_default_factor"].Number(1));
                c.verticalCenter = static_cast<float>(s["vertical_radius_center_factor"].Number(0));
                c.yScale = ReadFloat(s["y_scale"], 3);
            } else {
                c.countMin = static_cast<int>(v["count"]["min_inclusive"].Int());
                c.countMax = static_cast<int>(v["count"]["max_inclusive"].Int());
                c.thickness = ReadFloat(v["thickness"], 1);
                c.weirdBias = v["weird_thickness_bias"].Bool();
                c.roomVertical = ReadFloat(v["room_vertical_radius_multiplier"], 1);
                c.horizontal = ReadFloat(v["horizontal_radius_multiplier"], 1);
                c.vertical = ReadFloat(v["vertical_radius_multiplier"], 1);
                c.startVertical = ReadFloat(v["start_vertical_radius_multiplier"], 1);
                c.floor = ReadFloat(v["floor_level"], -0.7f);
            }
            out->carvers.push_back(c);
        }
        const BlockCatalog& b = BlockCatalog::Instance();
        out->bedrock = b.DefaultBlock("bedrock");
        out->grass = b.DefaultBlock("grass_block");
        out->mycelium = b.DefaultBlock("mycelium");
        out->dirt = b.DefaultBlock("dirt");
        out->water = b.DefaultBlock("water");
        out->lava = b.DefaultBlock("lava");
        return out;
    }

    void VanillaCarvers::Carve(int chunkX, int chunkZ, uint16_t* blocks, VanillaAquifer& aquifer,
                               const VanillaMaterials* materials, const uint8_t* biomes,
                               const std::function<float(int, int, int, int)>& temperature,
                               const std::function<int(int, int, int)>& biomeAt) const {
        Mask mask;
        Lcg random(0);
        for (int dx = -8; dx <= 8; dx++)
            for (int dz = -8; dz <= 8; dz++) {
                const int sx = chunkX + dx, sz = chunkZ + dz;
                for (size_t index = 0; index < carvers.size(); index++) {
                    const Carver& c = carvers[index];
                    random.SetLargeFeatureSeed(seed + static_cast<int64_t>(index), sx, sz);
                    if (!(random.NextFloat() <= c.probability))
                        continue;
                    if (c.canyon) {
                        CanyonRun run{c, chunkX, chunkZ, mask};
                        run.Run(random, sx, sz);
                    } else {
                        CaveRun run{c, chunkX, chunkZ, mask};
                        run.Run(random, sx, sz);
                    }
                }
            }
        if (!mask.any)
            return;
        using Sub = VanillaAquifer::Substance;
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                uint16_t* col = blocks + static_cast<size_t>((x * 16 + z) * kHeight);
                const int wx = chunkX * 16 + x, wz = chunkZ * 16 + z;
                int y = kMaskMinY;
                while (y <= kMaskMaxY) {
                    if (!mask.Get(x, y, z)) {
                        y++;
                        continue;
                    }
                    int top = y;
                    while (top + 1 <= kMaskMaxY && mask.Get(x, top + 1, z))
                        top++;
                    const int bottom = y;
                    bool hasGrass = false;
                    for (int wy = top; wy >= bottom; wy--) {
                        uint16_t& block = col[wy - kMinY];
                        if (block == bedrock)
                            continue;
                        if (block == grass || block == mycelium)
                            hasGrass = true;
                        const Sub s = aquifer.Compute(wx, wy, wz, 0.0);
                        if (s == Sub::Solid)
                            continue;
                        block = s == Sub::Water ? water : s == Sub::Lava ? lava : static_cast<uint16_t>(0);
                        if (!hasGrass || wy - 1 < kMinY || col[wy - 1 - kMinY] != dirt || !materials)
                            continue;
                        uint16_t top_material;
                        if (materials->TopMaterial(chunkX, chunkZ, x, z, wy - 1, biomes, blocks, s != Sub::Air,
                                                   temperature, biomeAt,
                                                   top_material))
                            col[wy - 1 - kMinY] = top_material;
                    }
                    y = top + 1;
                }
            }
    }
}
