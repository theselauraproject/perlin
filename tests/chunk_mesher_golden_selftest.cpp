#include "client/systems/gfx/Tessellator.hpp"
#include "client/systems/blocks/BiomeTints.hpp"
#include "client/systems/blocks/BlockGeometry.hpp"
#include "client/systems/blocks/BlockLibrary.hpp"
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
#include "client/systems/player/BlockCollision.hpp"
#include "world/level/Level.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {
    struct MesherTestAccess {
        static void Prepare(BlockLibrary& library, BiomeTints& tints) {
            library.ready = true;
            library.geometry.blocks["test"];
            library.models.tiles = {0, 5, 6, 7, 8, 9, 10, 11};
            library.models.textures = {"stone", "grass_top", "oak_leaves", "water_still", "glass", "bamboo_leaf"};
            library.models.loaded = true;
            for (int tile : {9, 10, 11})
                library.glassTiles[tile];
            tints.ready.store(true);
        }
        static void AddVariant(BlockLibrary& library, std::vector<ModelQuad> quads) {
            library.models.variants.push_back(std::move(quads));
        }
    };

    const BlockAppearance& BlockLibrary::Look(int32_t id) const {
        static BlockAppearance table[16];
        static bool init = false;
        if (!init) {
            init = true;
            for (int i = 0; i < 16; i++) {
                BlockAppearance& a = table[i];
                a.top = 1 + i;
                a.side = 17 + i;
                a.bottom = 33 + i;
                a.texTop = "tex_top";
                a.texSide = "tex_side";
                a.texBottom = "tex_bottom";
            }
            table[2].tintTop = true;
            table[2].overlayFrac = 0.5f;
            table[2].overlayTile = 9;
            table[2].texTop = "grass_top";
            table[3].cutout = true;
            table[4].shape = BlockAppearance::Shape::Cross;
            table[4].texTop = "tallgrass";
            table[5].shape = BlockAppearance::Shape::Flat;
            table[6].shape = BlockAppearance::Shape::Wall;
            table[7].texTop = "water_still";
            table[7].texSide = "water_still";
            table[8].texTop = "oak_leaves";
            table[8].tintTop = table[8].tintSide = table[8].tintBottom = true;
            table[8].tintR = 0.4f;
        }
        return table[id & 15];
    }

    int BlockLibrary::GlassTile(int base, int mode, int mask) const { return base + mode * 16 + mask; }

    void BlockLibrary::TileUV(int tile, float& u0, float& v0, float& u1, float& v1) {
        u0 = static_cast<float>(tile & 15) / 16.0f;
        v0 = static_cast<float>(tile >> 4) / 16.0f;
        u1 = u0 + 1.0f / 16.0f;
        v1 = v0 + 1.0f / 16.0f;
    }

    BlockLibrary::TileAlpha BlockLibrary::AlphaOf(int tile) const {
        TileAlpha alpha;
        alpha.holes = tile % 5 == 0;
        alpha.translucent = tile % 7 == 3;
        alpha.saturation = 0.1f * static_cast<float>(tile % 4);
        return alpha;
    }

    float BlockLibrary::SaturationOf(int tile) const { return 0.1f * static_cast<float>(tile % 4); }

    std::string BlockLibrary::TilePathOf(int tile) const { return "tile" + std::to_string(tile); }

    namespace {
        std::unordered_map<uint16_t, ModelEntry>& Models() {
            static std::unordered_map<uint16_t, ModelEntry> models;
            return models;
        }
    }

    const ModelEntry* BlockGeometry::Find(uint16_t id) {
        const auto it = Models().find(id);
        return it == Models().end() ? nullptr : &it->second;
    }

    std::string BiomeTints::NameForId(int id) const { return "biome_" + std::to_string(id); }

    namespace {
        std::array<float, 3> Tint(const std::string& name, float scale) {
            const size_t h = std::hash<std::string>()(name);
            return {scale * static_cast<float>(h % 97) / 97.0f, scale * static_cast<float>(h % 89) / 89.0f,
                    scale * static_cast<float>(h % 83) / 83.0f};
        }
    }

    std::array<float, 3> BiomeTints::MapAverage(const std::string& name) const { return Tint(name, 1.0f); }
    std::array<float, 3> BiomeTints::Grass(const std::string& name) const { return Tint("g" + name, 1.0f); }
    std::array<float, 3> BiomeTints::Foliage(const std::string& name) const { return Tint("f" + name, 1.0f); }
    std::array<float, 3> BiomeTints::DryFoliage(const std::string& name) const { return Tint("d" + name, 1.0f); }
    std::array<float, 3> BiomeTints::Water(const std::string& name) const { return Tint("w" + name, 1.0f); }

    const physics::BlockShape& LevelCollision::ShapeOf(uint16_t) {
        static const physics::BlockShape shape;
        return shape;
    }
}

