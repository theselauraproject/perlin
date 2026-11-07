#include "world/level/lighting/ChunkLighting.hpp"

#include <algorithm>

namespace perlin {
    namespace {
        constexpr int kSpan = ChunkLighting::kSpan;
        constexpr size_t kLayer = static_cast<size_t>(kSpan) * kSpan;

        struct Scratch {
            ChunkLighting::Grid grid;
            std::vector<uint32_t> queue;
            std::vector<ChunkLighting::Emitter> emitters;
        };

        Scratch& Local() {
            static thread_local Scratch scratch;
            return scratch;
        }

        inline bool Occluded(const ChunkLighting::Grid& grid, uint32_t from, uint32_t to, int dir) {
            if (grid.shaped.empty() || (!grid.shaped[from] && !grid.shaped[to]))
                return false;
            const LightRegistry& registry = LightRegistry::Instance();
            const FaceOcclusion* a = grid.shaped[from] ? registry.Occlusion(grid.ids[from]) : nullptr;
            const FaceOcclusion* b = grid.shaped[to] ? registry.Occlusion(grid.ids[to]) : nullptr;
            return FaceOcclusion::Occludes(a, dir, b);
        }

        void Flood(const ChunkLighting::Grid& grid, std::vector<uint8_t>& light, std::vector<uint32_t>& queue,
                   int rows) {
            const std::vector<uint8_t>& opacity = grid.opacity;
            size_t head = 0;
            while (head < queue.size()) {
                const uint32_t i = queue[head++];
                const int level = light[i];
                if (level <= 1)
                    continue;
                const int x = static_cast<int>(i % kSpan);
                const int z = static_cast<int>((i / kSpan) % kSpan);
                const int y = static_cast<int>(i / kLayer);

                auto visit = [&](uint32_t n, int dir) {
                    const int cost = std::max(1, static_cast<int>(opacity[n]));
                    if (opacity[n] >= kMaxLight)
                        return;
                    if (Occluded(grid, i, n, dir))
                        return;
                    const int next = level - cost;
                    if (next > light[n]) {
                        light[n] = static_cast<uint8_t>(next);
                        queue.push_back(n);
                    }
                };
                if (x > 0)
                    visit(i - 1, 4);
                if (x < kSpan - 1)
                    visit(i + 1, 5);
                if (z > 0)
                    visit(i - kSpan, 2);
                if (z < kSpan - 1)
                    visit(i + kSpan, 3);
                if (y > 0)
                    visit(i - static_cast<uint32_t>(kLayer), 0);
                if (y < rows - 1)
                    visit(i + static_cast<uint32_t>(kLayer), 1);
            }
            queue.clear();
        }
    }

    void ChunkLighting::Propagate(Grid& grid, const std::vector<Emitter>& emitters) {
        Scratch& scratch = Local();
        const int rows = grid.rows;
        const size_t cells = kLayer * static_cast<size_t>(rows);
        grid.sky.assign(cells, 0);
        grid.block.assign(cells, 0);

        static thread_local std::vector<int16_t> colTop;
        colTop.assign(kLayer, -1);
        for (int z = 0; z < kSpan; z++)
            for (int x = 0; x < kSpan; x++) {
                int level = kMaxLight;
                size_t i = grid.Index(x, grid.yBase + rows - 1, z);
                for (int row = rows - 1; row >= 0; row--, i -= kLayer) {
                    const int cost = grid.opacity[i];
                    if (cost >= kMaxLight)
                        level = 0;
                    else if (row < rows - 1 &&
                             Occluded(grid, static_cast<uint32_t>(i + kLayer), static_cast<uint32_t>(i), 0))
                        level = 0;
                    else
                        level = std::max(0, level - cost);
                    grid.sky[i] = static_cast<uint8_t>(level);
                    if (level < kMaxLight && colTop[static_cast<size_t>(z) * kSpan + x] < row)
                        colTop[static_cast<size_t>(z) * kSpan + x] = static_cast<int16_t>(row);
                }
            }

        std::vector<uint32_t>& queue = scratch.queue;
        queue.clear();
        for (int z = 0; z < kSpan; z++)
            for (int x = 0; x < kSpan; x++) {
                const size_t c = static_cast<size_t>(z) * kSpan + x;
                int top = colTop[c];
                if (x > 0)
                    top = std::max<int>(top, colTop[c - 1]);
                if (x < kSpan - 1)
                    top = std::max<int>(top, colTop[c + 1]);
                if (z > 0)
                    top = std::max<int>(top, colTop[c - kSpan]);
                if (z < kSpan - 1)
                    top = std::max<int>(top, colTop[c + kSpan]);
                const int scanTop = std::min(rows - 1, top + 1);
                for (int row = 0; row <= scanTop; row++) {
                    const uint32_t i = static_cast<uint32_t>(static_cast<size_t>(row) * kLayer + c);
                    const int level = grid.sky[i];
                    if (level <= 1)
                        continue;
                    auto darker = [&](uint32_t n) {
                        return grid.opacity[n] < kMaxLight &&
                               grid.sky[n] < level - std::max(1, static_cast<int>(grid.opacity[n]));
                    };
                    bool edge = false;
                    if (x > 0)
                        edge |= darker(i - 1);
                    if (x < kSpan - 1)
                        edge |= darker(i + 1);
                    if (z > 0)
                        edge |= darker(i - kSpan);
                    if (z < kSpan - 1)
                        edge |= darker(i + kSpan);
                    if (row > 0)
                        edge |= darker(i - static_cast<uint32_t>(kLayer));
                    if (row < rows - 1)
                        edge |= darker(i + static_cast<uint32_t>(kLayer));
                    if (edge)
                        queue.push_back(i);
                }
            }
        Flood(grid, grid.sky, queue, rows);

        if (emitters.empty())
            return;
        for (const Emitter& emitter : emitters) {
            grid.block[emitter.index] = std::max(grid.block[emitter.index], emitter.level);
            queue.push_back(emitter.index);
        }
        Flood(grid, grid.block, queue, rows);
    }

