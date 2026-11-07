#include "world/vanilla/VanillaSurface.hpp"

#include "world/BlockCatalog.hpp"
#include "world/vanilla/VanillaAquifer.hpp"
#include "world/vanilla/VanillaCarvers.hpp"
#include "world/vanilla/VanillaFeatures.hpp"
#include "world/vanilla/VanillaMaterials.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <vector>
#include <memory>

namespace perlin::server::vanilla {

    namespace {
        uint32_t AxisDistance(int32_t t, int32_t lo, int32_t hi) {
            const int32_t over = t - hi, under = lo - t;
            uint32_t dist = over > 0 ? static_cast<uint32_t>(over) : (under > 0 ? static_cast<uint32_t>(under) : 0u);
            dist = std::min(dist, kMaxClimateDistance);
            return dist * dist;
        }

        struct BiomeNode {
            std::array<int32_t, 6> lo{}, hi{};
            uint64_t minOffset = 0;
            int first = 0, count = 0;
            int left = -1, right = -1;
        };

        struct BiomeTree {
            std::vector<int> order;
            std::vector<BiomeNode> nodes;

            int Build(int first, int count) {
                BiomeNode n;
                n.first = first;
                n.count = count;
                n.lo.fill(INT32_MAX);
                n.hi.fill(INT32_MIN);
                n.minOffset = UINT64_MAX;
                for (int k = first; k < first + count; k++) {
                    const int16_t* p = kBiomePointData + static_cast<size_t>(order[static_cast<size_t>(k)]) * kBiomePointStride;
                    for (int d = 0; d < 6; d++) {
                        n.lo[static_cast<size_t>(d)] = std::min<int32_t>(n.lo[static_cast<size_t>(d)], p[d * 2]);
                        n.hi[static_cast<size_t>(d)] = std::max<int32_t>(n.hi[static_cast<size_t>(d)], p[d * 2 + 1]);
                    }
                    const uint64_t off = static_cast<uint64_t>(std::abs(static_cast<int32_t>(p[12])));
                    n.minOffset = std::min(n.minOffset, off * off);
                }
                const int index = static_cast<int>(nodes.size());
                nodes.push_back(n);
                if (count <= 8)
                    return index;
                int axis = 0;
                int64_t spread = -1;
                for (int d = 0; d < 6; d++) {
                    int64_t cmin = INT64_MAX, cmax = INT64_MIN;
                    for (int k = first; k < first + count; k++) {
                        const int16_t* p = kBiomePointData + static_cast<size_t>(order[static_cast<size_t>(k)]) * kBiomePointStride;
                        const int64_t c = static_cast<int64_t>(p[d * 2]) + p[d * 2 + 1];
                        cmin = std::min(cmin, c);
                        cmax = std::max(cmax, c);
                    }
                    if (cmax - cmin > spread) {
                        spread = cmax - cmin;
                        axis = d;
                    }
                }
                auto begin = order.begin() + first, end = begin + count;
                std::sort(begin, end, [axis](int a, int b) {
                    const int16_t* pa = kBiomePointData + static_cast<size_t>(a) * kBiomePointStride;
                    const int16_t* pb = kBiomePointData + static_cast<size_t>(b) * kBiomePointStride;
                    const int ca = pa[axis * 2] + pa[axis * 2 + 1], cb = pb[axis * 2] + pb[axis * 2 + 1];
                    return ca != cb ? ca < cb : a < b;
                });
                const int half = count / 2;
                const int left = Build(first, half);
                const int right = Build(first + half, count - half);
                nodes[static_cast<size_t>(index)].left = left;
                nodes[static_cast<size_t>(index)].right = right;
                return index;
            }

            uint64_t Bound(const BiomeNode& n, const int32_t* t) const {
                uint64_t b = n.minOffset;
                for (int d = 0; d < 6; d++)
                    b += AxisDistance(t[d], n.lo[static_cast<size_t>(d)], n.hi[static_cast<size_t>(d)]);
                return b;
            }

