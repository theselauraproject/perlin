#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "client/systems/gfx/Tessellator.hpp"
#include "client/systems/blocks/BlockLibrary.hpp"
#include "util/Env.hpp"

#include <bit>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace perlin {
    void Tessellator::Begin() {
        vertices.clear();
        boundsKnown = false;
        for (auto& list : indices)
            list.clear();
        sections.clear();
        visibility.clear();
        specials.clear();
        quads.clear();
        for (auto& groups : pending)
            for (auto& g : groups)
                g.clear();
        curBlock = 0;
        layer = RenderLayer::Solid;
        building = true;
        backSealed = false;
        static const bool noMerge = std::getenv("PERLIN_NO_MERGE") != nullptr;
        merging = MergeFaces() && !CaptureQuadInfo() && !noMerge;
        dead.clear();
        pinned.clear();
        mergedAway = 0;
    }

    namespace {
        // What a face's lighting does across it: the value at its first corner and the change per block
        // along each of its two axes. Colour (AO and tint), the two light levels and the AO kept in the
        // tangent all vary this way, so faces on one plane with the same steps can be drawn as one: the
        // GPU interpolates the big face to the same value at every pixel as the small ones it replaces.
        struct Plane {
            int16_t at[6], stepA[6], stepB[6];
            float aoAt, aoStepA, aoStepB;
        };

        struct MergeScratch {
            int32_t grid[16][256];
            uint8_t code[16][256];
            Plane plane[16][256];
            uint64_t occupied[16][4];
            std::vector<uint32_t> keep;
            MergeScratch() {
                std::memset(grid, 0, sizeof(grid));
                std::memset(code, 0, sizeof(code));
                std::memset(occupied, 0, sizeof(occupied));
            }
        };

        // Everything that must be the same at every corner of every face in a merged rectangle.
        // (The tangent's whole part is the tangent axis and handedness, its fraction the AO; both are >= 0.)
        bool SameConstants(const Vertex& a, const Vertex& b) {
            return a.normal == b.normal && a.entity == b.entity && a.midU == b.midU && a.midV == b.midV &&
                   (a.light >> 16) == (b.light >> 16) &&
                   static_cast<int>(a.tangent) == static_cast<int>(b.tangent);
        }

        void Channels(const Vertex& v, int (&out)[6]) {
            out[0] = static_cast<int>(v.rgba & 0xFF);
            out[1] = static_cast<int>((v.rgba >> 8) & 0xFF);
            out[2] = static_cast<int>((v.rgba >> 16) & 0xFF);
            out[3] = static_cast<int>(v.rgba >> 24);
            out[4] = static_cast<int>(v.light & 0xFF);
            out[5] = static_cast<int>((v.light >> 8) & 0xFF);
        }

        float Ao(const Vertex& v) {
            return v.tangent - static_cast<float>(static_cast<int>(v.tangent));
        }

        // corners[c]: the vertex at corner c (bit 0: along a, bit 1: along b). False when the four corners
        // do not lie on one plane in every value (the two triangles would then differ from a bigger face).
        bool PlaneOf(const Vertex* const (&corners)[4], Plane& out) {
            for (int i = 1; i < 4; i++)
                if (!SameConstants(*corners[0], *corners[i]))
                    return false;
            const Vertex& v0 = *corners[0];
            bool flat = true;
            for (int i = 1; i < 4 && flat; i++)
                flat = corners[i]->rgba == v0.rgba && corners[i]->light == v0.light && corners[i]->tangent == v0.tangent;
            if (flat) {
                // The usual case (open sky, no AO): one value all over.
                int c0[6];
                Channels(v0, c0);
                for (int k = 0; k < 6; k++) {
                    out.at[k] = static_cast<int16_t>(c0[k]);
                    out.stepA[k] = out.stepB[k] = 0;
                }
                out.aoAt = Ao(v0);
                out.aoStepA = out.aoStepB = 0.0f;
                return true;
            }
            int c[4][6];
            for (int i = 0; i < 4; i++)
                Channels(*corners[i], c[i]);
            for (int k = 0; k < 6; k++) {
                if (c[0][k] + c[3][k] != c[1][k] + c[2][k])
                    return false;
                out.at[k] = static_cast<int16_t>(c[0][k]);
                out.stepA[k] = static_cast<int16_t>(c[1][k] - c[0][k]);
                out.stepB[k] = static_cast<int16_t>(c[2][k] - c[0][k]);
            }
            const float ao[4] = {Ao(*corners[0]), Ao(*corners[1]), Ao(*corners[2]), Ao(*corners[3])};
            out.aoAt = ao[0];
            out.aoStepA = ao[1] - ao[0];
            out.aoStepB = ao[2] - ao[0];
            return ao[0] + ao[3] == ao[1] + ao[2] && ao[0] + out.aoStepA == ao[1] && ao[0] + out.aoStepB == ao[2];
        }

        // Whether the face `da` blocks along a and `db` along b from `origin` continues its plane exactly.
        bool Continues(const Plane& origin, const Plane& other, int da, int db) {
            for (int k = 0; k < 6; k++)
                if (other.stepA[k] != origin.stepA[k] || other.stepB[k] != origin.stepB[k] ||
                    other.at[k] != origin.at[k] + da * origin.stepA[k] + db * origin.stepB[k])
                    return false;
            if (other.aoStepA != origin.aoStepA || other.aoStepB != origin.aoStepB)
                return false;
            if (origin.aoStepA == 0.0f && origin.aoStepB == 0.0f)
                return other.aoAt == origin.aoAt;
            return other.aoAt == origin.aoAt + static_cast<float>(da) * origin.aoStepA +
                                     static_cast<float>(db) * origin.aoStepB;
        }

        int UnitBit(float value) {
            if (std::fabs(value) < 2e-3f)
                return 0;
            if (std::fabs(value - 1.0f) < 2e-3f)
                return 1;
            return -1;
        }
    }

    void Tessellator::MergeGroup(std::vector<uint32_t>& group, int normalGroup) {
        static constexpr int kNormalAxis[kFaceGroups] = {0, 1, 2, -1, 2, 1, 0};
        const int n = kNormalAxis[normalGroup];
        const size_t quadCount = group.size() / 6;
        if (n < 0 || quadCount < 2)
            return;
        const int a = n == 0 ? 1 : 0, b = n == 2 ? 1 : 2;
        const bool positive = normalGroup < 3;
        static thread_local MergeScratch scratch;
        std::vector<uint32_t>& keep = scratch.keep;
        keep.clear();
        uint32_t planes = 0;
        size_t candidates = 0;
        for (size_t k = 0; k < quadCount; k++) {
            const uint32_t* idx = &group[k * 6];
            const uint32_t base = idx[0] & ~3u;
            const Vertex* q = &vertices[base];
            const size_t quadIndex = base >> 2;
            bool ok = quadIndex >= pinned.size() || !pinned[quadIndex];
            int plane = 0, la = 0, lb = 0;
            uint8_t code = 0;
            Plane shade{};
            if (ok) {
                const float* p0 = &q[0].x;
                float minA = p0[a], minB = p0[b];
                for (int i = 1; i < 4; i++) {
                    const float* p = &q[i].x;
                    ok = ok && p[n] == p0[n];
                    minA = std::min(minA, p[a]);
                    minB = std::min(minB, p[b]);
                }
                ok = ok && minA == std::floor(minA) && minB == std::floor(minB) && p0[n] == std::floor(p0[n]);
                int seen = 0, ub[4] = {0, 0, 0, 0}, vb[4] = {0, 0, 0, 0};
                const Vertex* corners[4] = {q, q, q, q};
                for (int i = 0; i < 4 && ok; i++) {
                    const float* p = &q[i].x;
                    const float da = p[a] - minA, db = p[b] - minB;
                    if ((da != 0.0f && da != 1.0f) || (db != 0.0f && db != 1.0f)) {
                        ok = false;
                        break;
                    }
                    const int corner = (da != 0.0f ? 1 : 0) | (db != 0.0f ? 2 : 0);
                    const int u = UnitBit(q[i].u), v = UnitBit(q[i].v);
                    if (u < 0 || v < 0) {
                        ok = false;
                        break;
                    }
                    seen |= 1 << corner;
                    ub[corner] = u;
                    vb[corner] = v;
                    corners[corner] = &q[i];
                }
                ok = ok && seen == 15 && ub[3] == ub[1] + ub[2] - ub[0] && vb[3] == vb[1] + vb[2] - vb[0] &&
                     PlaneOf(corners, shade);
                if (ok) {
                    code = static_cast<uint8_t>(ub[0] | (ub[1] << 1) | (ub[2] << 2) | (vb[0] << 3) | (vb[1] << 4) |
                                                (vb[2] << 5) | 0x80);
                    const int along = static_cast<int>(p0[n]);
                    plane = (positive ? along - 1 : along) & 15;
                    la = static_cast<int>(minA) & 15;
                    lb = static_cast<int>(minB) & 15;
                    ok = scratch.grid[plane][lb * 16 + la] == 0;
                }
            }
            if (!ok) {
                keep.insert(keep.end(), idx, idx + 6);
                continue;
            }
            scratch.grid[plane][lb * 16 + la] = static_cast<int32_t>(k) + 1;
            scratch.code[plane][lb * 16 + la] = code;
            scratch.plane[plane][lb * 16 + la] = shade;
            scratch.occupied[plane][lb >> 2] |= 1ull << ((lb & 3) * 16 + la);
            planes |= 1u << plane;
            candidates++;
        }
        if (!candidates)
            return;
        if (dead.size() < vertices.size() / 4)
            dead.resize(vertices.size() / 4, 0);
        for (int plane = 0; plane < 16; plane++) {
            if (!(planes & (1u << plane)))
                continue;
            int32_t* grid = scratch.grid[plane];
            const uint8_t* codes = scratch.code[plane];
            const Plane* shades = scratch.plane[plane];
            uint64_t* occupied = scratch.occupied[plane];
            // The occupied cells in order (b, then a), as the bits of four words.
            for (int word = 0; word < 4; word++)
                for (uint64_t bits = occupied[word]; bits; bits &= bits - 1) {
                    const int cell = word * 64 + std::countr_zero(bits);
                    const int lb = cell >> 4, la = cell & 15;
                    if (!grid[cell])
                        continue;
                    const size_t k = static_cast<size_t>(grid[cell] - 1);
                    const uint32_t* idx = &group[k * 6];
                    const uint32_t base = idx[0] & ~3u;
                    const uint8_t code = codes[cell];
                    const auto matches = [&](int other, int da, int db) {
                        if (!grid[other] || codes[other] != code)
                            return false;
                        const uint32_t otherBase = group[static_cast<size_t>(grid[other] - 1) * 6] & ~3u;
                        return SameConstants(vertices[base], vertices[otherBase]) &&
                               Continues(shades[cell], shades[other], da, db);
                    };
                    int w = 1, h = 1;
                    while (la + w < 16 && matches(cell + w, w, 0))
                        w++;
                    while (lb + h < 16) {
                        bool row = true;
                        for (int x = 0; x < w && row; x++)
                            row = matches(cell + h * 16 + x, x, h);
                        if (!row)
                            break;
                        h++;
                    }
                    // The far corners of the merged face take their values from the faces there.
                    const auto quadAt = [&](int x, int y) -> const Vertex* {
                        return &vertices[group[static_cast<size_t>(grid[cell + y * 16 + x] - 1) * 6] & ~3u];
                    };
                    const Vertex* farA = quadAt(w - 1, 0);
                    const Vertex* farB = quadAt(0, h - 1);
                    const Vertex* farAB = quadAt(w - 1, h - 1);
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            const int other = cell + y * 16 + x;
                            if (other != cell) {
                                dead[(group[static_cast<size_t>(grid[other] - 1) * 6] & ~3u) >> 2] = 1;
                                mergedAway++;
                            }
                        }
                    if (w > 1 || h > 1) {
                        Vertex* q = &vertices[base];
                        float minA = (&q[0].x)[a], minB = (&q[0].x)[b];
                        for (int i = 1; i < 4; i++) {
                            minA = std::min(minA, (&q[i].x)[a]);
                            minB = std::min(minB, (&q[i].x)[b]);
                        }
                        // Corner c of each far face, picked by where it sits (min/max along a and b).
                        const auto cornerOf = [&](const Vertex* quad, bool hiA, bool hiB) -> const Vertex& {
                            float qa = (&quad[0].x)[a], qb = (&quad[0].x)[b];
                            for (int i = 1; i < 4; i++) {
                                qa = std::min(qa, (&quad[i].x)[a]);
                                qb = std::min(qb, (&quad[i].x)[b]);
                            }
                            for (int i = 0; i < 4; i++)
                                if (((&quad[i].x)[a] != qa) == hiA && ((&quad[i].x)[b] != qb) == hiB)
                                    return quad[i];
                            return quad[0];
                        };
                        struct Shade {
                            uint32_t rgba, light;
                            float tangent;
                        } shade[4];
                        for (int i = 0; i < 4; i++) {
                            const bool hiA = (&q[i].x)[a] != minA, hiB = (&q[i].x)[b] != minB;
                            const Vertex& from = hiA && hiB ? cornerOf(farAB, true, true)
                                                 : hiA      ? cornerOf(farA, true, false)
                                                 : hiB      ? cornerOf(farB, false, true)
                                                            : q[i];
                            shade[i] = {from.rgba, from.light, from.tangent};
                        }
                        const int u0 = code & 1, u1 = (code >> 1) & 1, u2 = (code >> 2) & 1;
                        const int v0 = (code >> 3) & 1, v1 = (code >> 4) & 1, v2 = (code >> 5) & 1;
                        for (int i = 0; i < 4; i++) {
                            float* p = &q[i].x;
                            const int da = p[a] != minA ? w : 0, db = p[b] != minB ? h : 0;
                            p[a] = minA + static_cast<float>(da);
                            p[b] = minB + static_cast<float>(db);
                            q[i].u = static_cast<float>(u0 + da * (u1 - u0) + db * (u2 - u0));
                            q[i].v = static_cast<float>(v0 + da * (v1 - v0) + db * (v2 - v0));
                            q[i].rgba = shade[i].rgba;
                            q[i].light = shade[i].light;
                            q[i].tangent = shade[i].tangent;
                        }
                    }
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++)
                            grid[cell + y * 16 + x] = 0;
                    keep.insert(keep.end(), idx, idx + 6);
                }
            occupied[0] = occupied[1] = occupied[2] = occupied[3] = 0;
        }
        group.swap(keep);
    }

    void Tessellator::Compact() {
        if (!mergedAway)
            return;
        const size_t quadCount = vertices.size() / 4;
        dead.resize(quadCount, 0);
        std::vector<uint32_t> remap(quadCount);
        uint32_t live = 0;
        for (size_t qi = 0; qi < quadCount; qi++) {
            remap[qi] = live;
            if (dead[qi])
                continue;
            if (live != qi)
                std::memcpy(&vertices[static_cast<size_t>(live) * 4], &vertices[qi * 4], sizeof(Vertex) * 4);
            live++;
        }
        vertices.resize(static_cast<size_t>(live) * 4);
        pinned.clear();
        for (auto& list : indices)
            for (uint32_t& index : list)
                index = remap[index >> 2] * 4 + (index & 3u);
        dead.clear();
    }

    void Tessellator::Reserve(size_t quads) {
        vertices.reserve(quads * 4);
        // Solid faces wait in the per-direction groups of the section being built before they join the layer.
        indices[static_cast<size_t>(RenderLayer::Solid)].reserve(quads * 6);
        indices[static_cast<size_t>(RenderLayer::CutoutCull)].reserve(quads);
        indices[static_cast<size_t>(RenderLayer::TranslucentCull)].reserve(quads / 2);
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

        nx = -nx;
        ny = -ny;
        nz = -nz;

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

        const uint32_t packedNormal = PackColor(nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, nz * 0.5f + 0.5f, shade);
        const float entity = static_cast<float>(curBlock);

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

        std::vector<uint32_t>* target = &indices[static_cast<size_t>(layer)];
        const int split = SplitSlot(layer);
        if (split >= 0 && !sections.empty()) {
            constexpr float kAxis = 0.99f;
            int group = 3;
            if (nx > kAxis)
                group = 0;
            else if (ny > kAxis)
                group = 1;
            else if (nz > kAxis)
                group = 2;
            else if (nz < -kAxis)
                group = 4;
            else if (ny < -kAxis)
                group = 5;
            else if (nx < -kAxis)
                group = 6;
            target = &pending[static_cast<size_t>(split)][static_cast<size_t>(group)];
        }
        std::vector<uint32_t>& out = *target;
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

    void Tessellator::FlushFaces() {
        if (sections.empty())
            return;
        if (backSealed) {
            backSealed = false;
            return;
        }
        MeshSection& cur = sections.back();
        cur.split = true;
        for (int s = 0; s < kSplitLayers; s++) {
            std::vector<uint32_t>& out =
                indices[static_cast<size_t>(s == 0 ? RenderLayer::Solid : RenderLayer::CutoutCull)];
            for (int g = 0; g < kFaceGroups; g++) {
                std::vector<uint32_t>& group = pending[static_cast<size_t>(s)][static_cast<size_t>(g)];
                if (merging)
                    MergeGroup(group, g);
                cur.faces[static_cast<size_t>(s)][static_cast<size_t>(g)] = static_cast<uint32_t>(group.size());
                out.insert(out.end(), group.begin(), group.end());
                group.clear();
            }
        }
    }

    void Tessellator::EnterSection(int sy) {
        if (!building)
            return;
        if (!sections.empty() && sections.back().sy == sy)
            return;
        FlushFaces();
        MeshSection section{};
        section.sy = sy;
        for (int i = 0; i < kRenderLayerCount; i++)
            section.start[i] = static_cast<uint32_t>(indices[i].size());
        sections.push_back(section);
    }

    void Tessellator::End() {
        if (!building)
            return;
        FlushFaces();
        building = false;
        Compact();

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
        // Give back only a large surplus: a shrink copies the whole array, and the reserve above is close.
        const auto trim = [](auto& list) {
            if (list.capacity() > list.size() + list.size() / 4 + 1024)
                list.shrink_to_fit();
        };
        trim(vertices);
        for (auto& list : indices)
            trim(list);
        trim(quads);
        for (auto& layer : pending)
            for (auto& group : layer) {
                group.clear();
                group.shrink_to_fit();
            }
        dead.clear();
        dead.shrink_to_fit();
        pinned.clear();
        pinned.shrink_to_fit();
    }

    void Tessellator::AppendSection(const Tessellator& from, int sy) {
        if (!building)
            return;
        const MeshSection* src = nullptr;
        for (const MeshSection& s : from.sections)
            if (s.sy == sy) {
                src = &s;
                break;
            }
        if (!src)
            return;
        FlushFaces();
        uint32_t lo = UINT32_MAX, hi = 0;
        for (int l = 0; l < kRenderLayerCount; l++)
            for (uint32_t i = 0; i < src->count[l]; i++) {
                const uint32_t v = from.indices[l][src->start[l] + i];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        MeshSection section = *src;
        for (int l = 0; l < kRenderLayerCount; l++)
            section.start[l] = static_cast<uint32_t>(indices[l].size());
        if (lo <= hi) {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.insert(vertices.end(), from.vertices.begin() + lo, from.vertices.begin() + hi + 1);
            for (int l = 0; l < kRenderLayerCount; l++) {
                std::vector<uint32_t>& out = indices[l];
                const auto first = from.indices[l].begin() + src->start[l];
                for (auto it = first; it != first + src->count[l]; ++it)
                    out.push_back(*it - lo + base);
            }
        }
        sections.push_back(section);
        backSealed = true;
        for (const SpecialBlock& b : from.specials)
            if ((b.y >> 4) == sy)
                specials.push_back(b);
        for (const QuadInfo& q : from.quads)
            if ((static_cast<int>(std::floor(q.by)) >> 4) == sy)
                quads.push_back(q);
    }
}
