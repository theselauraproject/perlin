#pragma once
#include "render/chunk/FaceMask.hpp"

#include <functional>

namespace perlin::render {
    // one merged rectangle of coplanar faces; 8 bytes so a section of terrain stays small enough to keep many cached
    //   a: x(5) y(5) z(5) w-1(4) h-1(4) face(3)   min corner of the voxel run the quad covers
    //   b: tile(16) | extra(16)                    extra is whatever the caller wants kept apart (light, ao)
    struct PackedQuad {
        uint32_t a = 0;
        uint32_t b = 0;

        int X() const { return static_cast<int>(a & 31); }
        int Y() const { return static_cast<int>((a >> 5) & 31); }
        int Z() const { return static_cast<int>((a >> 10) & 31); }
        int W() const { return static_cast<int>((a >> 15) & 15) + 1; }
        int H() const { return static_cast<int>((a >> 19) & 15) + 1; }
        int FaceIndex() const { return static_cast<int>((a >> 23) & 7); }
        uint16_t Tile() const { return static_cast<uint16_t>(b & 0xFFFF); }
        uint16_t Extra() const { return static_cast<uint16_t>(b >> 16); }

        static PackedQuad Make(int x, int y, int z, int w, int h, int face, uint16_t tile, uint16_t extra) {
            PackedQuad q;
            q.a = static_cast<uint32_t>(x) | static_cast<uint32_t>(y) << 5 | static_cast<uint32_t>(z) << 10 |
                  static_cast<uint32_t>(w - 1) << 15 | static_cast<uint32_t>(h - 1) << 19 |
                  static_cast<uint32_t>(face) << 23;
            q.b = static_cast<uint32_t>(tile) | static_cast<uint32_t>(extra) << 16;
            return q;
        }
        bool operator==(const PackedQuad& o) const { return a == o.a && b == o.b; }
    };

    // extra key for a visible face (light, ambient occlusion...); quads only merge faces with equal tile and extra
    using FaceKeyFn = std::function<uint16_t(int x, int y, int z, int face)>;

    struct MeshStats {
        uint32_t faces = 0;
        uint32_t quads = 0;
    };

    // binary greedy mesher over the visibility bit columns: slices are merged along the first in-plane axis, then
    // extended along the second while whole runs match
    MeshStats GreedyMesh(const SectionBlocks& section, const FaceMask& mask, const BlockTable& table,
                         const FaceKeyFn& extra, std::vector<PackedQuad>& out);

    // the section-local positions of a quad's four corners in counter-clockwise order seen from outside, and the
    // uv extent in blocks (u along the first in-plane axis, v along the second)
    struct QuadCorners {
        float pos[4][3];
        float uv[4][2];
    };
    QuadCorners CornersOf(const PackedQuad& quad);

    // 4 vertices, 6 indices per quad for the plain triangle path
    void AppendTriangles(const PackedQuad& quad, uint32_t baseIndex, std::vector<float>& positions,
                         std::vector<float>& uvs, std::vector<uint32_t>& indices);
}