            void Search(int index, const int32_t* t, uint64_t& best, int& foundIndex, int hint) const {
                const BiomeNode& n = nodes[static_cast<size_t>(index)];
                if (n.left < 0) {
                    for (int k = n.first; k < n.first + n.count; k++) {
                        const int i = order[static_cast<size_t>(k)];
                        const int16_t* p = kBiomePointData + static_cast<size_t>(i) * kBiomePointStride;
                        uint64_t fit = 0;
                        for (int d = 0; d < 6 && fit <= best; d++)
                            fit += AxisDistance(t[d], p[d * 2], p[d * 2 + 1]);
                        const uint64_t off = static_cast<uint64_t>(std::abs(static_cast<int32_t>(p[12])));
                        fit += off * off;
                        if (fit < best || (fit == best && foundIndex != hint && i < foundIndex)) {
                            best = fit;
                            foundIndex = i;
                        }
                    }
                    return;
                }
                const BiomeNode& a = nodes[static_cast<size_t>(n.left)];
                const BiomeNode& b = nodes[static_cast<size_t>(n.right)];
                const uint64_t ba = Bound(a, t), bb = Bound(b, t);
                if (ba <= bb) {
                    if (ba <= best)
                        Search(n.left, t, best, foundIndex, hint);
                    if (bb <= best)
                        Search(n.right, t, best, foundIndex, hint);
                } else {
                    if (bb <= best)
                        Search(n.right, t, best, foundIndex, hint);
                    if (ba <= best)
                        Search(n.left, t, best, foundIndex, hint);
                }
            }
        };

        const BiomeTree& Tree() {
            static const BiomeTree tree = [] {
                BiomeTree t;
                t.order.resize(static_cast<size_t>(kBiomePointCount));
                for (int i = 0; i < kBiomePointCount; i++)
                    t.order[static_cast<size_t>(i)] = i;
                t.nodes.reserve(static_cast<size_t>(kBiomePointCount));
                t.Build(0, kBiomePointCount);
                return t;
            }();
            return tree;
        }
    }

    int FindBiomePoint(const int32_t target[6], int hint) {
        const BiomeTree& tree = Tree();
        uint64_t best = 0xFFFFFFFFull - 1;
        int foundIndex = INT32_MAX;
        if (hint >= 0 && hint < kBiomePointCount) {
            const int16_t* p = kBiomePointData + static_cast<size_t>(hint) * kBiomePointStride;
            uint64_t fit = 0;
            for (int d = 0; d < 6; d++)
                fit += AxisDistance(target[d], p[d * 2], p[d * 2 + 1]);
            const uint64_t off = static_cast<uint64_t>(std::abs(static_cast<int32_t>(p[12])));
            fit += off * off;
            if (fit < best) {
                best = fit;
                foundIndex = hint;
            }
        } else {
            hint = -1;
        }
        tree.Search(0, target, best, foundIndex, hint);
        if (foundIndex == INT32_MAX) {
            uint32_t lbest = 0xFFFFFFFFu;
            foundIndex = 0;
            for (int i = 0; i < kBiomePointCount; i++) {
                const int16_t* p = kBiomePointData + static_cast<size_t>(i) * kBiomePointStride;
                uint32_t fit = 0;
                for (int d = 0; d < 6; d++)
                    fit += AxisDistance(target[d], p[d * 2], p[d * 2 + 1]);
                fit += static_cast<uint32_t>(std::abs(static_cast<int32_t>(p[12]))) * static_cast<uint32_t>(std::abs(static_cast<int32_t>(p[12])));
                if (fit < lbest) {
                    lbest = fit;
                    foundIndex = i;
                }
            }
        }
        return foundIndex;
    }

    int FindBiome(const int32_t target[6]) { return kBiomePointBiome[FindBiomePoint(target, -1)]; }

    int FindBiomeLinear(const int32_t target[6]) {
        uint32_t best = 0xFFFFFFFFu;
        int found = 0;
        for (int i = 0; i < kBiomePointCount; i++) {
            const int16_t* p = kBiomePointData + static_cast<size_t>(i) * kBiomePointStride;
            uint32_t fit = 0;
            for (int d = 0; d < 6 && fit < best; d++)
                fit += AxisDistance(target[d], p[d * 2], p[d * 2 + 1]);
            const uint32_t offset = static_cast<uint32_t>(std::abs(static_cast<int32_t>(p[12])));
            if (fit < best) {
                fit += offset * offset;
                if (fit < best) {
                    best = fit;
                    found = i;
                }
            }
        }
        return kBiomePointBiome[found];
    }

