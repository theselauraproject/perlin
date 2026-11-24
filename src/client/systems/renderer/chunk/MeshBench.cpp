#include "client/systems/renderer/chunk/MeshBench.hpp"

#include "client/systems/gfx/Tessellator.hpp"
#include <string>
#include <unordered_map>
#include "client/systems/blocks/BiomeTints.hpp"
#include "client/systems/blocks/BlockLibrary.hpp"
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
#include "util/Resources.hpp"
#include "util/ResourcePaths.hpp"
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
#include <array>
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
        uint16_t kStone, kDirt, kGrass, kSnow, kLog, kLeaves, kOre, kWater;
        void InternBlocks(bool real) {
            BlockPalette& pal = BlockPalette::Instance();
            std::unordered_map<std::string, int32_t> hashOf;
            if (real)
                for (const auto& [hash, name] : pal.OneStatePerName())
                    hashOf.emplace(name, hash);
            int32_t fake = -900001;
            const auto id = [&](const char* name) {
                if (const auto it = hashOf.find(name); it != hashOf.end())
                    return pal.Intern(it->second);
                const int32_t net = fake--;
                const uint16_t local = pal.Intern(net);
                pal.LearnName(net, name);
                return local;
            };
            kStone = id("minecraft:stone");
            kDirt = id("minecraft:dirt");
            kGrass = id("minecraft:grass_block");
            kSnow = id("minecraft:snow");
            kLog = id("minecraft:oak_log");
            kLeaves = id("minecraft:oak_leaves");
            kOre = id("minecraft:iron_ore");
            kWater = id("minecraft:water");
        }
    }
    namespace {
        uint16_t BlockAt(int x, int y, int z) {
            const int h = Height(x, z);
            if (y < -64)
                return 0;
            if (y <= -60)
                return kStone;
            if (y < h) {
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
    }

    namespace {
        // Merging exactness: every point of every face of the unmerged mesh is drawn by the merged mesh
        // with the same colour, light, AO and texture position (the merged mesh is what the GPU
        // interpolates, so this is the pixel-exact check for the face merge).
        struct Sampled {
            float rgba[4], light[2], ao, u, v;
        };

        bool Interpolate(const Vertex& a, const Vertex& b, const Vertex& c, const float p[3], int axis,
                         Sampled& out) {
            const int i = axis == 0 ? 1 : 0, j = axis == 2 ? 1 : 2;
            const float* pa = &a.x;
            const float* pb = &b.x;
            const float* pc = &c.x;
            const float d = (pb[i] - pa[i]) * (pc[j] - pa[j]) - (pc[i] - pa[i]) * (pb[j] - pa[j]);
            if (std::fabs(d) < 1e-9f || std::fabs(pa[axis] - p[axis]) > 1e-4f)
                return false;
            const float wb = ((p[i] - pa[i]) * (pc[j] - pa[j]) - (pc[i] - pa[i]) * (p[j] - pa[j])) / d;
            const float wc = ((pb[i] - pa[i]) * (p[j] - pa[j]) - (p[i] - pa[i]) * (pb[j] - pa[j])) / d;
            const float wa = 1.0f - wb - wc;
            constexpr float kIn = -1e-5f;
            if (wa < kIn || wb < kIn || wc < kIn)
                return false;
            const auto mix = [&](float x, float y, float z) { return wa * x + wb * y + wc * z; };
            for (int k = 0; k < 4; k++)
                out.rgba[k] = mix(static_cast<float>((a.rgba >> (8 * k)) & 0xFF), static_cast<float>((b.rgba >> (8 * k)) & 0xFF),
                                  static_cast<float>((c.rgba >> (8 * k)) & 0xFF));
            for (int k = 0; k < 2; k++)
                out.light[k] = mix(static_cast<float>((a.light >> (8 * k)) & 0xFF), static_cast<float>((b.light >> (8 * k)) & 0xFF),
                                   static_cast<float>((c.light >> (8 * k)) & 0xFF));
            const auto fr = [](float t) { return t - std::floor(t); };
            out.ao = mix(fr(a.tangent), fr(b.tangent), fr(c.tangent));
            out.u = mix(a.u, b.u, c.u);
            out.v = mix(a.v, b.v, c.v);
            return true;
        }

        int CheckMergeExact(const Tessellator& flat, const Tessellator& merged, int& checkedPoints) {
            int bad = 0;
            for (int l = 0; l < kRenderLayerCount; l++) {
                const auto& fi = flat.Indices(static_cast<RenderLayer>(l));
                const auto& mi = merged.Indices(static_cast<RenderLayer>(l));
                const auto& fv = flat.Vertices();
                const auto& mv = merged.Vertices();
                // Merged triangles by plane: axis, coordinate, normal and block.
                std::unordered_map<uint64_t, std::vector<size_t>> byPlane;
                const auto key = [](const Vertex& a, const Vertex& b, const Vertex& c, int& axis) -> uint64_t {
                    axis = a.x == b.x && a.x == c.x ? 0 : a.y == b.y && a.y == c.y ? 1 : a.z == b.z && a.z == c.z ? 2 : -1;
                    if (axis < 0)
                        return 0;
                    const float at = (&a.x)[axis];
                    uint32_t bits;
                    std::memcpy(&bits, &at, 4);
                    return (static_cast<uint64_t>(bits) << 32) ^ (static_cast<uint64_t>(a.normal) * 31) ^
                           static_cast<uint64_t>(axis + 1) ^ (static_cast<uint64_t>(a.entity) << 40);
                };
                for (size_t t = 0; t + 2 < mi.size(); t += 3) {
                    int axis = -1;
                    const uint64_t k = key(mv[mi[t]], mv[mi[t + 1]], mv[mi[t + 2]], axis);
                    if (axis >= 0)
                        byPlane[k].push_back(t);
                }
                for (size_t t = 0; t + 2 < fi.size(); t += 3) {
                    const Vertex &a = fv[fi[t]], &b = fv[fi[t + 1]], &c = fv[fi[t + 2]];
                    int axis = -1;
                    const uint64_t k = key(a, b, c, axis);
                    if (axis < 0)
                        continue;
                    const auto found = byPlane.find(k);
                    static constexpr float kWeights[4][3] = {
                        {0.6f, 0.2f, 0.2f}, {0.2f, 0.6f, 0.2f}, {0.2f, 0.2f, 0.6f}, {0.34f, 0.33f, 0.33f}};
                    for (const auto& w : kWeights) {
                        const float p[3] = {w[0] * a.x + w[1] * b.x + w[2] * c.x, w[0] * a.y + w[1] * b.y + w[2] * c.y,
                                            w[0] * a.z + w[1] * b.z + w[2] * c.z};
                        Sampled want{}, got{};
                        Interpolate(a, b, c, p, axis, want);
                        bool hit = false;
                        if (found != byPlane.end())
                            for (const size_t m : found->second)
                                if (Interpolate(mv[mi[m]], mv[mi[m + 1]], mv[mi[m + 2]], p, axis, got)) {
                                    hit = true;
                                    break;
                                }
                        checkedPoints++;
                        bool same = hit;
                        for (int q = 0; q < 4 && same; q++)
                            same = std::fabs(want.rgba[q] - got.rgba[q]) < 0.02f;
                        for (int q = 0; q < 2 && same; q++)
                            same = std::fabs(want.light[q] - got.light[q]) < 0.02f;
                        const auto fr = [](float t) { return t - std::floor(t); };
                        const auto wrapNear = [&](float x, float y) {
                            const float d = std::fabs(fr(x) - fr(y));
                            return d < 1e-3f || d > 1.0f - 1e-3f;
                        };
                        same = same && std::fabs(want.ao - got.ao) < 1e-4f && wrapNear(want.u, got.u) && wrapNear(want.v, got.v);
                        if (!same && bad++ < 5)
                            std::printf("[meshbench] merge differs at (%.2f,%.2f,%.2f) layer %d: %s rgb %.1f/%.1f light %.1f,%.1f/%.1f,%.1f "
                                        "ao %.4f/%.4f\n",
                                        p[0], p[1], p[2], l, hit ? "covered" : "UNCOVERED", want.rgba[0], got.rgba[0],
                                        want.light[0], want.light[1], got.light[0], got.light[1], want.ao, got.ao);
                    }
                }
            }
            return bad;
        }
    }

    int RunMeshBench(int radius, int rounds) {
        std::shared_ptr<BlockLibrary> library;
        std::shared_ptr<BiomeTints> tints;
        if (const char* samples = std::getenv("PERLIN_SAMPLES")) {
            const std::string root = samples;
            BlockPalette::Instance().LoadVanillaDefinitions(
                root + "/metadata/vanilladata_modules/mojang-blocks.json");
            if (std::string table; resources::Read(resources::path::kBlockStates, table))
                BlockPalette::Instance().LoadServerStatesText(table, "block_states.json");
            library = std::make_shared<BlockLibrary>();
            const std::string packDir = root + "/../perlin-pack";
            if (!library->EnsureLoaded(packDir, root + "/resource_pack")) {
                std::printf("[meshbench] could not load the block library from %s\n", root.c_str());
                return 1;
            }
            library->ApplyCustom(root + "/resource_pack");
            tints = std::make_shared<BiomeTints>();
            // A pack without every biome rule loads on the second try, as in the game.
            if (!tints->EnsureLoaded(root + "/resource_pack"))
                tints->EnsureLoaded(root + "/resource_pack");
            std::printf("[meshbench] textured: library ready=%d complete=%d, tints ready=%d\n",
                        library->Ready(), library->Complete(), tints->Ready());
        }
        InternBlocks(library != nullptr);
        BlockLibrary* lib = library.get();
        BiomeTints* tint = tints.get();
        LightRegistry& registry = LightRegistry::Instance();
        if (const std::string rules = resources::ReadRequiredText(resources::path::kLight); !rules.empty())
            registry.LoadRules(rules);
        registry.SetDerivedOpacity(
            [](uint16_t id) { return id == 0 || id == kWater || id == kLeaves ? 1 : 15; });
        registry.SetOcclusion([](uint16_t id, FaceOcclusion& out) { return BlockFaceOcclusion(id, out); });
        Level level;
        const int r = radius + 1;
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

        for (const ChunkPos& p : todo) {
            Tessellator t;
            t.Begin();
            BuildChunkMesh(level, p, t, lib, tint);
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
                BuildChunkMesh(level, p, t, lib, tint);
                t.End();
                times.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                vertices += t.Vertices().size();
                if (round == 0) {
                    const auto* bytes = reinterpret_cast<const uint8_t*>(t.Vertices().data());
                    const size_t n = t.Vertices().size() * sizeof(t.Vertices()[0]);
                    for (size_t i = 0; i < n; i++)
                        checksum = (checksum ^ bytes[i]) * 1099511628211ull;
                }
            }
        // The merged mesh draws exactly what the unmerged one does.
        {
            int bad = 0, points = 0;
            size_t flatVerts = 0, mergedVerts = 0;
            const bool was = Tessellator::MergeFaces();
            for (const ChunkPos& p : todo) {
                Tessellator::SetMergeFaces(false);
                Tessellator flat;
                flat.Begin();
                BuildChunkMesh(level, p, flat, lib, tint);
                flat.End();
                Tessellator::SetMergeFaces(true);
                Tessellator merged;
                merged.Begin();
                BuildChunkMesh(level, p, merged, lib, tint);
                merged.End();
                flatVerts += flat.Vertices().size();
                mergedVerts += merged.Vertices().size();
                bad += CheckMergeExact(flat, merged, points);
            }
            Tessellator::SetMergeFaces(was);
            std::printf("[meshbench] merge: %zu -> %zu vertices (%.1f%% fewer), %d of %d sample points differ\n",
                        flatVerts, mergedVerts, 100.0 * (1.0 - static_cast<double>(mergedVerts) / std::max<size_t>(1, flatVerts)),
                        bad, points);
            if (bad) {
                std::printf("FAIL merged faces draw differently from the faces they replace\n");
                return 1;
            }
        }
        std::ranges::sort(times);
        double sum = 0;
        for (double v : times)
            sum += v;
        std::printf("[meshbench] %zu chunks x %d rounds: mean %.3f ms, median %.3f ms, p90 %.3f ms, "
                    "%.0f vertices/chunk\n",
                    todo.size(), rounds, sum / times.size(), times[times.size() / 2],
                    times[times.size() * 9 / 10],
                    static_cast<double>(vertices) / static_cast<double>(times.size()));
        MeshProfilePrint();

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
            int x, z;
            int y;
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
        BuildChunkMesh(level, col, *prev, lib, tint, &reuse);
        prev->End();
        reuse.mesh = prev;
        int failures = 0;
        double fullMs = 0, partialMs = 0;
        for (const Edit& e : edits) {
            int y = e.y;
            if (y < -1000)
                y = Height(e.x, e.z) + (y + 2000);
            if (e.y == 0) {
                for (int d = 0; d < 40; d++)
                    level.SetBlock(e.x, Height(e.x, e.z) - d, e.z, 0);
            } else {
                level.SetBlock(e.x, y, e.z, e.id);
            }
            Tessellator full;
            auto t0 = std::chrono::steady_clock::now();
            full.Begin();
            BuildChunkMesh(level, col, full, lib, tint);
            full.End();
            fullMs +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            auto next = std::make_shared<Tessellator>();
            t0 = std::chrono::steady_clock::now();
            next->Begin();
            BuildChunkMesh(level, col, *next, lib, tint, &reuse);
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
}
