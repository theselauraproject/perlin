#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace perlin::sectionvis {
    inline constexpr int kSide = 16;
    inline constexpr int kCells = kSide * kSide * kSide;
    inline constexpr uint16_t kAllConnections = 0x7FFF;
    using Mask = std::array<uint8_t, kCells>;

    namespace detail {
        struct LookupTables {
            std::array<uint8_t, kCells> faces{};
            std::array<uint16_t, kCells - 14 * 14 * 14> boundary{};
            LookupTables() {
                size_t count = 0;
                for (int i = 0; i < kCells; i++) {
                    const int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
                    faces[i] = static_cast<uint8_t>((y == 0 ? 1 : 0) | (y == 15 ? 2 : 0) |
                                                    (z == 0 ? 4 : 0) | (z == 15 ? 8 : 0) |
                                                    (x == 0 ? 16 : 0) | (x == 15 ? 32 : 0));
                    if (faces[i])
                        boundary[count++] = static_cast<uint16_t>(i);
                }
            }
        };

        inline const LookupTables& Tables() {
            // Construct once with the language's thread-safe local-static initialization. Keeping
            // these larger tables out of constexpr evaluation also fits MSVC's default step limit.
            static const LookupTables tables;
            return tables;
        }

        inline constexpr auto kConnections = [] {
            std::array<uint16_t, 64> connections{};
            for (int mask = 0; mask < 64; mask++)
                for (int a = 0; a < 6; a++)
                    if (mask & (1 << a))
                        for (int b = a + 1; b < 6; b++)
                            if (mask & (1 << b))
                                connections[mask] |= static_cast<uint16_t>(
                                    1u << (a * (11 - a) / 2 + (b - a - 1)));
            return connections;
        }();

        // Nonzero cells are opaque or already visited. No interior-only component can connect two
        // section faces, so only boundary cells need to start a flood fill.
        inline uint16_t Consume(Mask& seen) {
            const auto open = std::count(seen.begin(), seen.end(), uint8_t{0});
            if (open == kCells)
                return kAllConnections;
            if (!open)
                return 0;

            const auto& tables = Tables();
            std::array<uint16_t, kCells> stack;
            uint16_t connections = 0;
            for (uint16_t start : tables.boundary) {
                if (seen[start])
                    continue;
                unsigned mask = 0;
                size_t count = 0;
                stack[count++] = start;
                seen[start] = 1;
                while (count) {
                    const uint16_t i = stack[--count];
                    const uint8_t faces = tables.faces[i];
                    mask |= faces;
                    if (mask == 63)
                        return kAllConnections;
                    const auto push = [&](int next) {
                        if (!seen[next]) {
                            seen[next] = 1;
                            stack[count++] = static_cast<uint16_t>(next);
                        }
                    };
                    if (!(faces & 16))
                        push(i - 1);
                    if (!(faces & 32))
                        push(i + 1);
                    if (!(faces & 4))
                        push(i - 16);
                    if (!(faces & 8))
                        push(i + 16);
                    if (!(faces & 1))
                        push(i - 256);
                    if (!(faces & 2))
                        push(i + 256);
                }
                connections |= kConnections[mask];
                if (connections == kAllConnections)
                    break;
            }
            return connections;
        }
    }

    // Dense section order is x + 16*z + 256*y. A nonzero byte blocks traversal.
    inline uint16_t Compute(std::span<const uint8_t, kCells> opaque) {
        Mask seen;
        std::memcpy(seen.data(), opaque.data(), seen.size());
        return detail::Consume(seen);
    }

    // ChunkMesher keeps a one-block horizontal border: 18 x 18 cells per Y row. Rows outside the
    // cache's vertical window are air, exactly as BlockCache::Occ treats them.
    inline uint16_t ComputePadded(std::span<const uint8_t> opaque, int yBase, int sectionY) {
        constexpr size_t kRowCells = 18 * 18;
        const size_t rows = opaque.size() / kRowCells;
        Mask seen{};
        for (int y = 0; y < kSide; y++) {
            const int64_t row = static_cast<int64_t>(sectionY) * kSide + y - yBase;
            if (row < 0 || static_cast<uint64_t>(row) >= rows)
                continue;
            const uint8_t* from = opaque.data() + static_cast<size_t>(row) * kRowCells + 18 + 1;
            uint8_t* to = seen.data() + y * 256;
            for (int z = 0; z < kSide; z++)
                std::memcpy(to + z * 16, from + z * 18, kSide);
        }
        return detail::Consume(seen);
    }
}
