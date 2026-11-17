#include "world/vanilla/VanillaFeatures.hpp"

#include "world/BlockCatalog.hpp"
#include "world/Chunk.hpp"
#include "world/vanilla/JavaRandom.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace perlin::server::vanilla {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        struct Stream {
            WorldRandom rng;
            Stream(uint64_t a, uint64_t b) : rng(MixStafford13(a), MixStafford13(b ^ kGolden64)) {}

            int Below(int n) { return n <= 1 ? 0 : static_cast<int>(rng.NextDouble() * n); }
            double Double() { return rng.NextDouble(); }
            float Float() { return rng.NextFloat(); }
        };

        int Draw(Stream& s, const DistDef& d, bool inclusive) {
            if (d.kind == 2)
                return d.lo;
            const int span = d.hi - d.lo + (inclusive ? 1 : 0);
            if (d.kind == 0)
                return d.lo + s.Below(span);

            return d.lo + (s.Below(span) + s.Below(span)) / 2;
        }

        float HashFloat(uint64_t key, int x, int y, int z) {
            const uint64_t h =
                MixStafford13(key ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0x9E3779B1ull) ^
                              (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 21) ^
                              (static_cast<uint64_t>(static_cast<uint32_t>(z)) << 42));
            return static_cast<float>(h >> 40) * 5.9604645E-8f;
        }

        struct Blob {
            double cx[64], cy[64], cz[64], radius[64];
            int n = 0;
            double minX, maxX, minZ, maxZ;
        };
    }

    VanillaFeatures::VanillaFeatures(int64_t seed) : seed(seed) {
        const BlockCatalog& c = BlockCatalog::Instance();
        features.reserve(static_cast<size_t>(kOreFeatureCount));
        for (int i = 0; i < kOreFeatureCount; i++) {
            const OreFeatureDef& d = kOreFeatures[i];
            Feature f{d.count, d.discardChanceOnAirExposure, {}};
            for (int r = 0; r < d.replaceCount; r++) {
                const OreReplaceDef& rd = d.replace[r];
                f.replace.push_back({c.DefaultBlock(rd.place), c.DefaultBlock(rd.mayReplace0),
                                     rd.mayReplace1 ? c.DefaultBlock(rd.mayReplace1) : uint16_t(0xFFFF)});
            }
            features.push_back(std::move(f));
        }
        for (int i = 0; i < kOreRuleCount; i++) {
            const OreRuleDef& d = kOreRules[i];
            rules.push_back({&d, &features[static_cast<size_t>(d.feature)],
                             MixStafford13(static_cast<uint64_t>(i) * kSilver64 + 0x51ED)});
        }
    }

    void VanillaFeatures::Apply(int chunkX, int chunkZ, const uint8_t* biomes, uint16_t* blocks) const {
        const int baseX = chunkX * 16, baseZ = chunkZ * 16;
        for (size_t ri = 0; ri < rules.size(); ri++) {
            const Rule& rule = rules[ri];
            const OreRuleDef& def = *rule.def;
            const Feature& feature = *rule.feature;
            const int n = std::min(feature.count, 64);
            if (n <= 0)
                continue;
            for (int dx = -1; dx <= 1; dx++)
                for (int dz = -1; dz <= 1; dz++) {
                    const int ox = chunkX + dx, oz = chunkZ + dz;
                    Stream s(static_cast<uint64_t>(seed) ^ rule.key,
                             static_cast<uint64_t>(static_cast<uint32_t>(ox)) * 341873128712ull +
                                 static_cast<uint64_t>(static_cast<uint32_t>(oz)) * 132897987541ull +
                                 rule.key);

                    if (def.scatterDenominator > 1 && s.Below(def.scatterDenominator) >= def.scatterNumerator)
                        continue;
                    for (int it = 0; it < def.iterations; it++) {
                        int lx = 0, ly = 0, lz = 0;
                        for (const char* o = def.order; *o; o++) {
                            if (*o == 'x')
                                lx = Draw(s, def.x, false);
                            else if (*o == 'y')
                                ly = Draw(s, def.y, true);
                            else if (*o == 'z')
                                lz = Draw(s, def.z, false);
                        }

                        Blob b;
                        b.n = n;
                        const double angle = static_cast<double>(s.Float()) * kPi;
                        const double sx = std::sin(angle) * n / 8.0, sz = std::cos(angle) * n / 8.0;
                        const double x0 = lx + sx, x1 = lx - sx, z0 = lz + sz, z1 = lz - sz;
                        const double y0 = ly + s.Below(3) - 2, y1 = ly + s.Below(3) - 2;
                        b.minX = b.minZ = 1e30;
                        b.maxX = b.maxZ = -1e30;
                        for (int i = 0; i < n; i++) {
                            const double t = static_cast<double>(i) / n;
                            b.cx[i] = ox * 16 + x0 + (x1 - x0) * t;
                            b.cy[i] = y0 + (y1 - y0) * t;
                            b.cz[i] = oz * 16 + z0 + (z1 - z0) * t;
                            const double size = s.Double() * n / 16.0;
                            b.radius[i] = ((std::sin(i * kPi / n) + 1.0) * size + 1.0) / 2.0;
                            b.minX = std::min(b.minX, b.cx[i] - b.radius[i]);
                            b.maxX = std::max(b.maxX, b.cx[i] + b.radius[i]);
                            b.minZ = std::min(b.minZ, b.cz[i] - b.radius[i]);
                            b.maxZ = std::max(b.maxZ, b.cz[i] + b.radius[i]);
                        }
                        if (b.maxX < baseX || b.minX >= baseX + 16 || b.maxZ < baseZ || b.minZ >= baseZ + 16)
                            continue;
                        const int biomeX = std::clamp(ox * 16 + lx, baseX, baseX + 15) - baseX;
                        const int biomeZ = std::clamp(oz * 16 + lz, baseZ, baseZ + 15) - baseZ;
                        const int biome = biomes[biomeX * 16 + biomeZ];
                        if (!((def.biomeMask[biome >> 6] >> (biome & 63)) & 1))
                            continue;
                        const uint64_t blobKey = MixStafford13(
                            rule.key ^ MixStafford13(static_cast<uint64_t>(seed) +
                                                     static_cast<uint64_t>(ox * 16 + lx) * 73856093ull +
                                                     static_cast<uint64_t>(oz * 16 + lz) * 19349663ull +
                                                     static_cast<uint64_t>(ly + 64) * 83492791ull +
                                                     static_cast<uint64_t>(it)));
                        for (int i = 0; i < n; i++) {
                            const double r = b.radius[i];
                            const int minX = static_cast<int>(std::floor(b.cx[i] - r)),
                                      maxX = static_cast<int>(std::floor(b.cx[i] + r));
                            const int minY = static_cast<int>(std::floor(b.cy[i] - r)),
                                      maxY = static_cast<int>(std::floor(b.cy[i] + r));
                            const int minZ = static_cast<int>(std::floor(b.cz[i] - r)),
                                      maxZ = static_cast<int>(std::floor(b.cz[i] + r));
                            for (int wx = std::max(minX, baseX); wx <= std::min(maxX, baseX + 15); wx++) {
                                const double ddx = (wx + 0.5 - b.cx[i]) / r;
                                if (ddx * ddx >= 1.0)
                                    continue;
                                for (int wz = std::max(minZ, baseZ); wz <= std::min(maxZ, baseZ + 15); wz++) {
                                    const double ddz = (wz + 0.5 - b.cz[i]) / r;
                                    if (ddx * ddx + ddz * ddz >= 1.0)
                                        continue;
                                    uint16_t* col =
                                        blocks +
                                        static_cast<size_t>(((wx - baseX) * 16 + (wz - baseZ)) * kHeight);
                                    for (int wy = std::max(minY, kMinY);
                                         wy <= std::min(maxY, kMinY + kHeight - 1); wy++) {
                                        const double ddy = (wy + 0.5 - b.cy[i]) / r;
                                        if (ddx * ddx + ddy * ddy + ddz * ddz >= 1.0)
                                            continue;
                                        uint16_t& cell = col[wy - kMinY];
                                        const Replace* hit = nullptr;
                                        for (const Replace& rp : feature.replace)
                                            if (cell == rp.may0 || cell == rp.may1) {
                                                hit = &rp;
                                                break;
                                            }
                                        if (!hit)
                                            continue;
                                        if (feature.discard > 0.0f) {

                                            const int lx2 = wx - baseX, lz2 = wz - baseZ, ly2 = wy - kMinY;
                                            bool exposed = false;
                                            const auto air = [&](int ax, int ay, int az) {
                                                return ax >= 0 && ax < 16 && az >= 0 && az < 16 && ay >= 0 &&
                                                       ay < kHeight &&
                                                       blocks[static_cast<size_t>((ax * 16 + az) * kHeight +
                                                                                  ay)] == 0;
                                            };
                                            exposed = air(lx2 - 1, ly2, lz2) || air(lx2 + 1, ly2, lz2) ||
                                                      air(lx2, ly2 - 1, lz2) || air(lx2, ly2 + 1, lz2) ||
                                                      air(lx2, ly2, lz2 - 1) || air(lx2, ly2, lz2 + 1);
                                            if (exposed && HashFloat(blobKey, wx, wy, wz) < feature.discard)
                                                continue;
                                        }
                                        cell = hit->place;
                                    }
                                }
                            }
                        }
                    }
                }
        }
    }
}