    void ChunkLighting::Extract(const Grid& grid, int yLo, int yHi, Result& out) {
        out.yBase = yLo;
        out.rows = yHi - yLo + 1;
        out.packed.assign(static_cast<size_t>(out.rows) * kPadded * kPadded, 0);
        for (int y = yLo; y <= yHi; y++) {
            const bool inside = y >= grid.yBase && y < grid.yBase + grid.rows;
            for (int z = -1; z <= 16; z++)
                for (int x = -1; x <= 16; x++) {
                    uint8_t packed = Result::kSkyOnly;
                    if (inside) {
                        const size_t i = grid.Index(x + 16, y, z + 16);
                        packed = static_cast<uint8_t>((grid.sky[i] << 4) | grid.block[i]);
                    } else if (y < grid.yBase) {
                        packed = 0;
                    }
                    out.packed[(static_cast<size_t>(y - yLo) * kPadded + static_cast<size_t>(z + 1)) *
                                   kPadded +
                               static_cast<size_t>(x + 1)] = packed;
                }
        }
    }

    bool ChunkLighting::Compute(Level& level, const ChunkPos& pos, int yLo, int yHi, Result& out,
                                bool tryLock) {
        Scratch& scratch = Local();
        Grid& grid = scratch.grid;

        const Chunk* around[3][3];
        bool present[3][3];
        int lo = Chunk::MIN_Y + Chunk::HEIGHT, hi = Chunk::MIN_Y;
        {
            std::unique_lock lock(level.mutex, std::defer_lock);
            if (!tryLock)
                lock.lock();
            else if (!lock.try_lock())
                return false;
            for (int dz = 0; dz < 3; dz++)
                for (int dx = 0; dx < 3; dx++) {
                    const auto it = level.chunks.find({pos.x + dx - 1, pos.z + dz - 1});
                    around[dz][dx] = it == level.chunks.end() ? nullptr : &it->second;
                    present[dz][dx] = around[dz][dx] != nullptr;
                    int clo = 0, chi = 0;
                    if (around[dz][dx] && around[dz][dx]->YRange(clo, chi)) {
                        lo = std::min(lo, clo);
                        hi = std::max(hi, chi);
                    }
                }
            if (hi < lo) {
                out.yBase = yLo;
                out.rows = yHi - yLo + 1;
                out.packed.assign(static_cast<size_t>(out.rows) * kPadded * kPadded, Result::kSkyOnly);
                return true;
            }
            grid.yBase = std::max(std::max(Chunk::MIN_Y, std::min(lo, yLo) - 1), yLo - 16);
            const int top = std::min(Chunk::MIN_Y + Chunk::HEIGHT - 1, std::max(hi, yHi) + 1);
            grid.rows = top - grid.yBase + 1;
            grid.ids.assign(kLayer * static_cast<size_t>(grid.rows), 0);
            for (int row = 0; row < grid.rows; row++) {
                const int y = grid.yBase + row;
                for (int z = 0; z < kSpan; z++)
                    for (int cx = 0; cx < 3; cx++)
                        if (const Chunk* src = around[z >> 4][cx])
                            src->CopyRun(
                                y, z & 15, 0, 16,
                                &grid.ids[static_cast<size_t>(row) * kLayer + static_cast<size_t>(z) * kSpan +
                                          static_cast<size_t>(cx) * 16],
                                nullptr, nullptr);
            }
        }

        const LightRegistry& registry = LightRegistry::Instance();
        const size_t cells = grid.ids.size();
        grid.opacity.resize(cells);
        grid.shaped.assign(cells, 0);
        scratch.emitters.clear();
        for (size_t i = 0; i < cells; i++) {
            const uint16_t id = grid.ids[i];
            if (id == 0) {
                grid.opacity[i] = 0;
                continue;
            }
            const BlockLightProps props = registry.Resolve(id);
            grid.opacity[i] = props.opacity;
            if (props.opacity < kMaxLight && registry.Occlusion(id))
                grid.shaped[i] = 1;
            if (props.emission > 0)
                scratch.emitters.push_back({static_cast<uint32_t>(i), props.emission});
        }
        for (int dz = 0; dz < 3; dz++)
            for (int dx = 0; dx < 3; dx++) {
                if (present[dz][dx])
                    continue;
                for (int z = dz * 16; z < dz * 16 + 16; z++)
                    for (int x = dx * 16; x < dx * 16 + 16; x++)
                        for (int row = 0; row < grid.rows; row++)
                            grid.opacity[static_cast<size_t>(row) * kLayer + static_cast<size_t>(z) * kSpan +
                                         static_cast<size_t>(x)] = kMaxLight;
            }

        Propagate(grid, scratch.emitters);
        Extract(grid, yLo, yHi, out);
        return true;
    }
}