    VanillaClimate::VanillaClimate(DensityGraph& graph) {
        static const char* kIds[6] = {"minecraft:overworld/temperature", "minecraft:overworld/vegetation",
                                      "minecraft:overworld/continents",  "minecraft:overworld/erosion",
                                      "minecraft:overworld/depth",       "minecraft:overworld/ridges"};
        ok = true;
        for (int i = 0; i < 6; i++) {
            roots[static_cast<size_t>(i)] = graph.Load(kIds[i]);
            ok = ok && roots[static_cast<size_t>(i)] != nullptr;
        }
    }

    void VanillaClimate::Sample(int x, int y, int z, int32_t out[6]) const {
        for (size_t i = 0; i < 6; i++) {
            const float v = roots[i]->Eval(x, y, z);
            // (long)(v * 10000.0F): float product, truncated toward zero, NaN as 0
            const float scaled = v * 10000.0f;
            out[i] = scaled != scaled ? 0 : static_cast<int32_t>(std::clamp(scaled, -2.0e9f, 2.0e9f));
        }
    }

    void VanillaClimate::SampleColumn(int x, int z, const int* ys, int count, int32_t* out) const {
        for (size_t i = 0; i < 6; i++) {
            const bool flat = !roots[i]->yDependent;
            int32_t first = 0;
            for (int n = 0; n < count; n++) {
                if (flat && n > 0) {
                    out[n * 6 + static_cast<int>(i)] = first;
                    continue;
                }
                const float v = roots[i]->Eval(x, ys[n], z);
                const float scaled = v * 10000.0f;
                const int32_t q = scaled != scaled ? 0 : static_cast<int32_t>(std::clamp(scaled, -2.0e9f, 2.0e9f));
                out[n * 6 + static_cast<int>(i)] = q;
                first = q;
            }
        }
    }

    BiomeSite SiteOf(int worldX, int worldZ, int top) {
        // quart cells are 4 blocks; the biome of a column is read at its cell, at the height of the ground
        const int worldTop = kMinY + (top < 0 ? kSeaLevel - kMinY : top);
        return {(worldX >> 2) << 2, (worldTop >> 2) << 2, (worldZ >> 2) << 2};
    }

    namespace {
        constexpr uint8_t kNetworkBiome[] = {14, 47, 46, 45, 44, 24, 0, 43, 42, 40, 25, 6, 191, 184, 12, 26, 131, 185, 3, 30, 34, 5, 1, 186, 16, 4, 160, 132, 27, 29, 193, 36, 35, 21, 37, 2, 38, 182, 189, 11, 7, 140, 195, 32, 129, 155, 23, 48, 165, 163, 192, 183, 188, 187, 194, 190, 8, 178, 179, 180, 181, 9, 9, 9, 9, 9};
    }

    uint8_t NetworkBiome(int tableIndex) {
        return tableIndex >= 0 && tableIndex < static_cast<int>(sizeof(kNetworkBiome)) ? kNetworkBiome[tableIndex] : 1;
    }

