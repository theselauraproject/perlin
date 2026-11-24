// The kept chunk light (LightStore) against the window lighting (ChunkLighting::Compute) it replaces: the same
// value in every cell of every chunk after chunks arrive in any order, blocks change (sources, opaque, filtering
// and half blocks), chunks go away and come back. Usage: light_store_selftest [seed] [rounds]
#include "world/level/Level.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/lighting/ChunkLighting.hpp"
#include "world/level/lighting/LightRegistry.hpp"
#include "world/level/lighting/LightStore.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace perlin;

namespace {
    uint16_t kStone, kDirt, kGrass, kLeaves, kWater, kGlow, kTorch, kSlab, kGlass;

    uint32_t Hash(int x, int y, int z) {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
                     static_cast<uint32_t>(z) * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
    float Value(float x, float z) {
        const int ix = static_cast<int>(std::floor(x)), iz = static_cast<int>(std::floor(z));
        const float fx = x - static_cast<float>(ix), fz = z - static_cast<float>(iz);
        const auto v = [&](int a, int b) { return static_cast<float>(Hash(a, 0, b) & 0xFFFF) / 65535.0f; };
        const float sx = fx * fx * (3 - 2 * fx), sz = fz * fz * (3 - 2 * fz);
        const float a = v(ix, iz) + (v(ix + 1, iz) - v(ix, iz)) * sx;
        const float b = v(ix, iz + 1) + (v(ix + 1, iz + 1) - v(ix, iz + 1)) * sx;
        return a + (b - a) * sz;
    }
    int Height(int x, int z) {
        return 40 + static_cast<int>(Value(x / 23.0f, z / 23.0f) * 40.0f + Value(x / 7.0f, z / 7.0f) * 8.0f);
    }
    uint16_t BlockAt(int x, int y, int z) {
        const int h = Height(x, z);
        if (y <= -60)
            return kStone;
        if (y < h) {
            const float c = Value(x / 9.0f + y * 0.41f, z / 9.0f - y * 0.23f);
            if (y < h - 4 && c > 0.68f) {
                const uint32_t r = Hash(x, y, z);
                if ((r & 255) == 0)
                    return kGlow;
                if ((r & 255) == 1)
                    return kTorch;
                return 0;
            }
            if ((Hash(x, y, z) & 127) == 3)
                return kSlab;
            return y < h - 3 ? kStone : kDirt;
        }
        if (y == h)
            return kGrass;
        if (y < 50)
            return kWater;
        if ((Hash(x, 9, z) % 61) == 0 && y <= h + 5)
            return kLeaves;
        if ((Hash(x, y, z) % 997) == 0 && y < h + 12)
            return kGlass;
        return 0;
    }

    uint16_t Intern(const char* name, int32_t net) {
        BlockPalette& pal = BlockPalette::Instance();
        const uint16_t id = pal.Intern(net);
        pal.LearnName(net, name);
        return id;
    }

    void FillChunk(Level& level, int cx, int cz) {
        level.EnsureChunk(cx, cz);
        std::vector<uint16_t> cells(4096);
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

    int failures = 0;
    long long cellsCompared = 0;

    bool Compare(Level& level, const ChunkPos& pos, const char* when) {
        ChunkLighting::Result want, got;
        constexpr int yLo = Chunk::MIN_Y, yHi = Chunk::MIN_Y + Chunk::HEIGHT - 1;
        if (!ChunkLighting::Compute(level, pos, yLo, yHi, want))
            return true;
        {
            std::lock_guard lock(level.mutex);
            LightStore::For(level, LightRegistry::Instance()).Extract(level, pos, yLo, yHi, got);
        }
        int bad = 0;
        for (int y = yLo; y <= yHi; y++)
            for (int z = -1; z <= 16; z++)
                for (int x = -1; x <= 16; x++) {
                    cellsCompared++;
                    const uint8_t a = want.At(x, y, z), b = got.At(x, y, z);
                    if (a != b) {
                        if (bad < 4 && failures < 12)
                            std::printf("  %s: chunk (%d,%d) cell (%d,%d,%d): window sky %d block %d, store sky %d block %d\n",
                                        when, pos.x, pos.z, x, y, z, a >> 4, a & 15, b >> 4, b & 15);
                        bad++;
                    }
                }
        if (bad)
            failures++;
        return bad == 0;
    }
}

int main(int argc, char** argv) {
    const unsigned seed = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1])) : 1;
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 3;
    kStone = Intern("minecraft:stone", -910001);
    kDirt = Intern("minecraft:dirt", -910002);
    kGrass = Intern("minecraft:grass_block", -910003);
    kLeaves = Intern("minecraft:oak_leaves", -910004);
    kWater = Intern("minecraft:water", -910005);
    kGlow = Intern("minecraft:glowstone", -910006);
    kTorch = Intern("minecraft:torch", -910007);
    kSlab = Intern("minecraft:stone_slab", -910008);
    kGlass = Intern("minecraft:glass", -910009);

    LightRegistry& registry = LightRegistry::Instance();
    registry.SetCustom("minecraft:glowstone", 15, 15);
    registry.SetCustom("minecraft:torch", 14, 0);
    registry.SetCustom("minecraft:oak_leaves", 0, 1);
    registry.SetCustom("minecraft:water", 0, 2);
    registry.SetCustom("minecraft:glass", 0, 0);
    registry.SetCustom("minecraft:stone_slab", 0, 0);
    registry.SetDerivedOpacity([](uint16_t id) { return id == 0 ? 0 : 15; });
    // The slab covers its whole bottom face and the lower half of each side.
    registry.SetOcclusion([](uint16_t id, FaceOcclusion& out) {
        if (id != kSlab)
            return false;
        for (int w = 0; w < 4; w++)
            out.bits[0][w] = ~0ull;
        for (int dir = 2; dir < 6; dir++)
            out.bits[dir][0] = out.bits[dir][1] = ~0ull;
        return true;
    });

    std::mt19937 rng(seed);
    const int radius = 3;
    for (int round = 0; round < rounds; round++) {
        Level level;
        std::vector<ChunkPos> order;
        for (int cx = -radius; cx <= radius; cx++)
            for (int cz = -radius; cz <= radius; cz++)
                order.push_back({cx + round * 50, cz});
        std::shuffle(order.begin(), order.end(), rng);

        // Chunks arrive in a random order, meshes asked for along the way.
        std::vector<ChunkPos> present;
        for (const ChunkPos& p : order) {
            FillChunk(level, p.x, p.z);
            present.push_back(p);
            for (int k = 0; k < 2; k++)
                Compare(level, present[rng() % present.size()], "arrival");
        }
        for (const ChunkPos& p : present)
            Compare(level, p, "all arrived");

        // Block edits: sources, opaque, filtering, half blocks; at borders as often as inside.
        const uint16_t palette[] = {0, kStone, kGlow, kTorch, kLeaves, kWater, kSlab, kGlass};
        for (int e = 0; e < 120; e++) {
            const ChunkPos p = present[rng() % present.size()];
            const int lx = (rng() % 3 == 0) ? (rng() % 2 ? 0 : 15) : static_cast<int>(rng() % 16);
            const int lz = static_cast<int>(rng() % 16);
            const int x = p.x * 16 + lx, z = p.z * 16 + lz;
            const int y = Height(x, z) + static_cast<int>(rng() % 12) - 6;
            level.SetBlock(x, y, z, palette[rng() % std::size(palette)]);
            if (e % 3 == 0)
                Compare(level, {(x + static_cast<int>(rng() % 33) - 16) >> 4, (z + static_cast<int>(rng() % 33) - 16) >> 4}, "edit");
        }
        for (const ChunkPos& p : present)
            Compare(level, p, "after edits");

        // Chunks go away and come back.
        for (int k = 0; k < 10; k++) {
            const size_t i = rng() % present.size();
            const ChunkPos p = present[i];
            level.RemoveChunk(p.x, p.z);
            present.erase(present.begin() + static_cast<long>(i));
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++)
                    if (level.HasChunk(p.x + dx, p.z + dz))
                        Compare(level, {p.x + dx, p.z + dz}, "removal");
            if (k % 2 == 0) {
                FillChunk(level, p.x, p.z);
                present.push_back(p);
                Compare(level, p, "return");
            }
        }
        for (const ChunkPos& p : present)
            Compare(level, p, "final");

        // Speed: meshing every chunk once with each, the store having lit nothing beforehand.
        {
            Level fresh;
            for (const ChunkPos& p : order)
                FillChunk(fresh, p.x, p.z);
            ChunkLighting::Result r;
            const auto t0 = std::chrono::steady_clock::now();
            for (const ChunkPos& p : order)
                ChunkLighting::Compute(fresh, p, -20, 120, r);
            const auto t1 = std::chrono::steady_clock::now();
            for (const ChunkPos& p : order) {
                std::lock_guard lock(fresh.mutex);
                LightStore::For(fresh, LightRegistry::Instance()).Extract(fresh, p, -20, 120, r);
            }
            const auto t2 = std::chrono::steady_clock::now();
            for (const ChunkPos& p : order) {
                std::lock_guard lock(fresh.mutex);
                LightStore::For(fresh, LightRegistry::Instance()).Extract(fresh, p, -20, 120, r);
            }
            const auto t3 = std::chrono::steady_clock::now();
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            const LightStore::Stats& st = LightStore::For(fresh, LightRegistry::Instance()).Counters();
            std::printf("round %d: %zu chunks: window %.2f ms/chunk, store first time %.2f ms/chunk, store again %.3f ms/chunk, %zu KiB kept; "
                        "%llu relights, %llu increase and %llu decrease steps\n",
                        round, order.size(), ms(t0, t1) / order.size(), ms(t1, t2) / order.size(), ms(t2, t3) / order.size(),
                        LightStore::For(fresh, LightRegistry::Instance()).Bytes() / 1024, static_cast<unsigned long long>(st.relit),
                        static_cast<unsigned long long>(st.increaseSteps), static_cast<unsigned long long>(st.decreaseSteps));
        }
    }
    {
        // A player moving along leaves chunks behind with no meshed neighbour; the store must let them go.
        Level trail;
        for (int x = 0; x < 80; x++)
            for (int z = 0; z < 2; z++) {
                FillChunk(trail, x, z);
                std::lock_guard lock(trail.mutex);
                ChunkLighting::Result r;
                LightStore::For(trail, LightRegistry::Instance()).Extract(trail, {x, z}, -20, 120, r);
            }
        for (int x = 0; x < 76; x++)
            for (int z = 0; z < 2; z++)
                trail.RemoveChunk(x, z);
        {
            std::lock_guard lock(trail.mutex);
            ChunkLighting::Result r;
            LightStore::For(trail, LightRegistry::Instance()).Extract(trail, {79, 0}, -20, 120, r);
        }
        const size_t kept = LightStore::For(trail, LightRegistry::Instance()).Chunks();
        std::printf("trail: %zu chunks loaded, %zu kept by the store\n", trail.chunks.size(), kept);
        if (kept > trail.chunks.size()) {
            std::printf("FAIL the store keeps unloaded chunks\n");
            failures++;
        }
        Compare(trail, {77, 0}, "after the trail sweep");
    }
    std::printf("LightStore: %lld cells compared, %d chunk comparisons differ\n", cellsCompared, failures);
    return failures ? 1 : 0;
}
