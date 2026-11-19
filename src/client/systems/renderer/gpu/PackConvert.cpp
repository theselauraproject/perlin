#include "client/systems/renderer/gpu/PackConvert.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>

namespace perlin::gpu {
    namespace {
        struct QuadBasis {
            glm::vec3 normal;
            glm::vec3 tangent;
            glm::vec3 centre;
            glm::vec3 blockCentre;
        };

        struct QuadClass {
            int packId = -1;
            bool fluid = false;
            float emission = 0.0f;
            bool translucent = false;
            uint16_t block = 0;
        };

        struct AtlasCoords {
            float u[4];
            float v[4];
            float mid[2];
        };

        bool IsTranslucentLayer(uint8_t layer) {
            return layer == static_cast<uint8_t>(RenderLayer::Translucent) ||
                   layer == static_cast<uint8_t>(RenderLayer::TranslucentCull);
        }

        glm::vec3 PositionOf(const Vertex& v) {
            return {v.x, v.y, v.z};
        }

        glm::vec3 RawNormal(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
                            const glm::vec3& p3) {
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
            if (glm::dot(n, n) < 1e-12f)
                n = glm::cross(p2 - p0, p3 - p0);
            const float len = glm::length(n);
            return len > 1e-8f ? n / len : glm::vec3(0, 1, 0);
        }

        glm::vec3 QuadTangent(const Vertex* v, const glm::vec3& p0, const glm::vec3& p1,
                              const glm::vec3& p2) {
            const glm::vec2 uv0(v[0].u, v[0].v), uv1(v[1].u, v[1].v), uv2(v[2].u, v[2].v);
            const glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
            const glm::vec2 d1 = uv1 - uv0, d2 = uv2 - uv0;
            const float det = d1.x * d2.y - d2.x * d1.y;
            glm::vec3 tangent = std::fabs(det) > 1e-8f ? (e1 * d2.y - e2 * d1.y) / det : e1;
            if (glm::dot(tangent, tangent) < 1e-12f)
                tangent = glm::vec3(1, 0, 0);
            return glm::normalize(tangent);
        }

        QuadBasis ComputeBasis(const Vertex* v, const QuadInfo* info) {
            const glm::vec3 p0 = PositionOf(v[0]), p1 = PositionOf(v[1]), p2 = PositionOf(v[2]),
                            p3 = PositionOf(v[3]);
            glm::vec3 n = RawNormal(p0, p1, p2, p3);
            const glm::vec3 centre = (p0 + p1 + p2 + p3) * 0.25f;
            const glm::vec3 blockOrigin =
                info ? glm::vec3(info->bx, info->by, info->bz) : glm::floor(centre - n * 0.01f);
            const glm::vec3 blockCentre = blockOrigin + glm::vec3(0.5f);
            const float side = glm::dot(n, centre - blockCentre);
            if (std::fabs(side) < 0.15f)
                n = glm::vec3(0, 1, 0);
            else if (side < 0.0f)
                n = -n;
            return {n, QuadTangent(v, p0, p1, p2), centre, blockCentre};
        }

        QuadClass Classify(const Vertex* v, const QuadInfo* info, const PackConvertContext& ctx) {
            QuadClass c;
            if (info) {
                c.block = info->block;
                c.translucent = IsTranslucentLayer(info->layer);
            }
            const uint16_t block = c.block;
            c.packId = block && ctx.packBlockId ? ctx.packBlockId(block) : -1;
            const int tile = static_cast<int>(v[0].light >> 16);
            const bool water = tile >= 0 && (tile == ctx.fluidTiles[0] || tile == ctx.fluidTiles[1]);
            const bool lava = tile >= 0 && (tile == ctx.fluidTiles[2] || tile == ctx.fluidTiles[3]);
            if (water || lava) {
                c.fluid = true;
                c.packId = ctx.fluidIds[water ? 0 : 1];
            } else if (block && ctx.isFluid) {
                c.fluid = ctx.isFluid(block);
            }
            if (block && ctx.blockEmission)
                c.emission = static_cast<float>(std::clamp(ctx.blockEmission(block), 0, 15));
            return c;
        }

