#pragma once
#include "world/level/Level.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <cstdint>
#include <vector>

namespace perlin {
    class ChunkLighting {
    public:
        static constexpr int kSpan = 48;
        static constexpr int kPadded = 18;

        struct Result {
            int yBase = 0;
            int rows = 0;
            std::vector<uint8_t> packed;

            uint8_t At(int x, int y, int z) const {
                if (y >= yBase + rows)
                    return kSkyOnly;
                if (y < yBase || x < -1 || x > 16 || z < -1 || z > 16)
                    return 0;
                return packed[(static_cast<size_t>(y - yBase) * kPadded + static_cast<size_t>(z + 1)) *
                                  kPadded +
                              static_cast<size_t>(x + 1)];
            }

            static constexpr uint8_t kSkyOnly = kMaxLight << 4;
            static int Sky(uint8_t light) { return light >> 4; }
            static int Block(uint8_t light) { return light & 15; }
        };

        // tryLock: give up (false) instead of waiting when the level is busy
        static bool Compute(Level& level, const ChunkPos& pos, int yLo, int yHi, Result& out,
                            bool tryLock = false);

        struct Grid {
            int yBase = 0;
            int rows = 0;
            std::vector<uint16_t> ids;
            std::vector<uint8_t> opacity;
            std::vector<uint8_t> shaped;
            std::vector<uint8_t> sky;
            std::vector<uint8_t> block;

            size_t Index(int x, int y, int z) const {
                return (static_cast<size_t>(y - yBase) * kSpan + static_cast<size_t>(z)) * kSpan +
                       static_cast<size_t>(x);
            }
        };

        struct Emitter {
            uint32_t index;
            uint8_t level;
        };

        static void Propagate(Grid& grid, const std::vector<Emitter>& emitters);
        static void Extract(const Grid& grid, int yLo, int yHi, Result& out);
    };
}
