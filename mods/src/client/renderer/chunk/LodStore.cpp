#include "client/renderer/chunk/LodStore.hpp"

#include "client/renderer/block/BiomeTints.hpp"
#include "client/renderer/block/BlockLibrary.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace perlin {
    namespace {
        bool Has(const std::string& s, const char* part) {
            return s.contains(part);
        }
    }

    LodStore::Rgb LodStore::ColourOf(uint16_t id, int biome, bool top, const BlockLibrary& blocks,
                                     const BiomeTints& tints) {
        const uint32_t key =
            (static_cast<uint32_t>(id) << 9) | static_cast<uint32_t>(biome & 0xff) << 1 | (top ? 1u : 0u);
        const auto cached = colours.find(key);
        if (cached != colours.end())
            return cached->second;

        const BlockAppearance& look = blocks.Look(id);
        const int tile = look.top;
        const int px = BlockLibrary::TilePx();
        const std::vector<uint8_t>& atlas = blocks.AtlasPixels();
        float sum[3] = {0, 0, 0};
        float weight = 0;
        const size_t base = static_cast<size_t>(tile) * px * px * 4;
        if (tile > 0 && base + static_cast<size_t>(px) * px * 4 <= atlas.size()) {
            for (int i = 0; i < px * px; i++) {
                const uint8_t* p = &atlas[base + static_cast<size_t>(i) * 4];
                const float a = p[3] / 255.0f;
                sum[0] += p[0] * a;
                sum[1] += p[1] * a;
                sum[2] += p[2] * a;
                weight += a;
            }
        }
        float c[3] = {0.5f, 0.5f, 0.5f};
        if (weight > 0)
            for (int i = 0; i < 3; i++)
                c[i] = sum[i] / weight / 255.0f;

        const std::string name = BlockPalette::Instance().Name(id);
        if (top && Has(name, "red_mushroom_block")) {
            c[0] = 0.71f;
            c[1] = 0.16f;
            c[2] = 0.15f;
        } else if (top && Has(name, "brown_mushroom_block")) {
            c[0] = 0.58f;
            c[1] = 0.44f;
            c[2] = 0.32f;
        }
        std::array<float, 3> tint = {1, 1, 1};
        const std::string biomeName = tints.Ready() ? tints.NameForId(biome) : std::string();
        if (Has(name, "water")) {
            if (tints.Ready())
                tint = tints.Water(biomeName);
            c[0] = c[1] = c[2] = 1.0f;
        } else if (tints.Ready() && (Has(name, "leaves") || (Has(name, "vine") && !Has(name, "cave_vines") &&
                                                               !Has(name, "twisting") && !Has(name, "weeping")))) {
            const float hi = std::max({c[0], c[1], c[2]}), lo = std::min({c[0], c[1], c[2]});
            const bool coloured = (tile > 0 && blocks.SaturationOf(tile) > 0.25f) || (hi > 0.0f && (hi - lo) / hi > 0.18f) ||
                                  Has(name, "cherry") || Has(name, "azalea") || Has(name, "pale_oak");
            if (!coloured)
                tint = Has(name, "birch")      ? tints.MapAverage("birch")
                       : Has(name, "spruce")   ? tints.MapAverage("evergreen")
                       : Has(name, "mangrove") ? tints.MapAverage("mangrove_swamp_foliage")
                                               : tints.Foliage(biomeName);
        } else if (tints.Ready() && (name == "minecraft:grass_block" || name == "minecraft:short_grass" ||
                                     name == "minecraft:tall_grass" || name == "minecraft:fern" ||
                                     name == "minecraft:large_fern" || name == "minecraft:sugar_cane" ||
                                     name == "minecraft:reeds" || look.tintTop)) {
            tint = tints.Grass(biomeName);
        } else {
            tint = {look.tintR, look.tintG, look.tintB};
        }
        Rgb out;
        out.r = static_cast<uint8_t>(std::clamp(c[0] * tint[0], 0.0f, 1.0f) * 255.0f + 0.5f);
        out.g = static_cast<uint8_t>(std::clamp(c[1] * tint[1], 0.0f, 1.0f) * 255.0f + 0.5f);
        out.b = static_cast<uint8_t>(std::clamp(c[2] * tint[2], 0.0f, 1.0f) * 255.0f + 0.5f);
        colours[key] = out;
        return out;
    }

    bool LodStore::Capture(const Level& level, const ChunkPos& pos, const BlockLibrary& blocks,
                           const BiomeTints& tints) {
        LodColumn col;
        bool any = false;
        {
            std::lock_guard<std::mutex> lock(level.mutex);
            const auto it = level.chunks.find(pos);
            if (it == level.chunks.end())
                return false;
            const Chunk& chunk = it->second;
            int lo = 0, hi = 0;
            if (!chunk.YRange(lo, hi))
                return false;
            for (int cz = 0; cz < LodColumn::kCells; cz++)
                for (int cx = 0; cx < LodColumn::kCells; cx++) {
                    const int x = cx * LodColumn::kCellSize + LodColumn::kCellSize / 2;
                    const int z = cz * LodColumn::kCellSize + LodColumn::kCellSize / 2;
                    LodCell& cell = col.cells[static_cast<size_t>(cz) * LodColumn::kCells + cx];
                    int waterTop = Chunk::MIN_Y - 1;
                    uint16_t waterId = 0;
                    int floorY = lo - 1;
                    uint16_t floorId = 0;
                    for (int y = hi; y >= lo; y--) {
                        uint16_t id = chunk.Get(x, y, z);
                        const uint16_t liquid = chunk.GetLiquid(x, y, z);
                        const bool isWater =
                            liquid != 0 || (id != 0 && BlockPalette::Instance().Name(id).contains("water"));
                        if (isWater) {
                            if (waterTop < Chunk::MIN_Y) {
                                waterTop = y;
                                waterId = liquid != 0 ? liquid : id;
                            }
                            if (liquid != 0 && id != 0 &&
                                blocks.Look(id).shape == BlockAppearance::Shape::Cube &&
                                !blocks.Look(id).cutout) {
                                floorY = y;
                                floorId = id;
                                break;
                            }
                            continue;
                        }
                        if (id == 0)
                            continue;
                        const BlockAppearance& look = blocks.Look(id);
                        if (look.shape != BlockAppearance::Shape::Cube && !look.cutout)
                            continue;
                        if (look.shape != BlockAppearance::Shape::Cube)
                            continue;
                        floorY = y;
                        floorId = id;
                        break;
                    }
                    if (floorId == 0 && waterTop < Chunk::MIN_Y) {
                        cell.y = static_cast<int16_t>(Chunk::MIN_Y);
                        continue;
                    }
                    any = true;
                    const int biome = chunk.GetBiome(x, std::max(floorY, Chunk::MIN_Y), z);
                    if (waterTop >= Chunk::MIN_Y) {
                        const Rgb w = ColourOf(waterId, biome, true, blocks, tints);
                        Rgb f = floorId ? ColourOf(floorId, biome, true, blocks, tints) : Rgb{0, 0, 0};
                        const int depth = floorId ? std::max(1, waterTop - floorY) : 16;
                        const float a = std::min(0.85f, 0.45f + depth * 0.06f);
                        cell.r = static_cast<uint8_t>(w.r * a + f.r * (1 - a));
                        cell.g = static_cast<uint8_t>(w.g * a + f.g * (1 - a));
                        cell.b = static_cast<uint8_t>(w.b * a + f.b * (1 - a));
                        cell.y = static_cast<int16_t>(waterTop + 1);
                        cell.water = 1;
                        cell.depth = static_cast<uint8_t>(std::min(depth, 255));
                    } else {
                        const Rgb f = ColourOf(floorId, biome, true, blocks, tints);
                        cell.r = f.r;
                        cell.g = f.g;
                        cell.b = f.b;
                        cell.y = static_cast<int16_t>(floorY + 1);
                    }
                }
        }
        if (!any)
            return false;
        auto found = columns.find(pos);
        if (found != columns.end()) {
            bool same = true;
            for (size_t i = 0; i < col.cells.size() && same; i++) {
                const LodCell &a = found->second.cells[i], &b = col.cells[i];
                same = a.y == b.y && a.r == b.r && a.g == b.g && a.b == b.b && a.water == b.water;
            }
            if (same)
                return false;
            found->second = col;
            return true;
        }
        columns.emplace(pos, col);
        tiles[TileKeyOf(TileOf(pos.x), TileOf(pos.z))]++;
        return true;
    }

    void LodStore::Evict(int centreX, int centreZ, int radius, std::vector<ChunkPos>& removed) {
        std::vector<int64_t> outside;
        for (const auto& [key, count] : tiles) {
            const int tx = static_cast<int>(key >> 32), tz = static_cast<int>(static_cast<uint32_t>(key));
            const int x0 = tx * kTileChunks, z0 = tz * kTileChunks;
            const bool inside = x0 >= centreX - radius && x0 + kTileChunks - 1 <= centreX + radius &&
                                z0 >= centreZ - radius && z0 + kTileChunks - 1 <= centreZ + radius;
            if (inside)
                continue;
            outside.push_back(key);
        }
        for (int64_t key : outside) {
            const int tx = static_cast<int>(key >> 32), tz = static_cast<int>(static_cast<uint32_t>(key));
            for (int dz = 0; dz < kTileChunks; dz++)
                for (int dx = 0; dx < kTileChunks; dx++) {
                    const ChunkPos p{tx * kTileChunks + dx, tz * kTileChunks + dz};
                    if (std::abs(p.x - centreX) <= radius && std::abs(p.z - centreZ) <= radius)
                        continue;
                    if (columns.erase(p)) {
                        removed.push_back(p);
                        DropFromTile(p);
                    }
                }
        }
    }

    bool LodStore::BuildTile(int tileX, int tileZ, int stride,
                             const std::function<bool(const ChunkPos&)>& hidden, Tessellator& out) const {
        constexpr int kSide = LodStore::kTileChunks * LodColumn::kCells;
        const int cellsPerSide = kSide / stride;
        const int blockSize = LodColumn::kCellSize * stride;
        struct Cell {
            int y;
            uint8_t r, g, b;
            uint8_t water;
            bool present;
        };
        std::vector<Cell> grid(static_cast<size_t>(cellsPerSide) * cellsPerSide, Cell{0, 0, 0, 0, 0, false});
        const int chunkX0 = tileX * kTileChunks, chunkZ0 = tileZ * kTileChunks;
        bool anyDraw = false;
        for (int dz = 0; dz < kTileChunks; dz++)
            for (int dx = 0; dx < kTileChunks; dx++) {
                const ChunkPos cp{chunkX0 + dx, chunkZ0 + dz};
                const LodColumn* col = Find(cp);
                if (!col || hidden(cp))
                    continue;
                for (int cz = 0; cz < LodColumn::kCells; cz++)
                    for (int cx = 0; cx < LodColumn::kCells; cx++) {
                        const LodCell& s = col->cells[static_cast<size_t>(cz) * LodColumn::kCells + cx];
                        if (s.y <= Chunk::MIN_Y)
                            continue;
                        const int gx = (dx * LodColumn::kCells + cx) / stride;
                        const int gz = (dz * LodColumn::kCells + cz) / stride;
                        Cell& cell = grid[static_cast<size_t>(gz) * cellsPerSide + gx];
                        if (!cell.present || s.y > cell.y) {
                            cell = {s.y, s.r, s.g, s.b, s.water, true};
                        }
                        anyDraw = true;
                    }
            }
        if (!anyDraw)
            return false;

        out.Begin();
        out.SetLayer(RenderLayer::Solid);
        float u0, v0, u1, v1;
        BlockLibrary::TileUV(0, u0, v0, u1, v1);
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        const float x0 = static_cast<float>(chunkX0 * 16), z0 = static_cast<float>(chunkZ0 * 16);
        const float size = static_cast<float>(blockSize);
        auto shade = [](const Cell& c, float k, float col[4][3]) {
            for (int i = 0; i < 4; i++) {
                col[i][0] = c.r / 255.0f * k;
                col[i][1] = c.g / 255.0f * k;
                col[i][2] = c.b / 255.0f * k;
            }
        };
        const auto similar = [](const Cell& a, const Cell& b) {
            return b.present && a.y == b.y && a.water == b.water && std::abs(a.r - b.r) <= 2 &&
                   std::abs(a.g - b.g) <= 2 && std::abs(a.b - b.b) <= 2;
        };
        for (int gz = 0; gz < cellsPerSide; gz++)
            for (int gx = 0; gx < cellsPerSide;) {
                const Cell& c = grid[static_cast<size_t>(gz) * cellsPerSide + gx];
                if (!c.present) {
                    gx++;
                    continue;
                }
                int run = 1;
                while (gx + run < cellsPerSide &&
                       similar(c, grid[static_cast<size_t>(gz) * cellsPerSide + gx + run]))
                    run++;
                const float xa = x0 + gx * size, xb = xa + size * static_cast<float>(run),
                            za = z0 + gz * size, zb = za + size;
                const float y = static_cast<float>(c.y) - (c.water ? 0.11f : 0.0f);
                float col[4][3];
                const float v[4][3] = {{xa, y, za}, {xb, y, za}, {xb, y, zb}, {xa, y, zb}};
                shade(c, 1.0f, col);
                out.AddQuad(v, col, uv, false, 0.0f, 15.0f);
                gx += run;
            }
        for (int gz = 0; gz < cellsPerSide; gz++)
            for (int gx = 0; gx < cellsPerSide; gx++) {
                const Cell& c = grid[static_cast<size_t>(gz) * cellsPerSide + gx];
                if (!c.present)
                    continue;
                const float xa = x0 + gx * size, xb = xa + size, za = z0 + gz * size, zb = za + size;
                const float y = static_cast<float>(c.y) - (c.water ? 0.11f : 0.0f);
                float col[4][3];
                static const int kDir[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
                static const float kShade[4] = {0.8f, 0.8f, 0.65f, 0.65f};
                for (int d = 0; d < 4; d++) {
                    const int nx = gx + kDir[d][0], nz = gz + kDir[d][1];
                    int ny;
                    if (nx < 0 || nz < 0 || nx >= cellsPerSide || nz >= cellsPerSide) {
                        const int cxw = chunkX0 * LodColumn::kCells + nx * stride;
                        const int czw = chunkZ0 * LodColumn::kCells + nz * stride;
                        const int ccx = cxw >= 0 ? cxw / LodColumn::kCells
                                                 : -((-cxw + LodColumn::kCells - 1) / LodColumn::kCells);
                        const int ccz = czw >= 0 ? czw / LodColumn::kCells
                                                 : -((-czw + LodColumn::kCells - 1) / LodColumn::kCells);
                        const ChunkPos np{ccx, ccz};
                        const LodColumn* ncol = Find(np);
                        if (!ncol) {
                            ny = c.y - 24;
                        } else if (hidden(np)) {
                            continue;
                        } else {
                            const int lx = cxw - ccx * LodColumn::kCells, lz = czw - ccz * LodColumn::kCells;
                            const LodCell& n = ncol->cells[static_cast<size_t>(lz) * LodColumn::kCells + lx];
                            ny = n.y <= Chunk::MIN_Y ? c.y - 24 : n.y;
                        }
                    } else {
                        const Cell& n = grid[static_cast<size_t>(nz) * cellsPerSide + nx];
                        if (!n.present) {
                            ny = c.y - 24;
                        } else {
                            ny = n.y;
                        }
                    }
                    if (ny >= c.y)
                        continue;
                    const float yt = y, yb = static_cast<float>(std::max(ny, c.y - 64));
                    float v[4][3];
                    if (d == 0) {
                        const float q[4][3] = {{xb, yt, za}, {xa, yt, za}, {xa, yb, za}, {xb, yb, za}};
                        std::copy(&q[0][0], &q[0][0] + 12, &v[0][0]);
                    } else if (d == 1) {
                        const float q[4][3] = {{xa, yt, zb}, {xb, yt, zb}, {xb, yb, zb}, {xa, yb, zb}};
                        std::copy(&q[0][0], &q[0][0] + 12, &v[0][0]);
                    } else if (d == 2) {
                        const float q[4][3] = {{xa, yt, za}, {xa, yt, zb}, {xa, yb, zb}, {xa, yb, za}};
                        std::copy(&q[0][0], &q[0][0] + 12, &v[0][0]);
                    } else {
                        const float q[4][3] = {{xb, yt, zb}, {xb, yt, za}, {xb, yb, za}, {xb, yb, zb}};
                        std::copy(&q[0][0], &q[0][0] + 12, &v[0][0]);
                    }
                    shade(c, kShade[d], col);
                    out.AddQuad(v, col, uv, false, 0.0f, 15.0f);
                }
            }
        out.End();
        return !out.Empty();
    }
}
