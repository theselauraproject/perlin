#include "client/systems/entity/BlockEntityMeshes.hpp"

#include "client/systems/entity/EntityColor.hpp"
#include "client/systems/gfx/Frustum.hpp"
#include "client/systems/text/FormatCodes.hpp"
#include "client/systems/text/McFont.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace perlin::entity {
    namespace {
        const char* const kDyeNames[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime",
                                         "pink",  "gray",   "silver",  "cyan",       "purple", "blue",
                                         "brown", "green",  "red",     "black"};

        std::vector<std::string> WrapSignText(const ui::McFont& font, const std::string& text, float maxWidth,
                                              int maxLines) {
            std::vector<std::string> lines;
            for (size_t start = 0;;) {
                const size_t nl = text.find('\n', start);
                std::string row =
                    text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                while (!row.empty() && font.Width(row) > maxWidth) {
                    size_t cut = row.size();
                    while (cut > 1 && font.Width(row.substr(0, cut)) > maxWidth)
                        cut--;
                    const size_t space = row.rfind(' ', cut);
                    size_t take = space != std::string::npos && space > 0 ? space : cut;
                    lines.push_back(row.substr(0, take));
                    row.erase(0, take);
                    while (!row.empty() && row[0] == ' ')
                        row.erase(0, 1);
                }
                lines.push_back(row);
                if (nl == std::string::npos)
                    break;
                start = nl + 1;
            }
            if (static_cast<int>(lines.size()) > maxLines)
                lines.resize(static_cast<size_t>(maxLines));
            return lines;
        }

        struct SkullPart {
            int parent;
            glm::vec3 offset, rotation;
            float scale;
        };
        struct SkullBox {
            int part;
            float x, y, z, w, h, d, u, v, grow;
            bool mirror;
        };
        struct SkullModelDef {
            const char* texture;
            float texW, texH;
            std::vector<SkullPart> parts;
            std::vector<SkullBox> boxes;
            int flagPart = -1;
            int lidPart = -1;
        };

        const SkullModelDef& SkullModelFor(int kind) {
            static const std::vector<SkullModelDef> models = [] {
                std::vector<SkullModelDef> m(7);
                const auto plain = [](const char* tex, float tw, float th, bool hat) {
                    SkullModelDef d{tex,
                                    tw,
                                    th,
                                    {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f}},
                                    {{0, -4, -8, -4, 8, 8, 8, 0, 0, 0, false}}};
                    if (hat)
                        d.boxes.push_back({0, -4, -8, -4, 8, 8, 8, 32, 0, 0.25f, false});
                    return d;
                };
                m[0] = plain("textures/entity/skeleton/skeleton", 64, 32, false);
                m[1] = plain("textures/entity/skeleton/wither_skeleton", 64, 32, false);
                m[2] = plain("textures/entity/zombie/zombie", 64, 32, true);
                m[3] = plain("textures/entity/steve", 64, 64, true);
                m[4] = plain("textures/entity/creeper/creeper", 64, 32, false);
                m[5] = SkullModelDef{"textures/entity/dragon/dragon",
                                     256,
                                     256,
                                     {{-1, glm::vec3(0.0f, -7.986666f, 0.0f), glm::vec3(0.0f), 0.75f},
                                      {0, glm::vec3(0.0f, 4.0f, -8.0f), glm::vec3(0.2f, 0.0f, 0.0f), 1.0f}},
                                     {{0, -6, -1, -24, 12, 5, 16, 176, 44, 0, false},
                                      {0, -8, -8, -10, 16, 16, 16, 112, 30, 0, false},
                                      {0, -5, -12, -4, 2, 4, 6, 0, 0, 0, true},
                                      {0, -5, -3, -22, 2, 2, 4, 112, 0, 0, true},
                                      {0, 3, -12, -4, 2, 4, 6, 0, 0, 0, false},
                                      {0, 3, -3, -22, 2, 2, 4, 112, 0, 0, false},
                                      {1, -6, 0, -16, 12, 4, 16, 176, 65, 0, false}}};
                m[6] = SkullModelDef{"textures/entity/piglin/piglin",
                                     64,
                                     64,
                                     {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f},
                                      {0, glm::vec3(4.5f, -6.0f, 0.0f), glm::vec3(0.0f, 0.0f, -0.7f), 1.0f},
                                      {0, glm::vec3(-4.5f, -6.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.7f), 1.0f}},
                                     {{0, -5, -8, -4, 10, 8, 8, 0, 0, 0, false},
                                      {0, -2, -4, -5, 4, 4, 1, 31, 1, 0, false},
                                      {0, 2, -2, -5, 1, 2, 1, 2, 4, 0, false},
                                      {0, -3, -2, -5, 1, 2, 1, 2, 0, 0, false},
                                      {1, 0, 0, -2, 1, 5, 4, 51, 6, 0, false},
                                      {2, -1, 0, -2, 1, 5, 4, 39, 6, 0, false}}};
                static const char* const kCopper[] = {"textures/entity/copper_golem/copper_golem",
                                                      "textures/entity/copper_golem/copper_golem_exposed",
                                                      "textures/entity/copper_golem/copper_golem_weathered",
                                                      "textures/entity/copper_golem/copper_golem_oxidized"};
                for (const char* tex : kCopper)
                    m.push_back(SkullModelDef{tex,
                                              64,
                                              64,
                                              {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f},
                                               {0, glm::vec3(0.0f, -5.0f, 0.0f), glm::vec3(0.0f), 1.0f},
                                               {1, glm::vec3(0.0f, -6.0f, 0.0f), glm::vec3(0.0f), 1.0f},
                                               {1, glm::vec3(-4.0f, -6.0f, 0.0f), glm::vec3(0.0f), 1.0f},
                                               {1, glm::vec3(4.0f, -6.0f, 0.0f), glm::vec3(0.0f), 1.0f},
                                               {0, glm::vec3(0.0f, -5.0f, 0.0f), glm::vec3(0.0f), 1.0f}},
                                              {{1, -4, -6, -3, 8, 6, 6, 0, 15, 0, false},
                                               {2, -4, -5, -5, 8, 5, 10, 0, 0, 0.015f, false},
                                               {2, -1, -2, -6, 2, 3, 2, 56, 0, 0, false},
                                               {2, -1, -9, -1, 2, 4, 2, 37, 8, -0.015f, false},
                                               {2, -2, -13, -2, 4, 4, 4, 37, 0, -0.015f, false},
                                               {3, -3, -1, -2, 3, 10, 4, 36, 16, 0, false},
                                               {4, 0, -1, -2, 3, 10, 4, 50, 16, 0, false},
                                               {5, -4, 0, -2, 4, 5, 4, 0, 27, 0, false},
                                               {5, 0, 0, -2, 4, 5, 4, 16, 27, 0, false}}});
                m.push_back(SkullModelDef{"textures/blocks/conduit_base",
                                          32,
                                          16,
                                          {{-1, glm::vec3(0.0f, -8.0f, 0.0f), glm::vec3(0.0f), 1.0f}},
                                          {{0, -3, -3, -3, 6, 6, 6, 0, 0, 0, false}}});
                m.push_back(SkullModelDef{"textures/entity/banner/banner_base",
                                          64,
                                          64,
                                          {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 2.0f / 3.0f},
                                           {0, glm::vec3(0.0f, -44.0f, 0.0f), glm::vec3(0.0f), 1.0f}},
                                          {{0, -1, -42, -1, 2, 42, 2, 44, 0, 0, false},
                                           {0, -10, -44, -1, 20, 2, 2, 0, 42, 0, false},
                                           {1, -10, 0, -2, 20, 40, 1, 0, 0, 0, false}},
                                          1});
                m.push_back(SkullModelDef{"textures/entity/banner/banner_base",
                                          64,
                                          64,
                                          {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 2.0f / 3.0f},
                                           {0, glm::vec3(0.0f, -20.5f, 10.5f), glm::vec3(0.0f), 1.0f}},
                                          {{0, -10, -20.5f, 9.5f, 20, 2, 2, 0, 42, 0, false},
                                           {1, -10, 0, -2, 20, 40, 1, 0, 0, 0, false}},
                                          1});
                const auto chest = [](const char* tex, bool large) {
                    const float w = large ? 30.0f : 14.0f;
                    SkullModelDef d{tex,
                                    large ? 128.0f : 64.0f,
                                    64.0f,
                                    {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f},
                                     {0, glm::vec3(-7.0f, -9.0f, 7.0f), glm::vec3(0.0f), 1.0f}},
                                    {{0, -7, -10, -7, w, 10, 14, 0, 19, 0, false},
                                     {1, 0, -5, -14, w, 5, 14, 0, 0, 0, false},
                                     {1, large ? 14.0f : 6.0f, -2, -15, 2, 4, 1, 0, 0, 0, false}}};
                    d.lidPart = 1;
                    return d;
                };
                for (const char* tex :
                     {"textures/entity/chest/normal", "textures/entity/chest/trapped",
                      "textures/entity/chest/ender", "textures/entity/chest/copper_default",
                      "textures/entity/chest/copper_exposed", "textures/entity/chest/copper_weathered",
                      "textures/entity/chest/copper_oxidized"})
                    m.push_back(chest(tex, false));
                for (const char* tex :
                     {"textures/entity/chest/double_normal", "textures/entity/chest/trapped_double",
                      "textures/entity/chest/copper_default_double",
                      "textures/entity/chest/copper_exposed_double",
                      "textures/entity/chest/copper_weathered_double",
                      "textures/entity/chest/copper_oxidized_double"})
                    m.push_back(chest(tex, true));
                return m;
            }();
            return models[static_cast<size_t>(std::clamp(kind, 0, static_cast<int>(models.size()) - 1))];
        }

        glm::mat4 SkullPartMatrix(const SkullModelDef& def, int part, float flagXRot = 0.0f) {
            if (part < 0)
                return glm::mat4(1.0f);
            const SkullPart& p = def.parts[static_cast<size_t>(part)];
            glm::mat4 m = SkullPartMatrix(def, p.parent, flagXRot);
            m = glm::translate(m, p.offset);
            m = glm::rotate(m, p.rotation.z, glm::vec3(0, 0, 1));
            m = glm::rotate(m, p.rotation.y, glm::vec3(0, 1, 0));
            m = glm::rotate(m, p.rotation.x + (part == def.flagPart || part == def.lidPart ? flagXRot : 0.0f),
                            glm::vec3(1, 0, 0));
            return glm::scale(m, glm::vec3(p.scale));
        }

        struct Placement {
            glm::vec3 base;
            float cs, sn;

            void Rotate(float lx, float lz, float& ox, float& oz) const {
                const float dx = lx - 0.5f, dz = lz - 0.5f;
                ox = 0.5f + dx * cs - dz * sn;
                oz = 0.5f + dx * sn + dz * cs;
            }
        };

        void EmitBed(MeshBatch& batch, const BlockEntityDraw& d, const Placement& at, const glm::vec3& light,
                     bool packLit) {
            const glm::vec3& base = at.base;
            const auto rotate = [&](float lx, float lz, float& ox, float& oz) { at.Rotate(lx, lz, ox, oz); };
            struct BFace {
                int dir;
                float u0, v0, u1, v1;
                int tile;
            };
            struct BBox {
                float from[3], to[3];
                std::vector<BFace> faces;
            };
            static const BBox kFoot[] = {
                {{0, 3, 0},
                 {16, 9, 16},
                 {{0, 0, 0, 16, 16, 8},
                  {1, 0, 0, 16, 16, 0},
                  {3, 0, 7, 16, 13, 1},
                  {4, 0, 7, 16, 13, 3},
                  {5, 0, 7, 16, 13, 2}}},
                {{0, 0, 13},
                 {3, 3, 16},
                 {{0, 7, 13, 10, 16, 3},
                  {2, 10, 13, 13, 16, 3},
                  {3, 0, 13, 3, 16, 1},
                  {4, 13, 13, 16, 16, 3},
                  {5, 3, 13, 6, 16, 1}}},
                {{13, 0, 13},
                 {16, 3, 16},
                 {{0, 6, 13, 9, 16, 2},
                  {2, 3, 13, 6, 16, 2},
                  {3, 13, 13, 16, 16, 1},
                  {4, 10, 13, 13, 16, 1},
                  {5, 0, 13, 3, 16, 2}}},
            };
            static const BBox kHead[] = {
                {{0, 3, 0},
                 {16, 9, 16},
                 {{0, 0, 0, 16, 16, 8},
                  {1, 0, 0, 16, 16, 4},
                  {2, 0, 7, 16, 13, 5},
                  {4, 0, 7, 16, 13, 7},
                  {5, 0, 7, 16, 13, 6}}},
                {{0, 0, 0},
                 {3, 3, 3},
                 {{0, 6, 13, 9, 16, 7},
                  {2, 13, 13, 16, 16, 5},
                  {3, 3, 13, 6, 16, 7},
                  {4, 0, 13, 3, 16, 7},
                  {5, 10, 13, 13, 16, 5}}},
                {{13, 0, 0},
                 {16, 3, 3},
                 {{0, 7, 13, 10, 16, 6},
                  {2, 0, 13, 3, 16, 5},
                  {3, 10, 13, 13, 16, 6},
                  {4, 3, 13, 6, 16, 5},
                  {5, 13, 13, 16, 16, 6}}},
            };
            const float kShade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
            const auto emitPiece = [&](const BBox* boxes, int count, float zOff) {
                for (int bi = 0; bi < count; bi++) {
                    const BBox& bx = boxes[bi];
                    const float x0 = bx.from[0], y0 = bx.from[1], z0 = bx.from[2], x1 = bx.to[0],
                                y1 = bx.to[1], z1 = bx.to[2];
                    const auto P = [&](float x, float y, float z) {
                        float ox, oz;
                        rotate(x / 16.0f, z / 16.0f + zOff, ox, oz);
                        return base + glm::vec3(ox, y / 16.0f, oz);
                    };
                    for (const BFace& f : bx.faces) {
                        glm::vec3 tl, tr, br, bl;
                        switch (f.dir) {
                        case 0:
                            tl = P(x0, y0, z1);
                            tr = P(x1, y0, z1);
                            br = P(x1, y0, z0);
                            bl = P(x0, y0, z0);
                            break;
                        case 1:
                            tl = P(x0, y1, z0);
                            tr = P(x1, y1, z0);
                            br = P(x1, y1, z1);
                            bl = P(x0, y1, z1);
                            break;
                        case 2:
                            tl = P(x1, y1, z0);
                            tr = P(x0, y1, z0);
                            br = P(x0, y0, z0);
                            bl = P(x1, y0, z0);
                            break;
                        case 3:
                            tl = P(x0, y1, z1);
                            tr = P(x1, y1, z1);
                            br = P(x1, y0, z1);
                            bl = P(x0, y0, z1);
                            break;
                        case 4:
                            tl = P(x0, y1, z0);
                            tr = P(x0, y1, z1);
                            br = P(x0, y0, z1);
                            bl = P(x0, y0, z0);
                            break;
                        default:
                            tl = P(x1, y1, z1);
                            tr = P(x1, y1, z0);
                            br = P(x1, y0, z0);
                            bl = P(x1, y0, z1);
                            break;
                        }
                        const float tu = static_cast<float>((f.tile % 4) * 16),
                                    tv = static_cast<float>((f.tile / 4) * 16);
                        const float uu0 = (tu + f.u0) / 64.0f, vv0 = (tv + f.v0) / 48.0f,
                                    uu1 = (tu + f.u1) / 64.0f, vv1 = (tv + f.v1) / 48.0f;
                        const uint32_t color = PackColor(light * (packLit ? 1.0f : kShade[f.dir]));
                        const uint32_t i0 = static_cast<uint32_t>(batch.vertices.size());
                        batch.vertices.push_back({tl.x, tl.y, tl.z, color, uu0, vv0});
                        batch.vertices.push_back({tr.x, tr.y, tr.z, color, uu1, vv0});
                        batch.vertices.push_back({br.x, br.y, br.z, color, uu1, vv1});
                        batch.vertices.push_back({bl.x, bl.y, bl.z, color, uu0, vv1});
                        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                            batch.indices.push_back(i0 + k);
                    }
                }
            };
            emitPiece(kFoot, 3, 0.0f);
            emitPiece(kHead, 3, -1.0f);
        }

        void EmitSkull(std::map<std::string, OverlayMeshBatch>& skulls, const BlockEntityDraw& d, const Placement& at,
                       const glm::vec3& light, bool packLit, double worldTicks) {
            const glm::vec3& base = at.base;
            const SkullModelDef& def = SkullModelFor(d.skull);
            const bool banner = def.flagPart >= 0;
            glm::mat4 place =
                glm::translate(glm::mat4(1.0f), base + glm::vec3(0.5f, d.onWall ? 0.25f : 0.0f, 0.5f));
            place = glm::rotate(place, -(d.yaw + 180.0f) * kDeg, glm::vec3(0, 1, 0));
            if (d.onWall)
                place = glm::translate(place, glm::vec3(0.0f, 0.0f, 0.25f));
            place = glm::scale(place, glm::vec3(-1.0f / 16.0f, -1.0f / 16.0f, 1.0f / 16.0f));
            float flagXRot = 0.0f;
            if (banner) {
                const double t = worldTicks + static_cast<double>(d.origin.x * 7.0 + d.origin.y * 9.0 +
                                                                     d.origin.z * 13.0);
                const float phase = static_cast<float>(t - std::floor(t / 100.0) * 100.0) / 100.0f;
                flagXRot = (-0.0125f + 0.01f * std::cos(6.2831853f * phase)) * 3.14159265f;
            }
            if (def.lidPart >= 0)
                flagXRot = -d.lid * 1.5707963f;
            const auto emitBox = [&](OverlayMeshBatch& batch, const SkullBox& b, float extraGrow,
                                     const glm::vec3& tint) {
                const glm::mat4 m = place * SkullPartMatrix(def, b.part, flagXRot);
                const float g = b.grow + extraGrow;
                float x0 = b.x - g, x1 = b.x + b.w + g;
                const float y0 = b.y - g, y1 = b.y + b.h + g, z0 = b.z - g, z1 = b.z + b.d + g;
                if (b.mirror)
                    std::swap(x0, x1);
                const auto P = [&](float x, float y, float z) {
                    return glm::vec3(m * glm::vec4(x, y, z, 1.0f));
                };
                const float u = b.u, v = b.v, w = b.w, h = b.h, dd = b.d;
                const auto face = [&](glm::vec3 tl, glm::vec3 tr, glm::vec3 br, glm::vec3 bl, float fu,
                                      float fv, float fw, float fh, float shade) {
                    const uint32_t color = PackColor(light * (packLit ? 1.0f : shade) * tint);
                    const uint32_t i0 = static_cast<uint32_t>(batch.vertices.size());
                    const float uu0 = fu / def.texW, vv0 = fv / def.texH, uu1 = (fu + fw) / def.texW,
                                vv1 = (fv + fh) / def.texH;
                    batch.vertices.push_back({tl.x, tl.y, tl.z, color, uu0, vv0});
                    batch.vertices.push_back({tr.x, tr.y, tr.z, color, uu1, vv0});
                    batch.vertices.push_back({br.x, br.y, br.z, color, uu1, vv1});
                    batch.vertices.push_back({bl.x, bl.y, bl.z, color, uu0, vv1});
                    for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                        batch.indices.push_back(i0 + k);
                };
                face(P(x0, y0, z0), P(x1, y0, z0), P(x1, y1, z0), P(x0, y1, z0), u + dd, v + dd, w, h,
                     0.8f);
                face(P(x0, y0, z1), P(x0, y0, z0), P(x0, y1, z0), P(x0, y1, z1), u, v + dd, dd, h, 0.6f);
                face(P(x1, y0, z0), P(x1, y0, z1), P(x1, y1, z1), P(x1, y1, z0), u + dd + w, v + dd, dd,
                     h, 0.6f);
                face(P(x1, y0, z1), P(x0, y0, z1), P(x0, y1, z1), P(x1, y1, z1), u + 2 * dd + w, v + dd,
                     w, h, 0.8f);
                face(P(x0, y0, z1), P(x1, y0, z1), P(x1, y0, z0), P(x0, y0, z0), u + dd, v, w, dd, 1.0f);
                face(P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1), u + dd + w, v, w, dd,
                     0.5f);
            };
            const auto dyeTint = [](int dye) {
                const uint32_t rgb = DyeColorRgb(dye);
                return glm::vec3(static_cast<float>((rgb >> 16) & 0xFF),
                                 static_cast<float>((rgb >> 8) & 0xFF), static_cast<float>(rgb & 0xFF)) /
                       255.0f;
            };
            OverlayMeshBatch& batch = skulls[def.texture];
            for (const SkullBox& b : def.boxes)
                emitBox(batch, b, 0.0f,
                        banner && b.part == def.flagPart ? dyeTint(d.color) : glm::vec3(1.0f));
            if (banner) {
                int layer = 0;
                for (const auto& [texture, dye] : d.patterns) {
                    layer++;
                    OverlayMeshBatch& over = skulls[texture];
                    over.overlay = true;
                    for (const SkullBox& b : def.boxes)
                        if (b.part == def.flagPart)
                            emitBox(over, b, 0.004f * static_cast<float>(layer), dyeTint(dye));
                }
            }
        }

        void EmitSignText(MeshBatch& text, const BlockEntityDraw& d, const Placement& at, const glm::vec3& light,
                          ui::McFont& font) {
            const glm::vec3& base = at.base;
            const auto rotate = [&](float lx, float lz, float& ox, float& oz) { at.Rotate(lx, lz, ox, oz); };
            float centerY, frontZ, backZ;
            float scale, maxWidth, lineHeight;
            if (d.hanging) {
                centerY = 7.0f / 16.0f;
                frontZ = 9.0f / 16.0f + 0.002f;
                backZ = 7.0f / 16.0f - 0.002f;
                scale = 0.0140625f;
                maxWidth = 60.0f;
                lineHeight = 9.0f;
            } else if (d.wall) {
                centerY = 8.5f / 16.0f;
                frontZ = 1.5f / 16.0f + 0.002f;
                backZ = -1.0f;
                scale = 0.0104167f;
                maxWidth = 90.0f;
                lineHeight = 10.0f;
            } else {
                centerY = 13.0f / 16.0f;
                frontZ = 8.6667f / 16.0f + 0.002f;
                backZ = 7.3333f / 16.0f - 0.002f;
                scale = 0.0104167f;
                maxWidth = 90.0f;
                lineHeight = 10.0f;
            }
            const float centerZ = d.hanging ? 0.5f : d.wall ? 0.5f : 0.5f;
            (void)centerZ;
            const auto emit = [&](const std::string& body, uint32_t argb, bool glow, bool frontSide,
                                  float planeZ) {
                if (body.empty() || planeZ < -0.5f)
                    return;
                const std::vector<std::string> lines = WrapSignText(font, body, maxWidth, 4);
                const float dir = frontSide ? 1.0f : -1.0f;
                const float rx = frontSide ? 1.0f : -1.0f;
                const float baseR = static_cast<float>(argb & 0xFF) / 255.0f,
                            baseG = static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
                            baseB = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
                const glm::vec3 ink(baseB, baseG, baseR);
                const glm::vec3 lit = glow ? glm::vec3(1.0f) : light;
                const float top = -static_cast<float>(lines.size()) * lineHeight * 0.5f;

                ui::FormatState style;
                for (size_t row = 0; row < lines.size(); row++) {
                    const std::string& line = lines[row];
                    const float width = font.Width(style.Codes() + line);
                    float pen = -width * 0.5f;
                    const float y0 = top + static_cast<float>(row) * lineHeight;
                    for (size_t i = 0; i < line.size();) {
                        uint32_t cp = 0;
                        if (!ui::McFont::Next(line, i, cp))
                            break;
                        if (cp == 0xA7) {
                            uint32_t code = 0;
                            if (ui::McFont::Next(line, i, code))
                                style.Apply(code);
                            continue;
                        }
                        if (cp < 0x20)
                            continue;
                        const glm::vec3 col = style.hasColor
                                                  ? glm::vec3(static_cast<float>((style.rgb >> 16) & 0xFF),
                                                              static_cast<float>((style.rgb >> 8) & 0xFF),
                                                              static_cast<float>(style.rgb & 0xFF)) /
                                                        255.0f
                                                  : ink;
                        const bool bold = style.bold;
                        const ui::McFont::Glyph& g = font.Get(style.obfuscated ? font.Obfuscated(cp) : cp);
                        if (g.width > 0) {
                            const uint32_t color = PackColor(g.colored ? glm::vec3(1.0f) : col * lit);
                            for (int pass = 0; pass < (bold ? 2 : 1); pass++) {
                                const float gx0 = pen + g.left + static_cast<float>(pass),
                                            gx1 = gx0 + static_cast<float>(g.width);
                                const auto corner = [&](float gx, float gy) {
                                    float ox, oz;
                                    const float lx = 0.5f + rx * gx * scale, lz = planeZ + dir * 0.0f;
                                    rotate(lx, lz, ox, oz);
                                    return base + glm::vec3(ox, centerY - gy * scale, oz);
                                };
                                const uint32_t i0 = static_cast<uint32_t>(text.vertices.size());
                                const glm::vec3 p0 = corner(gx0, y0 + g.top + g.height),
                                                p1 = corner(gx1, y0 + g.top + g.height),
                                                p2 = corner(gx1, y0 + g.top), p3 = corner(gx0, y0 + g.top);
                                text.vertices.push_back({p0.x, p0.y, p0.z, color, g.u0, g.v1});
                                text.vertices.push_back({p1.x, p1.y, p1.z, color, g.u1, g.v1});
                                text.vertices.push_back({p2.x, p2.y, p2.z, color, g.u1, g.v0});
                                text.vertices.push_back({p3.x, p3.y, p3.z, color, g.u0, g.v0});
                                for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                                    text.indices.push_back(i0 + k);
                            }
                        }
                        pen += static_cast<float>(g.advance) + (bold ? 1.0f : 0.0f);
                    }
                }
            };
            emit(d.front, d.frontColor, d.frontGlow, true, frontZ);
            emit(d.back, d.backColor, d.backGlow, false, backZ);
        }
    }

    const char* SkullBlockName(const std::string& texture) {
        if (texture.contains("banner"))
            return "minecraft:white_banner";
        if (texture.contains("wither"))
            return "minecraft:wither_skeleton_skull";
        if (texture.contains("skeleton"))
            return "minecraft:skeleton_skull";
        if (texture.contains("zombie"))
            return "minecraft:zombie_head";
        if (texture.contains("creeper"))
            return "minecraft:creeper_head";
        if (texture.contains("dragon"))
            return "minecraft:dragon_head";
        if (texture.contains("piglin"))
            return "minecraft:piglin_head";
        if (texture.contains("chest"))
            return texture.contains("ender")
                       ? "minecraft:ender_chest"
                       : (texture.contains("trapped") ? "minecraft:trapped_chest" : "minecraft:chest");
        return "minecraft:player_head";
    }

    const char* BedBlockName(int color) {
        static const char* const kBedBlocks[] = {
            "minecraft:white_bed",      "minecraft:orange_bed", "minecraft:magenta_bed",
            "minecraft:light_blue_bed", "minecraft:yellow_bed", "minecraft:lime_bed",
            "minecraft:pink_bed",       "minecraft:gray_bed",   "minecraft:light_gray_bed",
            "minecraft:cyan_bed",       "minecraft:purple_bed", "minecraft:blue_bed",
            "minecraft:brown_bed",      "minecraft:green_bed",  "minecraft:red_bed",
            "minecraft:black_bed"};
        return kBedBlocks[color];
    }

    std::string BedTextureName(int color) { return std::string("bed_") + kDyeNames[color] + ".png"; }

    BlockEntityBatches BuildBlockEntityBatches(const std::vector<BlockEntityDraw>& draws, const BlockEntityScene& scene) {
        BlockEntityBatches out;
        glm::ivec2 curLevels(-1, -1);
        const auto sync = [&](MeshBatch& batch) {
            if (!scene.packLit)
                return;
            while (batch.light.size() < batch.vertices.size() * 2) {
                batch.light.push_back(static_cast<int8_t>(curLevels.x));
                batch.light.push_back(static_cast<int8_t>(curLevels.y));
            }
        };
        const double maxDist = 64.0;
        for (const BlockEntityDraw& d : draws) {
            const glm::dvec3 centre = d.origin + glm::dvec3(0.5, 0.5, 0.5);
            const glm::dvec3 delta = centre - scene.camera;
            const double dist2 = glm::dot(delta, delta);
            if (dist2 > maxDist * maxDist)
                continue;
            if (scene.frustum &&
                !scene.frustum->Visible(
                    static_cast<float>(d.origin.x) - 1.0f, static_cast<float>(d.origin.y) - 0.5f,
                    static_cast<float>(d.origin.z) - 1.0f, static_cast<float>(d.origin.x) + 2.0f,
                    static_cast<float>(d.origin.y) + 1.5f, static_cast<float>(d.origin.z) + 2.0f))
                continue;
            if (scene.sectionReached && !scene.sectionReached(static_cast<int>(std::floor(d.origin.x)) >> 4,
                                                              static_cast<int>(std::floor(d.origin.y)) >> 4,
                                                              static_cast<int>(std::floor(d.origin.z)) >> 4))
                continue;
            glm::vec3 light(1.0f);
            if (scene.packLit) {
                curLevels = scene.lightLevels ? scene.lightLevels(centre) : glm::ivec2(-1, -1);
            } else if (scene.light) {
                light = scene.light(centre);
            }
            const float ryRad = d.yaw * kDeg;
            const Placement at{glm::vec3(static_cast<float>(d.origin.x), static_cast<float>(d.origin.y),
                                         static_cast<float>(d.origin.z)),
                               std::cos(ryRad), std::sin(ryRad)};
            if (d.kind == BlockEntityDraw::Kind::Bed) {
                MeshBatch& batch = out.beds[std::clamp(d.color, 0, 15)];
                EmitBed(batch, d, at, light, scene.packLit);
                sync(batch);
                continue;
            }
            if (d.kind == BlockEntityDraw::Kind::Skull) {
                EmitSkull(out.skulls, d, at, light, scene.packLit, scene.worldTicks);
                for (auto& [texture, batch] : out.skulls)
                    sync(batch);
                continue;
            }
            if (d.front.empty() && d.back.empty())
                continue;
            if (dist2 > 32.0 * 32.0 || !scene.signTextAvailable || !scene.font)
                continue;
            EmitSignText(out.text, d, at, light, *scene.font);
            sync(out.text);
        }
        return out;
    }
}
