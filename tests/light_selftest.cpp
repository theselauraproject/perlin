#include "world/level/lighting/ChunkLighting.hpp"

#include <cstdio>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static ChunkLighting::Grid MakeGrid(int rows) {
    ChunkLighting::Grid grid;
    grid.yBase = 0;
    grid.rows = rows;
    const size_t cells = static_cast<size_t>(rows) * ChunkLighting::kSpan * ChunkLighting::kSpan;
    grid.opacity.assign(cells, 0);
    grid.shaped.assign(cells, 0);
    grid.ids.assign(cells, 0);
    return grid;
}

int main() {
    {
        ChunkLighting::Grid grid = MakeGrid(8);
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[grid.Index(20, 0, 20)] == 15);
        CHECK(grid.sky[grid.Index(20, 7, 20)] == 15);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        for (int x = 0; x < ChunkLighting::kSpan; x++)
            for (int z = 0; z < ChunkLighting::kSpan; z++)
                grid.opacity[grid.Index(x, 5, z)] = kMaxLight;
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[grid.Index(10, 6, 10)] == 15);
        CHECK(grid.sky[grid.Index(10, 5, 10)] == 0);
        CHECK(grid.sky[grid.Index(10, 4, 10)] == 0);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        for (int x = 0; x < ChunkLighting::kSpan; x++)
            for (int z = 0; z < ChunkLighting::kSpan; z++)
                if (x != 24 || z != 24)
                    grid.opacity[grid.Index(x, 5, z)] = kMaxLight;
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[grid.Index(24, 4, 24)] == 15);
        CHECK(grid.sky[grid.Index(25, 4, 24)] == 14);
        CHECK(grid.sky[grid.Index(24, 4, 27)] == 12);
        CHECK(grid.sky[grid.Index(27, 4, 27)] == 9);
        CHECK(grid.sky[grid.Index(24, 3, 24)] == 15);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        for (int x = 0; x < ChunkLighting::kSpan; x++)
            for (int z = 0; z < ChunkLighting::kSpan; z++)
                grid.opacity[grid.Index(x, 7, z)] = kMaxLight;
        const uint32_t source = static_cast<uint32_t>(grid.Index(24, 3, 24));
        ChunkLighting::Propagate(grid, {{source, 14}});
        CHECK(grid.block[source] == 14);
        CHECK(grid.block[grid.Index(25, 3, 24)] == 13);
        CHECK(grid.block[grid.Index(24, 3, 27)] == 11);
        CHECK(grid.block[grid.Index(26, 4, 25)] == 10);
        CHECK(grid.block[grid.Index(24, 3, 24 + 14)] == 0);
        CHECK(grid.block[grid.Index(24, 3, 24 + 13)] == 1);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        for (int y = 0; y < 8; y++)
            grid.opacity[grid.Index(25, y, 24)] = kMaxLight;
        const uint32_t source = static_cast<uint32_t>(grid.Index(24, 3, 24));
        ChunkLighting::Propagate(grid, {{source, 15}});
        CHECK(grid.block[grid.Index(25, 3, 24)] == 0);
        CHECK(grid.block[grid.Index(26, 3, 24)] == 11);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        grid.opacity[grid.Index(24, 5, 24)] = 3;
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[grid.Index(24, 5, 24)] == 12);
        CHECK(grid.sky[grid.Index(24, 4, 24)] == 14);
    }

    LightRegistry::Instance().SetOcclusion([](uint16_t id, FaceOcclusion& f) {
        if (id != 5 && id != 6)
            return false;
        const bool bottom = id == 5;
        for (int bit = 0; bit < 256; bit++) {
            const int row = bit / 16;
            f.bits[bottom ? 0 : 1][bit >> 6] |= 1ull << (bit & 63);
            for (int d = 2; d < 6; d++)
                if (bottom ? row < 8 : row >= 8)
                    f.bits[d][bit >> 6] |= 1ull << (bit & 63);
        }
        return true;
    });
    {
        ChunkLighting::Grid grid = MakeGrid(8);
        const size_t slab = grid.Index(24, 4, 24);
        grid.ids[slab] = 5;
        grid.shaped[slab] = 1;
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[slab] == 15);
        CHECK(grid.sky[grid.Index(24, 3, 24)] == 14);
    }
    {
        ChunkLighting::Grid grid = MakeGrid(8);
        const size_t slab = grid.Index(24, 4, 24);
        grid.ids[slab] = 6;
        grid.shaped[slab] = 1;
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[slab] == 14);
        CHECK(grid.sky[grid.Index(24, 3, 24)] == 14);
    }

    std::printf(failures ? "%d FAILED\n" : "light self-tests passed\n", failures);
    return failures ? 1 : 0;
}
