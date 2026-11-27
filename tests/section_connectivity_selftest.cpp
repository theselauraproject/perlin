#include "client/systems/renderer/chunk/SectionConnectivity.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string_view>
#include <vector>

namespace {
    using perlin::sectionvis::Mask;
    int checks = 0, failures = 0;
    void Check(bool ok, const char* name) {
        checks++;
        if (!ok) {
            failures++;
            std::printf("FAIL %s\n", name);
        }
    }

    // Deliberately independent reference: visit every component with a FIFO and explicit 3D bounds.
    uint16_t Reference(const Mask& opaque) {
        std::array<bool, 4096> visited{};
        std::array<int, 4096> queue{};
        bool connected[6][6]{};
        constexpr int dx[6] = {0, 0, 0, 0, -1, 1};
        constexpr int dy[6] = {-1, 1, 0, 0, 0, 0};
        constexpr int dz[6] = {0, 0, -1, 1, 0, 0};
        for (int start = 0; start < 4096; start++) {
            if (opaque[start] || visited[start])
                continue;
            size_t head = 0, tail = 0;
            queue[tail++] = start;
            visited[start] = true;
            bool touches[6]{};
            while (head < tail) {
                const int cell = queue[head++];
                const int x = cell % 16, z = (cell / 16) % 16, y = cell / 256;
                touches[0] |= y == 0;
                touches[1] |= y == 15;
                touches[2] |= z == 0;
                touches[3] |= z == 15;
                touches[4] |= x == 0;
                touches[5] |= x == 15;
                for (int d = 0; d < 6; d++) {
                    const int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
                    if (nx < 0 || nx > 15 || ny < 0 || ny > 15 || nz < 0 || nz > 15)
                        continue;
                    const int next = nx + nz * 16 + ny * 256;
                    if (!opaque[next] && !visited[next]) {
                        visited[next] = true;
                        queue[tail++] = next;
                    }
                }
            }
            for (int a = 0; a < 6; a++)
                for (int b = a + 1; b < 6; b++)
                    connected[a][b] |= touches[a] && touches[b];
        }
        uint16_t result = 0;
        int bit = 0;
        for (int a = 0; a < 6; a++)
            for (int b = a + 1; b < 6; b++, bit++)
                if (connected[a][b])
                    result |= static_cast<uint16_t>(1u << bit);
        return result;
    }

    void Compare(const Mask& opaque, const char* name) {
        const Mask before = opaque;
        Check(perlin::sectionvis::Compute(opaque) == Reference(opaque), name);
        Check(opaque == before, "input occupancy remains unchanged");
    }

    Mask Dense(const std::vector<uint8_t>& padded, int yBase, int section) {
        Mask out{};
        const int64_t rows = static_cast<int64_t>(padded.size() / 324);
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++) {
                    const int64_t row = static_cast<int64_t>(section) * 16 + y - yBase;
                    if (row >= 0 && row < rows)
                        out[x + z * 16 + y * 256] = padded[static_cast<size_t>(row * 324 + (z + 1) * 18 + x + 1)];
                }
        return out;
    }

    // Original ChunkMesher algorithm, including the per-cell padded-cache lookup, for optional CPU
    // timing. Correctness above uses a separate FIFO implementation, not this same DFS.
    uint16_t Legacy(const std::vector<uint8_t>& padded, int yBase, int section) {
        const int yRows = static_cast<int>(padded.size() / 324);
        const int y0 = section * 16;
        static thread_local std::vector<uint8_t> seen;
        static thread_local std::vector<uint16_t> stack;
        seen.assign(4096, 0);
        int opaque = 0;
        for (int i = 0; i < 4096; i++) {
            const int x = i & 15, z = (i >> 4) & 15, y = (i >> 8) + y0;
            if (static_cast<unsigned>(y - yBase) < static_cast<unsigned>(yRows) &&
                padded[(static_cast<size_t>(y - yBase) * 18 + z + 1) * 18 + x + 1]) {
                seen[i] = 1;
                opaque++;
            }
        }
        if (opaque == 0)
            return 0x7FFF;
        if (opaque == 4096)
            return 0;
        uint16_t conn = 0;
        for (int start = 0; start < 4096; start++) {
            if (seen[start])
                continue;
            int mask = 0;
            stack.clear();
            stack.push_back(static_cast<uint16_t>(start));
            seen[start] = 1;
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                const int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
                if (y == 0) mask |= 1;
                if (y == 15) mask |= 2;
                if (z == 0) mask |= 4;
                if (z == 15) mask |= 8;
                if (x == 0) mask |= 16;
                if (x == 15) mask |= 32;
                const auto push = [&](int next) {
                    if (!seen[next]) {
                        seen[next] = 1;
                        stack.push_back(static_cast<uint16_t>(next));
                    }
                };
                if (x > 0) push(i - 1);
                if (x < 15) push(i + 1);
                if (z > 0) push(i - 16);
                if (z < 15) push(i + 16);
                if (y > 0) push(i - 256);
                if (y < 15) push(i + 256);
            }
            for (int a = 0; a < 6; a++)
                if (mask & (1 << a))
                    for (int b = a + 1; b < 6; b++)
                        if (mask & (1 << b))
                            conn |= static_cast<uint16_t>(1u << (a * (11 - a) / 2 + b - a - 1));
            if (conn == 0x7FFF)
                break;
        }
        return conn;
    }

    void Benchmark() {
        constexpr const char* names[] = {"air", "solid", "sealed cavities", "terrain", "sparse", "dense", "random"};
        std::mt19937 random(2984);
        uint64_t checksum = 0;
        for (int kind = 0; kind < 7; kind++) {
            std::array<std::vector<uint8_t>, 32> samples;
            for (auto& sample : samples) {
                sample.resize(16 * 324);
                for (int y = 0; y < 16; y++)
                    for (int z = 0; z < 18; z++)
                        for (int x = 0; x < 18; x++) {
                            bool solid = kind == 1;
                            if (kind == 2)
                                solid = x <= 1 || x >= 16 || z <= 1 || z >= 16 || y == 0 || y == 15 || random() % 5 == 0;
                            if (kind == 3)
                                solid = y <= 6 + (x + z) / 5;
                            if (kind >= 4)
                                solid = random() % 100 < (kind == 4 ? 5u : kind == 5 ? 95u : 50u);
                            sample[(y * 18 + z) * 18 + x] = static_cast<uint8_t>(solid);
                        }
            }
            for (const auto& sample : samples)
                Check(Legacy(sample, 0, 0) == perlin::sectionvis::ComputePadded(sample, 0, 0), "benchmark output agrees");
            constexpr int rounds = 100;
            const auto run = [&](auto fn) {
                const auto start = std::chrono::steady_clock::now();
                uint64_t sum = 0;
                for (int round = 0; round < rounds; round++)
                    for (const auto& sample : samples)
                        sum += fn(sample, 0, 0);
                checksum += sum;
                return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            };
            double before = 0, after = 0;
            for (bool candidate : {false, true, true, false}) {
                if (candidate)
                    after += run([](const auto& a, int b, int c) { return perlin::sectionvis::ComputePadded(a, b, c); });
                else
                    before += run(Legacy);
            }
            std::printf("BENCH %-16s old_ms=%.3f new_ms=%.3f speedup=%.2f sections=%zu order=ABBA\n", names[kind],
                        before / 2, after / 2, before / after, samples.size() * rounds);
        }
        std::printf("BENCH checksum=%llu\n", static_cast<unsigned long long>(checksum));
    }
}

