#include "world/vanilla/VanillaSurface.hpp"

#include "world/BlockCatalog.hpp"
#include "world/vanilla/VanillaAquifer.hpp"
#include "world/vanilla/VanillaFeatures.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace perlin::server::vanilla {

    int FindBiome(const int32_t target[6]) {
        uint32_t best = 0xFFFFFFFFu;
        int found = 0;
        for (int i = 0; i < kBiomePointCount; i++) {
            const int16_t* p = kBiomePointData + static_cast<size_t>(i) * kBiomePointStride;
            uint32_t fit = 0;
            for (int d = 0; d < 6 && fit < best; d++) {
                const int32_t lo = p[d * 2], hi = p[d * 2 + 1];
                const int32_t t = target[d];
                const int32_t over = t - hi, under = lo - t;
                uint32_t dist =
                    over > 0 ? static_cast<uint32_t>(over) : (under > 0 ? static_cast<uint32_t>(under) : 0u);
                dist = std::min(dist, kMaxClimateDistance);
                fit += dist * dist;
            }
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

    BiomeSite SiteOf(int worldX, int worldZ, int top) {
        // quart cells are 4 blocks; the biome of a column is read at its cell, at the height of the ground
        const int worldTop = kMinY + (top < 0 ? kSeaLevel - kMinY : top);
        return {(worldX >> 2) << 2, (worldTop >> 2) << 2, (worldZ >> 2) << 2};
    }

    namespace {
        constexpr uint8_t kNetworkBiome[] = {14, 47, 46, 45, 44, 24, 0, 43, 42, 40, 25, 6, 191, 184, 12, 26, 131, 185, 3, 30, 34, 5, 1, 186, 16, 4, 160, 132, 27, 29, 193, 36, 35, 21, 37, 2, 38, 182, 189, 11, 7, 140, 195, 32, 129, 155, 23, 48, 165, 163, 192, 183, 188, 187, 194, 190};
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
                              const AquiferFunctions* aquifers) const {
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
        if (features)
            features->Apply(chunkX, chunkZ, biomes, blocks.data());
        Store(blocks.data(), column);
        StoreBiomes(biomes, column);
    }
}