        AtlasCoords AtlasCoordsOf(const Vertex* v, const PackConvertContext& ctx) {
            const float inset = 1.0f / (2.0f * static_cast<float>(ctx.atlasTilePx));
            const float span = 1.0f - 2.0f * inset;
            const float invCols = 1.0f / static_cast<float>(ctx.gridCols),
                        invRows = 1.0f / static_cast<float>(ctx.gridRows);
            AtlasCoords a;
            for (int i = 0; i < 4; i++) {
                const uint32_t tile = v[i].light >> 16;
                const int gx = static_cast<int>(tile % static_cast<uint32_t>(ctx.gridCols)),
                          gy = static_cast<int>(tile / static_cast<uint32_t>(ctx.gridCols));
                a.u[i] = (static_cast<float>(gx) + inset + v[i].u * span) * invCols;
                a.v[i] = (static_cast<float>(gy) + inset + v[i].v * span) * invRows;
            }
            a.mid[0] = (a.u[0] + a.u[1] + a.u[2] + a.u[3]) * 0.25f;
            a.mid[1] = (a.v[0] + a.v[1] + a.v[2] + a.v[3]) * 0.25f;
            return a;
        }

        uint32_t ShadeColour(uint32_t abgr, float tangentField, bool separateAo) {
            const float ao = std::clamp((tangentField - std::floor(tangentField)) * 16.0f, 0.0f, 1.0f);
            const uint32_t aoQ = static_cast<uint32_t>(ao * 255.0f + 0.5f);
            if (separateAo)
                return (abgr & 0x00FFFFFFu) | (aoQ << 24);
            if (aoQ == 255u)
                return abgr;
            uint32_t mixed = abgr & 0xFF000000u;
            for (int ch = 0; ch < 3; ch++)
                mixed |= (((abgr >> (ch * 8)) & 0xFFu) * aoQ + 127u) / 255u << (ch * 8);
            return mixed;
        }

        void Put(uint8_t* base, uint32_t offset, const void* data, size_t bytes) {
            std::memcpy(base + offset, data, bytes);
        }

        void WriteVertex(uint8_t* o, const Vertex& v, int corner, const QuadBasis& basis,
                         const QuadClass& cls, const AtlasCoords& atlas, const PackConvertContext& ctx) {
            const float pos[3] = {v.x, v.y, v.z};
            const uint32_t abgr = ShadeColour(v.rgba, v.tangent, ctx.separateAo);
            const float uv[2] = {atlas.u[corner], atlas.v[corner]};
            const float light[2] = {static_cast<float>(v.light & 0xFF),
                                    ctx.noSkyLight ? 0.0f : static_cast<float>((v.light >> 8) & 0xFF)};
            const glm::vec3 toMid = (basis.blockCentre - glm::vec3(pos[0], pos[1], pos[2])) * 64.0f;
            const uint16_t midBlock[4] = {ToHalf(std::clamp(std::round(toMid.x), -127.0f, 127.0f)),
                                          ToHalf(std::clamp(std::round(toMid.y), -127.0f, 127.0f)),
                                          ToHalf(std::clamp(std::round(toMid.z), -127.0f, 127.0f)),
                                          ToHalf(cls.emission)};
            const uint16_t normal[4] = {ToHalf(basis.normal.x), ToHalf(basis.normal.y),
                                        ToHalf(basis.normal.z), 0};
            const uint16_t tang[4] = {ToHalf(basis.tangent.x), ToHalf(basis.tangent.y),
                                      ToHalf(basis.tangent.z), ToHalf(1.0f)};
            const float entity[2] = {static_cast<float>(cls.packId), cls.fluid ? 1.0f : 0.0f};
            Put(o, kPos, pos, 12);
            Put(o, kColor, &abgr, 4);
            Put(o, kUv, uv, 8);
            Put(o, kLight, light, 8);
            Put(o, kNormal, normal, 8);
            Put(o, kEntity, entity, 8);
            Put(o, kMid, atlas.mid, 8);
            Put(o, kMidBlock, midBlock, 8);
            Put(o, kTangent, tang, 8);
        }
    }

    void ConvertQuads(const std::vector<Vertex>& verts, const std::vector<QuadInfo>& quads,
                      const PackConvertContext& ctx, std::vector<uint8_t>& out) {
        const bool haveQuads = quads.size() * 4 == verts.size();
        out.assign(verts.size() * kPackVertexBytes, 0);
        for (size_t q = 0; q + 3 < verts.size(); q += 4) {
            const Vertex* v = &verts[q];
            const QuadInfo* info = haveQuads ? &quads[q / 4] : nullptr;
            const QuadBasis basis = ComputeBasis(v, info);
            QuadClass cls = Classify(v, info, ctx);
            const AtlasCoords atlas = AtlasCoordsOf(v, ctx);
            if (cls.translucent && ctx.traceTranslucent)
                ctx.traceTranslucent(cls.block, cls.packId, v[0].rgba);
            if (ctx.dropBlockIds)
                cls.packId = -1;
            for (int i = 0; i < 4; i++)
                WriteVertex(out.data() + (q + static_cast<size_t>(i)) * kPackVertexBytes, v[i], i, basis, cls,
                            atlas, ctx);
        }
    }

}
