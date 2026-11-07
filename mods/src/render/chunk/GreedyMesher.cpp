#include "render/chunk/GreedyMesher.hpp"

#include <bit>

namespace perlin::render {
    MeshStats GreedyMesh(const SectionBlocks& section, const FaceMask& mask, const BlockTable& table,
                         const FaceKeyFn& extra, std::vector<PackedQuad>& out) {
        MeshStats stats;
        stats.faces = mask.faceCount;
        const size_t first = out.size();
        uint32_t keys[16][16];
        uint16_t rows[16];

        for (int face = 0; face < FaceCount; face++) {
            const int axis = AxisOf(face);
            const auto& vis = mask.vis[static_cast<size_t>(face)];
            for (int depth = 0; depth < 16; depth++) {
                bool any = false;
                for (int q = 0; q < 16; q++) {
                    uint16_t row = 0;
                    for (int p = 0; p < 16; p++) {
                        if (!((vis[static_cast<size_t>(q * 16 + p)] >> depth) & 1))
                            continue;
                        int x, y, z;
                        ColumnCoords(axis, p, q, depth, x, y, z);
                        const uint16_t tile = table.Tile(section.At(x, y, z), face);
                        const uint16_t more = extra ? extra(x, y, z, face) : 0;
                        keys[q][p] = static_cast<uint32_t>(tile) | static_cast<uint32_t>(more) << 16;
                        row = static_cast<uint16_t>(row | (1u << p));
                    }
                    rows[q] = row;
                    any = any || row != 0;
                }
                if (!any)
                    continue;
                for (int q = 0; q < 16; q++) {
                    while (rows[q]) {
                        const int p = std::countr_zero(static_cast<unsigned>(rows[q]));
                        const uint32_t key = keys[q][p];
                        int w = 1;
                        while (p + w < 16 && ((rows[q] >> (p + w)) & 1) && keys[q][p + w] == key)
                            w++;
                        const uint32_t run = ((1u << w) - 1u) << p;
                        int h = 1;
                        while (q + h < 16 && (rows[q + h] & run) == run) {
                            bool same = true;
                            for (int i = 0; i < w; i++)
                                if (keys[q + h][p + i] != key) {
                                    same = false;
                                    break;
                                }
                            if (!same)
                                break;
                            h++;
                        }
                        for (int i = 0; i < h; i++)
                            rows[q + i] = static_cast<uint16_t>(rows[q + i] & ~run);
                        int x, y, z;
                        ColumnCoords(axis, p, q, depth, x, y, z);
                        out.push_back(PackedQuad::Make(x, y, z, w, h, face,
                                                       static_cast<uint16_t>(key & 0xFFFF),
                                                       static_cast<uint16_t>(key >> 16)));
                    }
                }
            }
        }
        stats.quads = static_cast<uint32_t>(out.size() - first);
        return stats;
    }

    QuadCorners CornersOf(const PackedQuad& quad) {
        const int face = quad.FaceIndex();
        const int axis = AxisOf(face);
        const float x = static_cast<float>(quad.X()), y = static_cast<float>(quad.Y()),
                    z = static_cast<float>(quad.Z());
        const float w = static_cast<float>(quad.W()), h = static_cast<float>(quad.H());
        const float plane = Positive(face) ? 1.0f : 0.0f;
        // corner order in (p, q) space; flipped for faces whose winding would otherwise face inwards
        // 0:(0,0) 1:(w,0) 2:(w,h) 3:(0,h)
        const float pp[4] = {0, w, w, 0};
        const float qq[4] = {0, 0, h, h};
        // X: p=y q=z   Y: p=x q=z   Z: p=x q=y.  The cross product of the p and q axes is +x for X, -y for Y (p=x,q=z gives
        // x cross z = -y) and +z for Z, so the positive faces of X and Z and the negative face of Y wind this way
        const bool natural = axis == 1 ? !Positive(face) : Positive(face);
        const int order[2][4] = {{0, 3, 2, 1}, {0, 1, 2, 3}};
        QuadCorners c{};
        for (int i = 0; i < 4; i++) {
            const int k = order[natural ? 1 : 0][i];
            float px = x, py = y, pz = z;
            switch (axis) {
            case 0:
                px += plane;
                py += pp[k];
                pz += qq[k];
                break;
            case 1:
                px += pp[k];
                py += plane;
                pz += qq[k];
                break;
            default:
                px += pp[k];
                py += qq[k];
                pz += plane;
                break;
            }
            c.pos[i][0] = px;
            c.pos[i][1] = py;
            c.pos[i][2] = pz;
            c.uv[i][0] = pp[k];
            c.uv[i][1] = qq[k];
        }
        return c;
    }

    void AppendTriangles(const PackedQuad& quad, uint32_t baseIndex, std::vector<float>& positions,
                         std::vector<float>& uvs, std::vector<uint32_t>& indices) {
        const QuadCorners c = CornersOf(quad);
        for (int i = 0; i < 4; i++) {
            positions.insert(positions.end(), c.pos[i], c.pos[i] + 3);
            uvs.insert(uvs.end(), c.uv[i], c.uv[i] + 2);
        }
        const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
        for (uint32_t i : idx)
            indices.push_back(baseIndex + i);
    }
}
