// Perlin --meshbench [radius] [rounds]: times the chunk mesher on generated terrain with no window and no gpu.
// Hills with caves, ore-like pockets, trees, water and a snow layer, so the scan, lighting and face paths all work.
// Block ids are arbitrary and no resource pack is loaded, so this measures the mesher's core (fill, classify,
// connectivity, lighting, faces), not textured special-block models.
#include "client/renderer/chunk/MeshBench.hpp"

#include "client/renderer/Tessellator.hpp"
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "util/Resources.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/Level.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

namespace perlin {
    namespace {
        uint32_t Hash(int x, int y, int z) {
            uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
                         static_cast<uint32_t>(z) * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return h ^ (h >> 16);
        }
        float Value(float x, float z) {
            const int ix = static_cast<int>(std::floor(x)), iz = static_cast<int>(std::floor(z));
            const float fx = x - static_cast<float>(ix), fz = z - static_cast<float>(iz);
            const auto v = [&](int a, int b) {
                return static_cast<float>(Hash(a, 0, b) & 0xFFFF) / 65535.0f;
            };
            const float sx = fx * fx * (3 - 2 * fx), sz = fz * fz * (3 - 2 * fz);
            const float a = v(ix, iz) + (v(ix + 1, iz) - v(ix, iz)) * sx;
            const float b = v(ix, iz + 1) + (v(ix + 1, iz + 1) - v(ix, iz + 1)) * sx;
            return a + (b - a) * sz;
        }
        int Height(int x, int z) {
            return 62 +
                   static_cast<int>(Value(x / 40.0f, z / 40.0f) * 48.0f + Value(x / 9.0f, z / 9.0f) * 6.0f);
        }
        // palette ids of real block names, so name-keyed memos behave as in game
        uint16_t kStone, kDirt, kGrass, kSnow, kLog, kLeaves, kOre, kWater;
        void InternBlocks() {
            BlockPalette& pal = BlockPalette::Instance();
            const auto id = [&](int32_t net, const char* name) {
                const uint16_t local = pal.Intern(net);
                pal.LearnName(net, name);
                return local;
            };
            kStone = id(-900001, "minecraft:stone");
            kDirt = id(-900002, "minecraft:dirt");
            kGrass = id(-900003, "minecraft:grass_block");
            kSnow = id(-900004, "minecraft:snow");
            kLog = id(-900005, "minecraft:oak_log");
            kLeaves = id(-900006, "minecraft:oak_leaves");
            kOre = id(-900007, "minecraft:iron_ore");
            kWater = id(-900008, "minecraft:water");
        }

        uint16_t BlockAt(int x, int y, int z) {
            const int h = Height(x, z);
            if (y < -64)
                return 0;
            if (y <= -60)
                return kStone;
            if (y < h) {
                // caves: worm-ish tunnels from 3d value noise
                const float c = Value(x / 11.0f + y * 0.37f, z / 11.0f - y * 0.21f);
                if (y < h - 6 && c > 0.72f)
                    return 0;
                if ((Hash(x, y, z) & 63) == 0)
                    return kOre;
                return y < h - 3 ? kStone : kDirt;
            }
            if (y == h)
                return h > 90 ? kSnow : kGrass;
            if (y < 64)
                return kWater;
            // trees on grass, every so often
            for (int tx = x - 2; tx <= x + 2; tx++)
                for (int tz = z - 2; tz <= z + 2; tz++) {
                    if ((Hash(tx, 7, tz) % 97) != 0)
                        continue;
                    const int th = Height(tx, tz);
                    if (th < 64)
                        continue;
                    if (tx == x && tz == z && y > th && y <= th + 5)
                        return kLog;
                    if (y >= th + 4 && y <= th + 7 && std::abs(tx - x) + std::abs(tz - z) <= 3)
                        return kLeaves;
                }
            return 0;
        }
    } // namespace

