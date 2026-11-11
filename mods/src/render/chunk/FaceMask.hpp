#pragma once
#include "render/chunk/VoxelTypes.hpp"

#include <array>
#include <unordered_map>

namespace perlin::render {
    // bit k of vis[face][q * 16 + p] says the block at depth k along the face axis shows that face.
    // (p, q) are the other two coordinates in axis order: X -> (y, z), Y -> (x, z), Z -> (x, y)
    struct FaceMask {
        std::array<std::array<uint16_t, 256>, FaceCount> vis{};
        // faces left out because the neighbour section was not known yet
        uint8_t borderUnknown = 0;
        uint32_t faceCount = 0;

        bool Empty() const { return faceCount == 0; }
    };

    void ColumnCoords(int axis, int p, int q, int depth, int& x, int& y, int& z);

    FaceMask ComputeFaceMask(const SectionBlocks& section, const SectionNeighbours& neighbours,
                             const BlockTable& table);

    // keeps one mask per section so a remesh after a light or tint change, or a rebuild at another lod, skips the
    // visibility pass; entries are dropped when the section or a neighbour changes
    struct SectionKey {
        int x = 0, y = 0, z = 0;
        bool operator==(const SectionKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct SectionKeyHash {
        size_t operator()(const SectionKey& k) const {
            uint64_t h = static_cast<uint32_t>(k.x) * 0x9E3779B97F4A7C15ull;
            h ^= (static_cast<uint32_t>(k.y) + 0x7F4A7C15u + (h << 6) + (h >> 2)) * 0xC2B2AE3D27D4EB4Full;
            h ^= (static_cast<uint32_t>(k.z) + 0x165667B1u + (h << 6) + (h >> 2)) * 0x9E3779B97F4A7C15ull;
            return static_cast<size_t>(h ^ (h >> 29));
        }
    };

    class FaceMaskCache {
    public:
        // returns the cached mask when `version` matches and every neighbour known now was known when it was built
        const FaceMask& Get(const SectionKey& key, uint64_t version, const SectionBlocks& section,
                            const SectionNeighbours& neighbours, const BlockTable& table);
        void Invalidate(const SectionKey& key) { entries.erase(key); }
        // a block changed in this section: its own mask and the six touching ones are stale
        void InvalidateAround(const SectionKey& key);
        void Clear() { entries.clear(); }
        size_t Size() const { return entries.size(); }
        uint64_t Hits() const { return hits; }
        uint64_t Misses() const { return misses; }

    private:
        struct Entry {
            uint64_t version = 0;
            uint8_t known = 0;
            FaceMask mask;
        };
        std::unordered_map<SectionKey, Entry, SectionKeyHash> entries;
        uint64_t hits = 0, misses = 0;
    };
}
