#include <cstdlib>
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/block/BlockLibrary.hpp"
#include "util/Env.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace perlin {
    void Tessellator::Begin() {
        vertices.clear();
        for (auto& list : indices)
            list.clear();
        sections.clear();
        visibility.clear();
        specials.clear();
        quads.clear();
        curBlock = 0;
        layer = RenderLayer::Solid;
        building = true;
    }

    void Tessellator::Reserve(size_t quads) {
        vertices.reserve(quads * 4);
        indices[0].reserve(quads * 6);
    }

    void Tessellator::AddQuad(const float v[4][3], const float c[4][3], const float uv[4][2], bool flip,
                              const float light[4][2], const float* ao, float shade) {
        if (!building)
            return;
        if (PERLIN_ENV("PERLIN_JAVA_DBG")) {
            static int q = 0;
            if (q++ == 0) {
                std::fprintf(stderr,
                             "[javadbg] quad0 v0=(%.2f,%.2f,%.2f) uv0=(%.4f,%.4f) light0=(%.1f,%.1f) "
                             "block=%u flip=%d\n",
                             v[0][0], v[0][1], v[0][2], uv[0][0], uv[0][1], light[0][0], light[0][1],
                             static_cast<unsigned>(curBlock), flip ? 1 : 0);
            }
        }
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        constexpr float kTiles = static_cast<float>(BlockLibrary::kAtlasTiles);
        const float kInset = 1.0f / (2.0f * BlockLibrary::TilePx());
        const float kSpan = 1.0f - 2.0f * kInset;
        const float centerU = (uv[0][0] + uv[1][0] + uv[2][0] + uv[3][0]) * 0.25f * kTiles;
        const float centerV = (uv[0][1] + uv[1][1] + uv[2][1] + uv[3][1]) * 0.25f * kTiles;
        const float column = std::floor(centerU), row = std::floor(centerV);
        const uint32_t tile = static_cast<uint32_t>(row * kTiles + column) << 16;
        // either triangle diagonal preserves the quad winding
        float e0[3] = {v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2]};
        float e1[3] = {v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2]};
        float nx = e0[1] * e1[2] - e0[2] * e1[1];
        float ny = e0[2] * e1[0] - e0[0] * e1[2];
        float nz = e0[0] * e1[1] - e0[1] * e1[0];
        float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl > 1e-9f) {
            nx /= nl;
            ny /= nl;
            nz /= nl;
        } else {
            nx = 0.0f;
            ny = 1.0f;
            nz = 0.0f;
        }
        // clockwise front faces need the opposite of the right-handed edge cross
        nx = -nx;
        ny = -ny;
        nz = -nz;
        // tangent from uv gradients; axis-aligned faces land exactly on an axis
        float d0u = uv[1][0] - uv[0][0], d0v = uv[1][1] - uv[0][1];
        float d1u = uv[2][0] - uv[0][0], d1v = uv[2][1] - uv[0][1];
        float det = d0u * d1v - d1u * d0v;
        float tx = 1.0f, ty = 0.0f, tz = 0.0f, hand = 1.0f;
        if (std::fabs(det) > 1e-12f) {
            const float r = 1.0f / det;
            float txx = (e0[0] * d1v - e1[0] * d0v) * r;
            float tyy = (e0[1] * d1v - e1[1] * d0v) * r;
            float tzz = (e0[2] * d1v - e1[2] * d0v) * r;
            float bxx = (e1[0] * d0u - e0[0] * d1u) * r;
            float byy = (e1[1] * d0u - e0[1] * d1u) * r;
            float bzz = (e1[2] * d0u - e0[2] * d1u) * r;
            float tl = std::sqrt(txx * txx + tyy * tyy + tzz * tzz);
            if (tl > 1e-9f) {
                txx /= tl;
                tyy /= tl;
                tzz /= tl;
            }
            const float cx = ny * bzz - nz * byy, cy = nz * bxx - nx * bzz, cz = nx * byy - ny * bxx;
            if (txx * cx + tyy * cy + tzz * cz < 0.0f)
                hand = -1.0f;
            tx = txx;
            ty = tyy;
            tz = tzz;
        } else if (std::fabs(nx) < 0.5f) {
            tx = 1.0f;
            ty = 0.0f;
            tz = 0.0f;
        } else {
            tx = 0.0f;
            ty = 0.0f;
            tz = 1.0f;
        }
        const float ax = std::fabs(tx), ay = std::fabs(ty), az = std::fabs(tz);
        int axis = 0;
        float along = tx;
        if (ay > ax && ay > az) {
            axis = 2;
            along = ty;
        } else if (az > ax && az > ay) {
            axis = 4;
            along = tz;
        }
        if (along < 0.0f)
            axis++;
        const float tangent = static_cast<float>(axis * 2 + (hand < 0.0f ? 0 : 1));
        // unused normal alpha carries baked shade; tangent fraction carries ao without growing the vertex
        const uint32_t packedNormal = PackColor(nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, nz * 0.5f + 0.5f, shade);
        const float entity = static_cast<float>(curBlock);
        // array textures use tile-local uvs for both corners and midpoints
        const float midU = (centerU - column - kInset) / kSpan, midV = (centerV - row - kInset) / kSpan;
        for (int i = 0; i < 4; i++) {
            const float u = (uv[i][0] * kTiles - column - kInset) / kSpan;
            const float w = (uv[i][1] * kTiles - row - kInset) / kSpan;
            vertices.push_back({v[i][0], v[i][1], v[i][2], PackColor(c[i][0], c[i][1], c[i][2]), u, w,
                                PackLight(light[i][0], light[i][1]) | tile, packedNormal, entity, midU, midV,
                                tangent + std::clamp(ao ? ao[i] : 1.0f, 0.0f, 1.0f) / 16.0f});
        }

        if (CaptureQuadInfo())
            quads.push_back({curX, curY, curZ, curBlock, static_cast<uint8_t>(layer)});

        std::vector<uint32_t>& out = indices[static_cast<size_t>(layer)];
        static constexpr uint32_t kForward[6] = {0, 1, 2, 0, 2, 3};
        static constexpr uint32_t kFlipped[6] = {1, 2, 3, 1, 3, 0};
        static const bool noFlip = std::getenv("PERLIN_NOFLIP") != nullptr;
        const uint32_t* pattern = flip && !noFlip ? kFlipped : kForward;
        for (int i = 0; i < 6; i++)
            out.push_back(base + pattern[i]);
    }

    void Tessellator::AddQuad(const float v[4][3], const float c[4][3], const float uv[4][2], bool flip,
                              float block, float sky) {
        const float light[4][2] = {{block, sky}, {block, sky}, {block, sky}, {block, sky}};
        AddQuad(v, c, uv, flip, light);
    }

    void Tessellator::EnterSection(int sy) {
        if (!building)
            return;
        if (!sections.empty() && sections.back().sy == sy)
            return;
        MeshSection section{};
        section.sy = sy;
        for (int i = 0; i < kRenderLayerCount; i++)
            section.start[i] = static_cast<uint32_t>(indices[i].size());
        sections.push_back(section);
    }

    void Tessellator::End() {
        if (!building)
            return;
        building = false;

        const size_t n = sections.size();
        for (size_t i = 0; i < n; i++)
            for (int l = 0; l < kRenderLayerCount; l++) {
                const uint32_t end =
                    i + 1 < n ? sections[i + 1].start[l] : static_cast<uint32_t>(indices[l].size());
                sections[i].count[l] = end - sections[i].start[l];
            }
        std::erase_if(sections, [](const MeshSection& s) {
            return !s.count[0] && !s.count[1] && !s.count[2] && !s.count[3] && !s.count[4];
        });
    }
}
