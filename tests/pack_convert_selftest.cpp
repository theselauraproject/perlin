#include "client/systems/renderer/gpu/PackConvert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
#include <random>

using namespace perlin;
using namespace perlin::gpu;

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    void ReferenceConvert(const std::vector<Vertex>& verts, const std::vector<QuadInfo>& quads,
                          const PackConvertContext& c, std::vector<uint8_t>& out) {
        const bool haveQuads = quads.size() * 4 == verts.size();
        out.assign(verts.size() * kPackVertexBytes, 0);
        const float inset = 1.0f / (2.0f * static_cast<float>(c.atlasTilePx));
        const float span = 1.0f - 2.0f * inset;
        const float invCols = 1.0f / static_cast<float>(c.gridCols),
                    invRows = 1.0f / static_cast<float>(c.gridRows);
        const auto put = [&](uint8_t* base, uint32_t offset, const void* data, size_t bytes) {
            std::memcpy(base + offset, data, bytes);
        };
        for (size_t q = 0; q + 3 < verts.size(); q += 4) {
            const Vertex* v = &verts[q];
            const glm::vec3 p0(v[0].x, v[0].y, v[0].z), p1(v[1].x, v[1].y, v[1].z),
                p2(v[2].x, v[2].y, v[2].z), p3(v[3].x, v[3].y, v[3].z);
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
            if (glm::dot(n, n) < 1e-12f)
                n = glm::cross(p2 - p0, p3 - p0);
            const float len = glm::length(n);
            n = len > 1e-8f ? n / len : glm::vec3(0, 1, 0);
            const glm::vec3 centre = (p0 + p1 + p2 + p3) * 0.25f;
            glm::vec3 blockOrigin;
            uint16_t block = 0;
            float layerType = 0.0f;
            if (haveQuads) {
                const QuadInfo& info = quads[q / 4];
                blockOrigin = glm::vec3(info.bx, info.by, info.bz);
                block = info.block;
                layerType = (info.layer == static_cast<uint8_t>(RenderLayer::Translucent) ||
                             info.layer == static_cast<uint8_t>(RenderLayer::TranslucentCull))
                                ? 1.0f
                                : 0.0f;
            } else {
                blockOrigin = glm::floor(centre - n * 0.01f);
            }
            const glm::vec3 blockCentre = blockOrigin + glm::vec3(0.5f);
            const float side = glm::dot(n, centre - blockCentre);
            if (std::fabs(side) < 0.15f)
                n = glm::vec3(0, 1, 0);
            else if (side < 0.0f)
                n = -n;
            const glm::vec2 uv0(v[0].u, v[0].v), uv1(v[1].u, v[1].v), uv2(v[2].u, v[2].v);
            const glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
            const glm::vec2 d1 = uv1 - uv0, d2 = uv2 - uv0;
            const float det = d1.x * d2.y - d2.x * d1.y;
            glm::vec3 tangent = std::fabs(det) > 1e-8f ? (e1 * d2.y - e2 * d1.y) / det : e1;
            if (glm::dot(tangent, tangent) < 1e-12f)
                tangent = glm::vec3(1, 0, 0);
            tangent = glm::normalize(tangent);
            int packId = block ? c.packBlockId(block) : -1;
            bool fluid = false;
            {
                const int tile0 = static_cast<int>(v[0].light >> 16);
                const bool waterTile = tile0 >= 0 && (tile0 == c.fluidTiles[0] || tile0 == c.fluidTiles[1]);
                const bool lavaTile = tile0 >= 0 && (tile0 == c.fluidTiles[2] || tile0 == c.fluidTiles[3]);
                if (waterTile || lavaTile) {
                    fluid = true;
                    packId = c.fluidIds[waterTile ? 0 : 1];
                } else if (block) {
                    fluid = c.isFluid(block);
                }
            }
            const float emission = block && c.blockEmission
                                       ? static_cast<float>(std::clamp(c.blockEmission(block), 0, 15))
                                       : 0.0f;
            float au[4], av[4];
            for (int i = 0; i < 4; i++) {
                const uint32_t tile = v[i].light >> 16;
                const int gx = static_cast<int>(tile % static_cast<uint32_t>(c.gridCols)),
                          gy = static_cast<int>(tile / static_cast<uint32_t>(c.gridCols));
                au[i] = (static_cast<float>(gx) + inset + v[i].u * span) * invCols;
                av[i] = (static_cast<float>(gy) + inset + v[i].v * span) * invRows;
            }
            const float mid[2] = {(au[0] + au[1] + au[2] + au[3]) * 0.25f,
                                  (av[0] + av[1] + av[2] + av[3]) * 0.25f};
            const uint16_t normal[4] = {ToHalf(n.x), ToHalf(n.y), ToHalf(n.z), 0};
            const uint16_t tang[4] = {ToHalf(tangent.x), ToHalf(tangent.y), ToHalf(tangent.z), ToHalf(1.0f)};
            (void)layerType;
            if (c.dropBlockIds)
                packId = -1;
            const float entity[2] = {static_cast<float>(packId), fluid ? 1.0f : 0.0f};
            for (int i = 0; i < 4; i++) {
                uint8_t* o = out.data() + (q + static_cast<size_t>(i)) * kPackVertexBytes;
                const float pos[3] = {v[i].x, v[i].y, v[i].z};
                const float ao = std::clamp((v[i].tangent - std::floor(v[i].tangent)) * 16.0f, 0.0f, 1.0f);
                uint32_t abgr = v[i].rgba;
                const uint32_t aoQ = static_cast<uint32_t>(ao * 255.0f + 0.5f);
                if (c.separateAo) {
                    abgr = (abgr & 0x00FFFFFFu) | (aoQ << 24);
                } else if (aoQ != 255u) {
                    uint32_t mixed = abgr & 0xFF000000u;
                    for (int ch = 0; ch < 3; ch++)
                        mixed |= (((abgr >> (ch * 8)) & 0xFFu) * aoQ + 127u) / 255u << (ch * 8);
                    abgr = mixed;
                }
                const float uv[2] = {au[i], av[i]};
                const float light[2] = {static_cast<float>(v[i].light & 0xFF),
                                        c.noSkyLight ? 0.0f : static_cast<float>((v[i].light >> 8) & 0xFF)};
                const glm::vec3 toMid = (blockCentre - glm::vec3(pos[0], pos[1], pos[2])) * 64.0f;
                const uint16_t midBlock[4] = {ToHalf(std::clamp(std::round(toMid.x), -127.0f, 127.0f)),
                                              ToHalf(std::clamp(std::round(toMid.y), -127.0f, 127.0f)),
                                              ToHalf(std::clamp(std::round(toMid.z), -127.0f, 127.0f)),
                                              ToHalf(emission)};
                put(o, kPos, pos, 12);
                put(o, kColor, &abgr, 4);
                put(o, kUv, uv, 8);
                put(o, kLight, light, 8);
                put(o, kNormal, normal, 8);
                put(o, kEntity, entity, 8);
                put(o, kMid, mid, 8);
                put(o, kMidBlock, midBlock, 8);
                put(o, kTangent, tang, 8);
            }
        }
    }

    Vertex RandomVertex(std::mt19937& rng, const glm::vec3& at) {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        Vertex v{};
        v.x = at.x;
        v.y = at.y;
        v.z = at.z;
        v.rgba = static_cast<uint32_t>(rng());
        v.u = unit(rng);
        v.v = unit(rng);
        v.light = (static_cast<uint32_t>(rng() % 20) << 16) | (static_cast<uint32_t>(rng() % 16) << 8) |
                  static_cast<uint32_t>(rng() % 16);
        v.tangent = unit(rng) * 3.0f;
        return v;
    }

    struct World {
        std::vector<Vertex> verts;
        std::vector<QuadInfo> quads;
    };

    World RandomWorld(std::mt19937& rng, int quadCount, bool withInfo) {
        World w;
        std::uniform_int_distribution<int> cell(-4, 4), shape(0, 5);
        for (int i = 0; i < quadCount; i++) {
            const glm::vec3 origin(cell(rng), cell(rng), cell(rng));
            glm::vec3 c[4];
            switch (shape(rng)) {
            case 0:
                c[0] = {0, 1, 0};
                c[1] = {1, 1, 0};
                c[2] = {1, 1, 1};
                c[3] = {0, 1, 1};
                break;
            case 1:
                c[0] = {0, 0, 0};
                c[1] = {0, 0, 1};
                c[2] = {1, 0, 1};
                c[3] = {1, 0, 0};
                break;
            case 2:
                c[0] = {1, 0, 0};
                c[1] = {1, 1, 0};
                c[2] = {1, 1, 1};
                c[3] = {1, 0, 1};
                break;
            case 3:
                c[0] = {0, 0, 0};
                c[1] = {0, 0, 0};
                c[2] = {0, 0, 0};
                c[3] = {0, 0, 0};
                break;
            case 4:
                c[0] = {0.1f, 0.5f, 0};
                c[1] = {0.9f, 0.5f, 0.9f};
                c[2] = {0.5f, 0.5f, 0.45f};
                c[3] = {0.2f, 0.5f, 0.1f};
                break;
            default:
                c[0] = {0, 0, 0.5f};
                c[1] = {1, 0, 0.5f};
                c[2] = {1, 1, 0.5f};
                c[3] = {0, 1, 0.5f};
                break;
            }
            for (const glm::vec3& corner : c)
                w.verts.push_back(RandomVertex(rng, origin + corner));
            if (withInfo) {
                QuadInfo info{};
                info.bx = origin.x;
                info.by = origin.y;
                info.bz = origin.z;
                info.block = static_cast<uint16_t>(rng() % 12);
                info.layer = static_cast<uint8_t>(rng() % 6);
                w.quads.push_back(info);
            }
        }
        return w;
    }

    PackConvertContext Context(bool separateAo, bool noSky, bool drop) {
        PackConvertContext c;
        c.atlasTilePx = 16;
        c.gridCols = 5;
        c.gridRows = 4;
        c.separateAo = separateAo;
        c.noSkyLight = noSky;
        c.dropBlockIds = drop;
        c.fluidTiles = {3, 4, 7, 8};
        c.fluidIds = {100, 101};
        c.packBlockId = [](uint16_t b) { return 10000 + b; };
        c.isFluid = [](uint16_t b) { return b % 5 == 0; };
        c.blockEmission = [](uint16_t b) { return static_cast<int>(b) * 3 - 5; };
        return c;
    }
}

