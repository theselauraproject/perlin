#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace perlin::server {

    struct ChunkPos {
        int32_t x = 0, z = 0;
        bool operator==(const ChunkPos&) const = default;
    };

    struct ChunkPosHash {
        size_t operator()(const ChunkPos& p) const {
            return static_cast<size_t>((static_cast<uint64_t>(static_cast<uint32_t>(p.x)) << 32) |
                                       static_cast<uint32_t>(p.z)) *
                   0x9E3779B97F4A7C15ull;
        }
    };

    constexpr int kMinY = -64;
    constexpr int kHeight = 384;
    constexpr int kSections = kHeight / 16;
    constexpr int kCells = 4096;
    constexpr uint8_t kDefaultBiome = 1;

    enum Dimension : int { kOverworld = 0, kNether = 1, kEnd = 2, kDimensionCount = 3 };

    struct DimensionRange {
        int minY;
        int height;
        int MaxY() const { return minY + height - 1; }
        int FirstSection() const { return (minY - kMinY) >> 4; }
        int SectionCount() const { return height >> 4; }
    };

    constexpr DimensionRange RangeOf(int dimension) {
        return dimension == kNether ? DimensionRange{0, 128} : dimension == kEnd ? DimensionRange{0, 256} : DimensionRange{kMinY, kHeight};
    }

    // cell index inside a section, the order the wire uses: x outermost, y innermost
    constexpr int CellIndex(int x, int y, int z) {
        return (x << 8) | (z << 4) | (y & 15);
    }

    class ChunkColumn {
    public:
        struct Section {
            uint16_t uniform = 0;
            std::unique_ptr<std::array<uint16_t, kCells>> cells;
        };

        uint16_t Get(int x, int y, int z) const;
        void Set(int x, int y, int z, uint16_t block);

        // bulk fill from a generator: `cells` is in wire order; collapses to uniform when every cell matches
        void SetSection(int section, const uint16_t* cells);
        void FillSection(int section, uint16_t block);

        const Section& SectionAt(int section) const { return sections[static_cast<size_t>(section)]; }
        bool IsUniform(int section) const { return !sections[static_cast<size_t>(section)].cells; }

        uint8_t GenBiome(int x, int z) const { return genBiomes[static_cast<size_t>(x * 16 + z)]; }
        uint8_t NetBiome(int x, int z) const { return netBiomes[static_cast<size_t>(x * 16 + z)]; }
        void SetBiome(int x, int z, uint8_t tableIndex, uint8_t networkId) {
            genBiomes[static_cast<size_t>(x * 16 + z)] = tableIndex;
            netBiomes[static_cast<size_t>(x * 16 + z)] = networkId;
        }
        static constexpr int kQuartLayers = kHeight / 4;
        static constexpr int QuartIndex(int qx, int qy, int qz) { return (qy * 4 + qz) * 4 + qx; }
        bool HasQuartBiomes() const { return hasQuartBiomes; }
        uint8_t QuartBiome(int qx, int qy, int qz) const {
            return hasQuartBiomes ? quartBiomes[static_cast<size_t>(QuartIndex(qx, qy, qz))]
                                  : genBiomes[static_cast<size_t>((qx * 4 + 1) * 16 + qz * 4 + 1)];
        }
        void SetQuartBiomes(const uint8_t* tableIndices, const uint8_t* networkIds) {
            std::copy_n(tableIndices, quartBiomes.size(), quartBiomes.begin());
            std::copy_n(networkIds, quartNet.size(), quartNet.begin());
            hasQuartBiomes = true;
        }
        ChunkColumn Clone() const;

        uint64_t version = 0;

        void EncodeNetwork(std::vector<uint8_t>& out, int dimension = kOverworld) const;

    private:
        std::array<Section, kSections> sections;
        std::array<uint8_t, 256> genBiomes{};
        std::array<uint8_t, 4 * 4 * kHeight / 4> quartBiomes{};
        std::array<uint8_t, 4 * 4 * kHeight / 4> quartNet{};
        bool hasQuartBiomes = false;
        std::array<uint8_t, 256> netBiomes = [] {
            std::array<uint8_t, 256> a{};
            a.fill(kDefaultBiome);
            return a;
        }();
    };
}
