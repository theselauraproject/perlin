#pragma once
#include "client/renderer/Tessellator.hpp"
#include "world/level/Level.hpp"

#include <array>
#include <functional>
#include <unordered_map>

namespace perlin {
    class BlockLibrary;
    class BiomeTints;

    struct LodCell {
        int16_t y = 0;
        uint8_t r = 0, g = 0, b = 0;
        uint8_t water = 0;
        uint8_t depth = 0;
    };

    struct LodColumn {
        static constexpr int kCells = 4;
        static constexpr int kCellSize = 16 / kCells;
        std::array<LodCell, kCells * kCells> cells;
    };

    class LodStore {
    public:
        static constexpr int kRealChunks = 32;
        static constexpr int kTileChunks = 4;

        static int TileOf(int chunk) {
            return chunk >= 0 ? chunk / kTileChunks : -((-chunk + kTileChunks - 1) / kTileChunks);
        }

        bool Capture(const Level& level, const ChunkPos& pos, const BlockLibrary& blocks,
                     const BiomeTints& tints);
        void Remove(const ChunkPos& pos) {
            if (columns.erase(pos))
                DropFromTile(pos);
        }
        void Clear() {
            columns.clear();
            tiles.clear();
            colours.clear();
        }
        void InvalidateColours() { colours.clear(); }
        const LodColumn* Find(const ChunkPos& pos) const {
            const auto it = columns.find(pos);
            return it == columns.end() ? nullptr : &it->second;
        }
        size_t Size() const { return columns.size(); }
        void Evict(int centreX, int centreZ, int radius, std::vector<ChunkPos>& removed);
        const std::unordered_map<ChunkPos, LodColumn, ChunkPosHash>& All() const { return columns; }

        const std::unordered_map<int64_t, int>& Tiles() const { return tiles; }
        static int64_t TileKeyOf(int tx, int tz) {
            return (static_cast<int64_t>(tx) << 32) | static_cast<uint32_t>(tz);
        }

        bool BuildTile(int tileX, int tileZ, int stride, const std::function<bool(const ChunkPos&)>& hidden,
                       Tessellator& out) const;

        struct Rgb {
            uint8_t r, g, b;
        };
        Rgb ColourOf(uint16_t id, int biome, bool top, const BlockLibrary& blocks, const BiomeTints& tints);

    private:
        std::unordered_map<ChunkPos, LodColumn, ChunkPosHash> columns;
        std::unordered_map<uint32_t, Rgb> colours;
        std::unordered_map<int64_t, int> tiles;
        void DropFromTile(const ChunkPos& pos) {
            const auto it = tiles.find(TileKeyOf(TileOf(pos.x), TileOf(pos.z)));
            if (it != tiles.end() && --it->second <= 0)
                tiles.erase(it);
        }
    };
}