int main() {
    std::mt19937 rng(777);
    int compared = 0;
    for (int trial = 0; trial < 300; trial++) {
        const bool withInfo = trial % 3 != 0;
        const World w = RandomWorld(rng, 1 + static_cast<int>(rng() % 40), withInfo);
        const PackConvertContext ctx = Context(trial % 2 == 0, trial % 5 == 0, trial % 7 == 0);
        std::vector<uint8_t> expected, actual;
        ReferenceConvert(w.verts, w.quads, ctx, expected);
        ConvertQuads(w.verts, w.quads, ctx, actual);
        Check(expected == actual, "converted bytes match the original implementation");
        compared += static_cast<int>(w.verts.size() / 4);
    }
    PackConvertContext noCallbacks;
    World w = RandomWorld(rng, 5, true);
    std::vector<uint8_t> out;
    ConvertQuads(w.verts, w.quads, noCallbacks, out);
    Check(out.size() == w.verts.size() * kPackVertexBytes, "output sized per vertex without callbacks");

    int traced = 0;
    PackConvertContext tracing = Context(false, false, true);
    tracing.traceTranslucent = [&](uint16_t block, int packId, uint32_t) {
        traced++;
        Check(block == 0 || packId >= 0, "trace sees ids before they are dropped");
    };
    int translucent = 0;
    for (const QuadInfo& q : w.quads)
        translucent += q.layer == static_cast<uint8_t>(RenderLayer::Translucent) ||
                       q.layer == static_cast<uint8_t>(RenderLayer::TranslucentCull);
    ConvertQuads(w.verts, w.quads, tracing, out);
    Check(traced == translucent, "trace is called for translucent quads only");

    Check(ToHalf(1.0f) == 0x3C00 && ToHalf(0.0f) == 0 && ToHalf(-2.0f) == 0xC000 && ToHalf(1e9f) == 0x7BFF,
          "half conversion");
    if (failures == 0)
        std::printf("pack_convert_selftest OK (%d quads)\n", compared);
    return failures == 0 ? 0 : 1;
}