using namespace perlin;

namespace {
    constexpr uint64_t kGolden = 0x403f9b6d5843dec1ull;

    uint64_t Mix(uint64_t h, const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; i++) {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    template <class T> uint64_t Mix(uint64_t h, T value) { return Mix(h, &value, sizeof value); }

    uint64_t MixFloat(uint64_t h, float value) {
        return Mix(h, static_cast<int64_t>(std::llround(static_cast<double>(value) * 1024.0)));
    }

    uint64_t HashMesh(uint64_t h, Tessellator& mesh) {
        for (const Vertex& v : mesh.Vertices()) {
            for (float f : {v.x, v.y, v.z, v.u, v.v, v.entity, v.midU, v.midV, v.tangent})
                h = MixFloat(h, f);
            h = Mix(h, v.rgba);
            h = Mix(h, v.light);
            h = Mix(h, v.normal);
        }
        for (size_t layer = 0; layer < kRenderLayerCount; layer++) {
            const auto& indices = mesh.Indices(static_cast<RenderLayer>(layer));
            h = Mix(h, indices.size());
            h = Mix(h, indices.data(), indices.size() * sizeof(uint32_t));
        }
        for (const QuadInfo& q : mesh.Quads()) {
            for (float f : {q.bx, q.by, q.bz})
                h = MixFloat(h, f);
            h = Mix(h, q.block);
            h = Mix(h, q.layer);
        }
        for (const SectionVis& s : mesh.Visibility()) {
            h = Mix(h, s.sy);
            h = Mix(h, s.conn);
        }
        for (const SpecialBlock& s : mesh.Specials()) {
            h = Mix(h, s.x);
            h = Mix(h, s.y);
            h = Mix(h, s.z);
            h = Mix(h, s.id);
        }
        h = Mix(h, mesh.packVertices.data(), mesh.packVertices.size());
        return h;
    }

    ModelQuad FaceQuad(int dir, const float lo[3], const float hi[3], int tex, int tile, int cull, int tint,
                       bool shade, int light) {
        ModelQuad q{};
        q.dir = static_cast<uint8_t>(dir);
        q.shadeDir = static_cast<uint8_t>(dir);
        q.cull = static_cast<int8_t>(cull);
        q.tint = static_cast<int8_t>(tint);
        q.shade = shade ? 1 : 0;
        q.light = static_cast<uint8_t>(light);
        q.tex = static_cast<uint16_t>(tex);
        q.tile = tile;
        static const int kAxis[6] = {1, 1, 2, 2, 0, 0};
        const int n = kAxis[dir];
        const int a = (n + 1) % 3, b = (n + 2) % 3;
        const float plane = (dir & 1) ? hi[n] : lo[n];
        const float corner[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int i = 0; i < 4; i++) {
            q.pos[i][n] = plane;
            q.pos[i][a] = corner[i][0] ? hi[a] : lo[a];
            q.pos[i][b] = corner[i][1] ? hi[b] : lo[b];
            q.uv[i][0] = corner[i][0] * 16.0f;
            q.uv[i][1] = corner[i][1] * 16.0f;
        }
        return q;
    }

    ModelEntry Box(const float lo[3], const float hi[3], int tex, int tile, bool cullFaces, int tint, int light,
                   uint8_t offset) {
        ModelEntry entry;
        entry.direct = true;
        entry.offset = offset;
        for (int dir = 0; dir < 6; dir++)
            entry.quads.push_back(
                FaceQuad(dir, lo, hi, tex, tile, cullFaces ? dir : -1, tint, true, dir == 1 ? light : 0));
        return entry;
    }

    struct Fixture {
        BlockLibrary library;
        BiomeTints tints;
        std::vector<uint16_t> models;
        std::vector<uint16_t> named;

        Fixture() {
            MesherTestAccess::Prepare(library, tints);
            const float full0[3] = {0, 0, 0}, full1[3] = {1, 1, 1};
            const float slab1[3] = {1, 0.5f, 1};
            const float post0[3] = {0.375f, 0, 0.375f}, post1[3] = {0.625f, 1, 0.625f};
            auto& pal = BlockPalette::Instance();
            int network = 7000;
            for (int filler = 0; filler < 16; filler++)
                pal.Intern(6900 + filler);
            auto add = [&](const char* name, ModelEntry entry) {
                const uint16_t id = pal.Intern(network);
                pal.LearnName(network, name);
                network++;
                Models()[id] = std::move(entry);
                models.push_back(id);
            };
            add("test:full", Box(full0, full1, 1, 0, true, -1, 0, 0));
            add("test:tinted", Box(full0, full1, 2, 0, true, 0, 0, 0));
            add("test:slab", Box(full0, slab1, 1, 0, false, -1, 0, 0));
            add("test:post", Box(post0, post1, 3, 0, false, -1, 5, 0));
            add("test:offset", Box(post0, post1, 5, 0, false, 0, 0, 1));
            add("test:offset2", Box(post0, post1, 5, 0, false, 0, 0, 2));
            add("test:glass", Box(full0, full1, 4, 9, true, -1, 0, 0));
            add("test:glass2", Box(full0, full1, 4, 10, false, -1, 0, 0));
            add("test:tilebox", Box(full0, slab1, 0xFFFF, 11, true, -1, 0, 0));
            add("test:water", Box(full0, full1, 3, 0, true, 0, 0, 0));

            ModelEntry grouped;
            grouped.groups = {{{0, 1}, {1, 3}}, {{2, 1}}};
            MesherTestAccess::AddVariant(library, {FaceQuad(1, full0, full1, 1, 0, 1, -1, true, 0)});
            MesherTestAccess::AddVariant(library, {FaceQuad(1, full0, full1, 2, 0, 1, 0, true, 0)});
            MesherTestAccess::AddVariant(library, {FaceQuad(5, full0, slab1, 1, 0, -1, -1, true, 3)});
            add("test:grouped", std::move(grouped));
        }
    };

    void BuildWorld(Level& level, const Fixture& fixture, uint32_t seed) {
        uint32_t state = seed;
        const auto rnd = [&] {
            state = state * 1664525u + 1013904223u;
            return state >> 8;
        };
        auto& pal = BlockPalette::Instance();
        using SV = BlockPalette::StateValue;
        const auto I = [](int v) {
            SV s;
            s.kind = SV::Int;
            s.i = v;
            return s;
        };
        const auto S = [](const char* v) {
            SV s;
            s.kind = SV::String;
            s.s = v;
            return s;
        };
        using States = std::vector<std::pair<std::string, SV>>;
        std::vector<uint16_t> shaped;
        int network = 5000;
        const auto add = [&](const char* name, States states) {
            const uint16_t id = pal.Intern(network);
            pal.LearnState(network, name, std::move(states));
            network++;
            shaped.push_back(id);
        };
        add("minecraft:fire", {});
        for (int d = 0; d < 4; d++)
            for (int up = 0; up < 2; up++)
                add("minecraft:oak_stairs",
                    {{"weirdo_direction", I(d)},
                     {"upside_down_bit", I(up)},
                     {"minecraft:corner", S(d == 1 ? "inner_left" : d == 2 ? "outer_right" : "none")}});
        for (int m = 0; m < 4; m++)
            add("minecraft:oak_fence", {{"minecraft:connection_north", I(m & 1)},
                                        {"minecraft:connection_east", I(m >> 1)},
                                        {"minecraft:connection_south", I(m == 3)},
                                        {"minecraft:connection_west", I(0)}});
        for (const char* v : {"none", "short", "tall"})
            add("minecraft:cobblestone_wall", {{"wall_connection_type_east", S(v)},
                                               {"wall_connection_type_west", S("short")},
                                               {"wall_connection_type_south", S("none")},
                                               {"wall_connection_type_north", S(v)},
                                               {"wall_post_bit", I(1)}});
        for (int b = 0; b < 7; b += 2)
            add("minecraft:cake", {{"bite_counter", I(b)}});
        add("minecraft:stone_slab", {{"minecraft:vertical_half", S("top")}});
        add("minecraft:stone_slab", {{"minecraft:vertical_half", S("bottom")}});
        add("minecraft:oak_door", {{"upper_block_bit", I(1)}});
        add("minecraft:oak_door", {{"upper_block_bit", I(0)}});

        for (int cx = -1; cx <= 1; cx++)
            for (int cz = -1; cz <= 1; cz++) {
                level.EnsureChunk(cx, cz);
                for (int sub = 0; sub < 6; sub++) {
                    uint8_t cells[4096];
                    for (int i = 0; i < 4096; i++)
                        cells[i] = static_cast<uint8_t>(((i >> 3) + sub * 7 + cx * 3 + cz) % 5 < 2 ? 1 : 2);
                    level.SetBiomeSubChunk(cx, cz, sub - 1, cells);
                }
            }
        for (int x = -16; x < 32; x++)
            for (int z = -16; z < 32; z++) {
                const int top = 20 + static_cast<int>(rnd() % 7);
                for (int y = -10; y < top; y++)
                    level.SetBlock(x, y, z, static_cast<uint16_t>(1 + rnd() % 3));
                for (int y = top; y < top + 3; y++) {
                    const unsigned r = rnd() % 8;
                    if (r == 0)
                        level.SetBlock(x, y, z, static_cast<uint16_t>(4 + rnd() % 5));
                    else if (r == 1)
                        level.SetBlock(x, y, z, shaped[rnd() % shaped.size()]);
                    else if (r < 5)
                        level.SetBlock(x, y, z, fixture.models[rnd() % fixture.models.size()]);
                }
                if (rnd() % 9 == 0)
                    level.SetLiquid(x, top + 1, z, 7);
            }
    }

    uint64_t HashWorld(Fixture& fixture, uint32_t seed, int glassMode, bool withTints) {
        fixture.library.SetGlassMode(glassMode);
        Level level;
        BuildWorld(level, fixture, seed);
        MesherInvalidateCaches();
        Tessellator::SetCaptureQuadInfo(true);
        uint64_t h = 1469598103934665603ull;
        for (int cx = -1; cx <= 1; cx++)
            for (int cz = -1; cz <= 1; cz++) {
                Tessellator mesh;
                mesh.Begin();
                BuildChunkMesh(level, ChunkPos{cx, cz}, mesh, &fixture.library,
                               withTints ? &fixture.tints : nullptr, nullptr);
                mesh.End();
                h = HashMesh(h, mesh);
            }
        return h;
    }
}

int main(int argc, char** argv) {
    Fixture fixture;
    uint64_t total = 1469598103934665603ull;
    for (int mode = 0; mode <= 3; mode++)
        total = Mix(total, HashWorld(fixture, 12345u + static_cast<uint32_t>(mode), mode, mode != 1));
    if (argc > 1 && std::strcmp(argv[1], "--print") == 0) {
        std::printf("0x%016llxull\n", static_cast<unsigned long long>(total));
        return 0;
    }
    const bool ok = total == kGolden;
    std::printf("%s chunk mesher golden hash 0x%016llx\n", ok ? "ok  " : "FAIL",
                static_cast<unsigned long long>(total));
    if (!ok)
        std::printf("if the mesher output changed on purpose, run with --print and update kGolden\n");
    return ok ? 0 : 1;
}