int main(int argc, char** argv) {
    Mask mask{};
    Check(perlin::sectionvis::Compute(mask) == 0x7FFF, "empty section connects every face pair");
    mask.fill(1);
    Check(perlin::sectionvis::Compute(mask) == 0, "opaque section connects no faces");
    mask.fill(0xFF);
    Compare(mask, "nonzero opacity bytes are blocked");
    mask[7 + 7 * 16 + 7 * 256] = 0;
    Compare(mask, "isolated interior air cell contributes no connection");
    for (int y = 1; y < 15; y++)
        for (int z = 1; z < 15; z++)
            for (int x = 1; x < 15; x++)
                mask[x + z * 16 + y * 256] = 0;
    Compare(mask, "large sealed cavity contributes no connection");
    mask[7 + 7 * 16] = 0;
    Compare(mask, "cavity touching one face contributes no pair");
    mask[7 + 7 * 16 + 15 * 256] = 0;
    Check(perlin::sectionvis::Compute(mask) == 1, "cavity tunnel connects only bottom and top");
    Compare(mask, "two-face cavity exact pair numbering");
    for (int corner = 0; corner < 8; corner++) {
        mask.fill(1);
        mask[(corner & 1 ? 15 : 0) + (corner & 2 ? 15 : 0) * 16 + (corner & 4 ? 15 : 0) * 256] = 0;
        Compare(mask, "single corner connects exactly its three faces");
    }
    for (int axis = 0; axis < 3; axis++) {
        mask.fill(1);
        for (int n = 0; n < 16; n++) {
            const int x = axis == 0 ? n : 7, z = axis == 1 ? n : 7, y = axis == 2 ? n : 7;
            mask[x + z * 16 + y * 256] = 0;
        }
        Compare(mask, "single straight tunnel across each axis");
    }
    mask.fill(0);
    for (int y = 0; y < 16; y++)
        for (int z = 0; z < 16; z++)
            mask[8 + z * 16 + y * 256] = 1;
    Compare(mask, "opaque wall separates opposite X faces");

    std::mt19937 random(0xA2C418u);
    for (int density : {0, 1, 5, 20, 40, 50, 60, 80, 95, 99, 100})
        for (int n = 0; n < 64; n++) {
            for (auto& cell : mask)
                cell = random() % 100 < static_cast<unsigned>(density) ? static_cast<uint8_t>(1 + random() % 255) : 0;
            Compare(mask, "seeded random occupancy matches independent reference");
        }
    for (int base : {-64, -33, -16, -5, 0, 13, 40})
        for (int rows : {0, 1, 15, 16, 17, 33}) {
            std::vector<uint8_t> padded(static_cast<size_t>(rows) * 324);
            for (uint8_t& cell : padded)
                cell = random() % 5 ? 1 : 0;
            const auto before = padded;
            for (int section = -5; section <= 4; section++)
                Check(perlin::sectionvis::ComputePadded(padded, base, section) == Reference(Dense(padded, base, section)),
                      "padded stride and partial vertical windows preserve out-of-range air");
            Check(padded == before, "padded input remains unchanged");
        }
    if (argc > 1 && std::string_view(argv[1]) == "--bench")
        Benchmark();
    std::printf("section_connectivity_selftest: %s (%d checks, %d failures)\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
