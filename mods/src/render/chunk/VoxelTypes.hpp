#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace perlin::render {
    constexpr int kSectionSize = 16;
    constexpr int kSectionCells = kSectionSize * kSectionSize * kSectionSize;

    // face = axis * 2 + (positive ? 1 : 0)
    enum Face : uint8_t { NegX, PosX, NegY, PosY, NegZ, PosZ, FaceCount };

    constexpr int AxisOf(int face) {
        return face >> 1;
    }
    constexpr bool Positive(int face) {
        return (face & 1) != 0;
    }

    // per block id facts the mesher needs; ids past the table behave like air
    struct BlockTable {
        enum : uint8_t {
            // hides the face of a neighbouring meshed block
            Opaque = 1,
            // emits unit cube faces
            Meshed = 2,
        };
        std::vector<uint8_t> flags;
        // texture tile per id and face, id * 6 + face
        std::vector<uint16_t> tiles;

        uint8_t Flags(uint16_t id) const { return id < flags.size() ? flags[id] : 0; }
        uint16_t Tile(uint16_t id, int face) const {
            const size_t i = static_cast<size_t>(id) * 6 + static_cast<size_t>(face);
            return i < tiles.size() ? tiles[i] : 0;
        }
        void Set(uint16_t id, uint8_t f, uint16_t tile) {
            if (flags.size() <= id)
                flags.resize(static_cast<size_t>(id) + 1, 0);
            if (tiles.size() < (static_cast<size_t>(id) + 1) * 6)
                tiles.resize((static_cast<size_t>(id) + 1) * 6, 0);
            flags[id] = f;
            for (int i = 0; i < 6; i++)
                tiles[static_cast<size_t>(id) * 6 + static_cast<size_t>(i)] = tile;
        }
    };

    // 16x16x16 blocks, index (y * 16 + z) * 16 + x; a null cell array means every cell is `uniform`
    struct SectionBlocks {
        const uint16_t* cells = nullptr;
        uint16_t uniform = 0;

        uint16_t At(int x, int y, int z) const { return cells ? cells[(y * 16 + z) * 16 + x] : uniform; }
    };

    // the six sections touching this one; an unknown neighbour counts as opaque so no face is spent on a border that
    // may be hidden, and the mask of known ones tells the owner when a rebuild is due
    struct SectionNeighbours {
        std::array<SectionBlocks, FaceCount> section{};
        uint8_t known = 0;

        void Set(int face, SectionBlocks s) {
            section[static_cast<size_t>(face)] = s;
            known = static_cast<uint8_t>(known | (1u << face));
        }
        bool Known(int face) const { return (known >> face) & 1u; }
    };
}