    int RunMeshBench(int radius, int rounds) {
        // light setup as LevelRenderer does it, so the registry caches like it does in game
        InternBlocks();
        LightRegistry& registry = LightRegistry::Instance();
        if (const std::string rules = resources::ReadText("light.json"); !rules.empty())
            registry.LoadRules(rules);
        registry.SetDerivedOpacity(
            [](uint16_t id) { return id == 0 || id == kWater || id == kLeaves ? 1 : 15; });
        registry.SetOcclusion([](uint16_t id, FaceOcclusion& out) { return BlockFaceOcclusion(id, out); });
        Level level;
        const int r = radius + 1; // a ring of neighbours so edge chunks see real borders
        std::vector<uint16_t> cells(4096);
        for (int cx = -r; cx <= r; cx++)
            for (int cz = -r; cz <= r; cz++) {
                level.EnsureChunk(cx, cz);
                for (int sy = -4; sy < 20; sy++) {
                    bool any = false;
                    for (int x = 0; x < 16; x++)
                        for (int z = 0; z < 16; z++)
                            for (int y = 0; y < 16; y++) {
                                const uint16_t id = BlockAt(cx * 16 + x, sy * 16 + y, cz * 16 + z);
                                cells[static_cast<size_t>(x * 256 + z * 16 + y)] = id;
                                any |= id != 0;
                            }
                    if (any)
                        level.SetSubChunk(cx, cz, sy, cells.data());
                }
                level.MarkComplete(cx, cz);
            }
        std::vector<ChunkPos> todo;
        for (int cx = -radius; cx <= radius; cx++)
            for (int cz = -radius; cz <= radius; cz++)
                todo.push_back({cx, cz});

        // warm-up: caches and allocations
        for (const ChunkPos& p : todo) {
            Tessellator t;
            t.Begin();
            BuildChunkMesh(level, p, t, nullptr, nullptr);
            t.End();
        }
        std::vector<double> times;
        size_t vertices = 0;
        uint64_t checksum = 1469598103934665603ull;
        for (int round = 0; round < rounds; round++)
            for (const ChunkPos& p : todo) {
                Tessellator t;
                const auto t0 = std::chrono::steady_clock::now();
                t.Begin();
                BuildChunkMesh(level, p, t, nullptr, nullptr);
                t.End();
                times.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                vertices += t.Vertices().size();
                if (round == 0) {
                    // fnv-1a over the vertex bytes: an optimization must leave this unchanged
                    const auto* bytes = reinterpret_cast<const uint8_t*>(t.Vertices().data());
                    const size_t n = t.Vertices().size() * sizeof(t.Vertices()[0]);
                    for (size_t i = 0; i < n; i++)
                        checksum = (checksum ^ bytes[i]) * 1099511628211ull;
                }
            }
        std::sort(times.begin(), times.end());
        double sum = 0;
        for (double v : times)
            sum += v;
        std::printf("[meshbench] %zu chunks x %d rounds: mean %.3f ms, median %.3f ms, p90 %.3f ms, "
                    "%.0f vertices/chunk\n",
                    todo.size(), rounds, sum / times.size(), times[times.size() / 2],
                    times[times.size() * 9 / 10],
                    static_cast<double>(vertices) / static_cast<double>(times.size()));
        MeshProfilePrint();

        // partial rebuilds: after each kind of edit, a rebuild that copies unchanged sections must equal a full
        // rebuild byte for byte
        const auto same = [](const Tessellator& a, const Tessellator& b) {
            if (a.Vertices().size() != b.Vertices().size() ||
                std::memcmp(a.Vertices().data(), b.Vertices().data(),
                            a.Vertices().size() * sizeof(a.Vertices()[0])))
                return false;
            for (int l = 0; l < kRenderLayerCount; l++)
                if (a.Indices(static_cast<RenderLayer>(l)) != b.Indices(static_cast<RenderLayer>(l)))
                    return false;
            if (a.Sections().size() != b.Sections().size())
                return false;
            for (size_t i = 0; i < a.Sections().size(); i++) {
                const MeshSection &x = a.Sections()[i], &y = b.Sections()[i];
                if (x.sy != y.sy || x.start != y.start || x.count != y.count || x.faces != y.faces)
                    return false;
            }
            return true;
        };
        struct Edit {
            const char* what;
            int x, z; // world
            int y;    // < -1000: the surface + (y + 2000)
            uint16_t id;
        };
        const ChunkPos col{0, 0};
        const int sx = 5, sz = 7;
        const Edit edits[] = {{"block on the surface", sx, sz, -2000 + 1, kStone},
                              {"block removed at the surface", sx, sz, -2000 + 1, 0},
                              {"block dug a section down", sx + 3, sz, -2000 - 20, 0},
                              {"block dug deep", sx + 1, sz + 2, -40, 0},
                              {"shaft opened down to a cave", sx + 4, sz + 4, 0, 0},
                              {"shaft capped (relights below)", sx + 4, sz + 4, -2000 + 1, kStone},
                              {"block at the chunk edge", 15, sz, -2000 + 1, kLog},
                              {"neighbour chunk edit at the border", 16, sz, -2000 + 1, kLog},
                              {"water placed", sx + 2, sz + 5, -2000 + 1, kWater},
                              {"leaves placed", sx + 6, sz + 1, -2000 + 2, kLeaves}};
        MeshReuse reuse;
        auto prev = std::make_shared<Tessellator>();
        prev->Begin();
        BuildChunkMesh(level, col, *prev, nullptr, nullptr, &reuse);
        prev->End();
        reuse.mesh = prev;
        int failures = 0;
        double fullMs = 0, partialMs = 0;
        for (const Edit& e : edits) {
            int y = e.y;
            if (y < -1000)
                y = Height(e.x, e.z) + (y + 2000);
            if (e.y == 0) {
                // a vertical shaft from the surface down 40 blocks
                for (int d = 0; d < 40; d++)
                    level.SetBlock(e.x, Height(e.x, e.z) - d, e.z, 0);
            } else {
                level.SetBlock(e.x, y, e.z, e.id);
            }
            Tessellator full;
            auto t0 = std::chrono::steady_clock::now();
            full.Begin();
            BuildChunkMesh(level, col, full, nullptr, nullptr);
            full.End();
            fullMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            auto next = std::make_shared<Tessellator>();
            t0 = std::chrono::steady_clock::now();
            next->Begin();
            BuildChunkMesh(level, col, *next, nullptr, nullptr, &reuse);
            next->End();
            partialMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const bool ok = same(full, *next);
            failures += ok ? 0 : 1;
            std::printf("[meshbench] %-38s %s (%d sections rebuilt, %d copied)\n", e.what,
                        ok ? "same" : "DIFFERS", reuse.rebuilt, reuse.copied);
            reuse.mesh = next;
        }
        std::printf("[meshbench] edits: full rebuild %.3f ms, partial %.3f ms per edit\n",
                    fullMs / std::size(edits), partialMs / std::size(edits));
        if (failures)
            std::printf("FAIL partial rebuild differs from full rebuild in %d case(s)\n", failures);
        if (failures)
            return 1;
        std::printf("RESULT mesh=%.3f checksum=%016llx\n", sum / times.size(),
                    static_cast<unsigned long long>(checksum));
        return 0;
    }
} // namespace perlin
