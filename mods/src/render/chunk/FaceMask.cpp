#include "render/chunk/FaceMask.hpp"

#include <bit>

namespace perlin::render {
    void ColumnCoords(int axis, int p, int q, int depth, int& x, int& y, int& z) {
        switch (axis) {
        case 0:
            x = depth;
            y = p;
            z = q;
            break;
        case 1:
            x = p;
            y = depth;
            z = q;
            break;
        default:
            x = p;
            y = q;
            z = depth;
            break;
        }
    }

    namespace {
        // opaque/meshed bits for one column along the axis
        struct Column {
            uint16_t opaque = 0;
            uint16_t meshed = 0;
        };
    }

    FaceMask ComputeFaceMask(const SectionBlocks& section, const SectionNeighbours& nb,
                             const BlockTable& table) {
        FaceMask out;
        // one pass over the cells fills the three axis views of the opaque and meshed sets
        std::array<std::array<Column, 256>, 3> cols{};
        if (!section.cells) {
            const uint8_t f = table.Flags(section.uniform);
            if (f & (BlockTable::Opaque | BlockTable::Meshed))
                for (int axis = 0; axis < 3; axis++)
                    for (Column& c : cols[static_cast<size_t>(axis)]) {
                        c.opaque = (f & BlockTable::Opaque) ? 0xFFFF : 0;
                        c.meshed = (f & BlockTable::Meshed) ? 0xFFFF : 0;
                    }
        } else {
            for (int y = 0; y < 16; y++)
                for (int z = 0; z < 16; z++)
                    for (int x = 0; x < 16; x++) {
                        const uint8_t f = table.Flags(section.cells[(y * 16 + z) * 16 + x]);
                        if (!(f & (BlockTable::Opaque | BlockTable::Meshed)))
                            continue;
                        const uint16_t o = (f & BlockTable::Opaque) ? 1 : 0;
                        const uint16_t m = (f & BlockTable::Meshed) ? 1 : 0;
                        Column& cx = cols[0][static_cast<size_t>(z * 16 + y)];
                        Column& cy = cols[1][static_cast<size_t>(z * 16 + x)];
                        Column& cz = cols[2][static_cast<size_t>(y * 16 + x)];
                        cx.opaque = static_cast<uint16_t>(cx.opaque | (o << x));
                        cx.meshed = static_cast<uint16_t>(cx.meshed | (m << x));
                        cy.opaque = static_cast<uint16_t>(cy.opaque | (o << y));
                        cy.meshed = static_cast<uint16_t>(cy.meshed | (m << y));
                        cz.opaque = static_cast<uint16_t>(cz.opaque | (o << z));
                        cz.meshed = static_cast<uint16_t>(cz.meshed | (m << z));
                    }
        }

        for (int face = 0; face < FaceCount; face++) {
            const int axis = AxisOf(face);
            const bool pos = Positive(face);
            const bool known = nb.Known(face);
            if (!known)
                out.borderUnknown = static_cast<uint8_t>(out.borderUnknown | (1u << face));
            for (int q = 0; q < 16; q++)
                for (int p = 0; p < 16; p++) {
                    const Column& c = cols[static_cast<size_t>(axis)][static_cast<size_t>(q * 16 + p)];
                    // is the block just past the section edge opaque
                    bool edgeOpaque = true;
                    if (known) {
                        int x, y, z;
                        ColumnCoords(axis, p, q, pos ? 0 : 15, x, y, z);
                        edgeOpaque = (table.Flags(nb.section[static_cast<size_t>(face)].At(x, y, z)) &
                                      BlockTable::Opaque) != 0;
                    }
                    uint16_t hidden;
                    if (pos)
                        hidden = static_cast<uint16_t>((c.opaque >> 1) | (edgeOpaque ? 0x8000 : 0));
                    else
                        hidden = static_cast<uint16_t>((c.opaque << 1) | (edgeOpaque ? 0x0001 : 0));
                    const uint16_t shown = static_cast<uint16_t>(c.meshed & ~hidden);
                    out.vis[static_cast<size_t>(face)][static_cast<size_t>(q * 16 + p)] = shown;
                    out.faceCount += static_cast<uint32_t>(std::popcount(shown));
                }
        }
        return out;
    }

    const FaceMask& FaceMaskCache::Get(const SectionKey& key, uint64_t version, const SectionBlocks& section,
                                       const SectionNeighbours& neighbours, const BlockTable& table) {
        Entry& e = entries[key];
        // a neighbour that arrived since the build can only reveal faces that were held back
        const bool fresh = e.version == version && e.version != 0 && (neighbours.known & ~e.known) == 0;
        if (fresh) {
            hits++;
            return e.mask;
        }
        misses++;
        e.version = version;
        e.known = neighbours.known;
        e.mask = ComputeFaceMask(section, neighbours, table);
        return e.mask;
    }

    void FaceMaskCache::InvalidateAround(const SectionKey& key) {
        entries.erase(key);
        entries.erase({key.x - 1, key.y, key.z});
        entries.erase({key.x + 1, key.y, key.z});
        entries.erase({key.x, key.y - 1, key.z});
        entries.erase({key.x, key.y + 1, key.z});
        entries.erase({key.x, key.y, key.z - 1});
        entries.erase({key.x, key.y, key.z + 1});
    }
}