    void VanillaSurface::StoreBiomes(const uint8_t* biomes, ChunkColumn& column) {
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                const uint8_t b = biomes[x * 16 + z];
                column.SetBiome(x, z, b, NetworkBiome(b));
            }
    }

    VanillaSurface::VanillaSurface() {
        const BlockCatalog& c = BlockCatalog::Instance();
        stone = c.DefaultBlock("stone");
        deepslate = c.DefaultBlock("deepslate");
        bedrock = c.DefaultBlock("bedrock");
        dirt = c.DefaultBlock("dirt");
        grass = c.DefaultBlock("grass_block");
        water = c.DefaultBlock("water");
        lava = c.DefaultBlock("lava");
        for (int i = 0; i < kBiomeCount; i++) {
            const BiomeMaterials& b = kBiomes[i];
            materials.push_back({c.DefaultBlock(b.top), c.DefaultBlock(b.mid), c.DefaultBlock(b.foundation),
                                 c.DefaultBlock(b.seaFloor), c.DefaultBlock(b.sea), b.seaFloorDepth,
                                 b.builder});
        }
    }

    int VanillaSurface::TopOf(const float* d) {
        for (int y = kHeight - 1; y >= 0; y--)
            if (d[y] > 0.0f)
                return y;
        return -1;
    }

    uint16_t VanillaSurface::BlockAt(const SurfaceMaterial& m, int idx, int top, bool solid) const {
        const int wy = kMinY + idx;
        if (idx == 0)
            return bedrock;
        if (!solid)
            return wy <= kSeaLevel ? m.sea : static_cast<uint16_t>(0);
        const int depth = top - idx;
        const int topWy = kMinY + top;
        uint16_t block = depth == 0 ? m.top : (depth <= kMidLayers ? m.mid : m.foundation);
        // plain stone goes to deepslate below y 0
        if (depth > kMidLayers && block == stone && wy < 0)
            block = deepslate;
        if (depth <= kMidLayers && topWy < kSeaLevel) {
            // under water: past the biome's sea floor depth the floor material replaces the skin; nearer the shore grass can't stay grass
            if (kSeaLevel - topWy > m.seaFloorDepth)
                block = m.seaFloor;
            else if (block == grass)
                block = dirt;
        }
        return block;
    }

    void VanillaSurface::Store(const uint16_t* blocks, ChunkColumn& column) {
        std::array<uint16_t, kCells> cells;
        for (int sec = 0; sec < kSections; sec++) {
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < 16; y++)
                        cells[static_cast<size_t>(CellIndex(x, y, z))] =
                            blocks[static_cast<size_t>((x * 16 + z) * kHeight + sec * 16 + y)];
            column.SetSection(sec, cells.data());
        }
    }

    void VanillaSurface::Fill(const float* density, const uint8_t* biomes, ChunkColumn& column,
                              const VanillaFeatures* features, int chunkX, int chunkZ,
                              const TerrainStages* stages) const {
        const AquiferFunctions* aquifers = stages ? stages->aquifers : nullptr;
        const VanillaMaterials* rules = stages ? stages->materials : nullptr;
        std::vector<uint16_t> blocks(static_cast<size_t>(16 * 16 * kHeight), 0);
        std::unique_ptr<VanillaAquifer> aquifer;
        if (aquifers && aquifers->ok)
            aquifer = std::make_unique<VanillaAquifer>(*aquifers, chunkX, chunkZ, kMinY, kHeight);
        std::vector<VanillaAquifer::Substance> kinds(static_cast<size_t>(kHeight));
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                const float* d = density + static_cast<size_t>((x * 16 + z) * kHeight);
                const SurfaceMaterial& m = materials[biomes[x * 16 + z]];
                uint16_t* out = blocks.data() + static_cast<size_t>((x * 16 + z) * kHeight);
                if (!aquifer) {
                    const int top = TopOf(d);
                    for (int y = 0; y < kHeight; y++)
                        out[y] = BlockAt(m, y, top, d[y] > 0.0f);
                    continue;
                }
                const int wx = chunkX * 16 + x, wz = chunkZ * 16 + z;
                int top = -1;
                for (int y = kHeight - 1; y >= 0; y--) {
                    kinds[static_cast<size_t>(y)] = aquifer->Compute(wx, kMinY + y, wz, static_cast<double>(d[y]));
                    if (top < 0 && kinds[static_cast<size_t>(y)] == VanillaAquifer::Substance::Solid)
                        top = y;
                }
                for (int y = 0; y < kHeight; y++) {
                    const VanillaAquifer::Substance k = kinds[static_cast<size_t>(y)];
                    if (rules) {
                        out[y] = k == VanillaAquifer::Substance::Solid   ? stone
                                 : k == VanillaAquifer::Substance::Water ? water
                                 : k == VanillaAquifer::Substance::Lava  ? lava
                                                                         : static_cast<uint16_t>(0);
                        continue;
                    }
                    if (k == VanillaAquifer::Substance::Solid || y == 0)
                        out[y] = BlockAt(m, y, top, true);
                    else if (k == VanillaAquifer::Substance::Water)
                        out[y] = kMinY + y <= kSeaLevel && y > top ? m.sea : water;
                    else if (k == VanillaAquifer::Substance::Lava)
                        out[y] = lava;
                    else
                        out[y] = 0;
                }
            }
        if (rules && aquifer) {
            rules->Apply(chunkX, chunkZ, biomes, blocks.data(), stages->temperature, stages->biome);
            if (stages->carvers)
                stages->carvers->Carve(chunkX, chunkZ, blocks.data(), *aquifer, rules, biomes, stages->temperature,
                                       stages->biome);
        }
        if (features)
            features->Apply(chunkX, chunkZ, biomes, blocks.data());
        Store(blocks.data(), column);
        StoreBiomes(biomes, column);
    }
}
