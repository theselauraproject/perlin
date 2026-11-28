#include "client/systems/sprites/SpecialItemModels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

// Java 26.3 special item models. Every cube below comes from the client's LayerDefinitions
// (ShieldModel, ConduitRenderer.createShellLayer, ChestModel, ShulkerModel, BannerModel/BannerFlagModel,
// SkullModel, DragonHeadModel, PiglinHeadModel, CopperGolemStatueModel) and every placement from the
// matching SpecialModelRenderer plus the item definition's "transformation". Only the texture paths are
// Bedrock's, since that is the resource pack the client draws with.
namespace perlin::ui::special {
    namespace {
        constexpr float kPi = 3.14159265358979323846f;

        glm::mat4 T(float x, float y, float z) {
            return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
        }
        glm::mat4 S(float x, float y, float z) {
            return glm::scale(glm::mat4(1.0f), glm::vec3(x, y, z));
        }

        // A ModelPart's pose: translate, then rotate Z, Y, X (PoseStack.translateAndRotate), then scale.
        struct Bone {
            int parent;
            glm::vec3 offset, rotation;  // pixels, radians
            float scale;
        };
        struct Box {
            int bone;
            float x, y, z, w, h, d, u, v, grow;
            bool mirror;
        };
        struct Def {
            const char* texture;
            float texW, texH;
            std::vector<Bone> bones;
            std::vector<Box> boxes;
            int flagBone = -1;  // banner flag: tinted with the base colour
        };

        glm::mat4 BoneMatrix(const Def& def, int bone) {
            if (bone < 0)
                return glm::mat4(1.0f);
            const Bone& b = def.bones[static_cast<size_t>(bone)];
            glm::mat4 m = BoneMatrix(def, b.parent);
            m = glm::translate(m, b.offset);
            m = glm::rotate(m, b.rotation.z, glm::vec3(0, 0, 1));
            m = glm::rotate(m, b.rotation.y, glm::vec3(0, 1, 0));
            m = glm::rotate(m, b.rotation.x, glm::vec3(1, 0, 0));
            return glm::scale(m, glm::vec3(b.scale));
        }

        Def Plain(const char* texture, float texW, float texH, bool hat) {
            Def d{texture, texW, texH, {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f}},
                  {{0, -4, -8, -4, 8, 8, 8, 0, 0, 0, false}}};
            if (hat)
                d.boxes.push_back({0, -4, -8, -4, 8, 8, 8, 32, 0, 0.25f, false});
            return d;
        }

        // SkullModel / DragonHeadModel / PiglinHeadModel, in the y-down space SkullBlockRenderer draws.
        bool HeadDef(const std::string& kind, Def& out) {
            if (kind == "skeleton")
                out = Plain("textures/entity/skeleton/skeleton", 64, 32, false);
            else if (kind == "wither_skeleton")
                out = Plain("textures/entity/skeleton/wither_skeleton", 64, 32, false);
            else if (kind == "zombie")
                out = Plain("textures/entity/zombie/zombie", 64, 64, true);
            else if (kind == "player")
                out = Plain("textures/entity/steve", 64, 64, true);
            else if (kind == "creeper")
                out = Plain("textures/entity/creeper/creeper", 64, 32, false);
            else if (kind == "dragon")
                out = Def{"textures/entity/dragon/dragon",
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
            else if (kind == "piglin")
                out = Def{"textures/entity/piglin/piglin",
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
            else
                return false;
            return true;
        }

        // ChestModel.createSingleBodyLayer, closed, in the y-down space the Bedrock chest textures follow.
        Def ChestDef(const char* texture) {
            return Def{texture,
                       64.0f,
                       64.0f,
                       {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f},
                        {0, glm::vec3(-7.0f, -9.0f, 7.0f), glm::vec3(0.0f), 1.0f}},
                       {{0, -7, -10, -7, 14, 10, 14, 0, 19, 0, false},
                        {1, 0, -5, -14, 14, 5, 14, 0, 0, 0, false},
                        {1, 6, -2, -15, 2, 4, 1, 0, 0, 0, false}}};
        }

        // BannerModel (standing pole and bar) + BannerFlagModel, at the item's animation time 0.
        Def BannerDef() {
            const float flagX = (-0.0125f + 0.01f) * kPi;
            return Def{"textures/entity/banner/banner_base",
                       64,
                       64,
                       {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 2.0f / 3.0f},
                        {0, glm::vec3(0.0f, -44.0f, 0.0f), glm::vec3(flagX, 0.0f, 0.0f), 1.0f}},
                       {{0, -1, -42, -1, 2, 42, 2, 44, 0, 0, false},
                        {0, -10, -44, -1, 20, 2, 2, 0, 42, 0, false},
                        {1, -10, 0, -2, 20, 40, 1, 0, 0, 0, false}},
                       1};
        }

        Def CopperGolemDef(const char* texture) {
            return Def{texture,
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
                        {5, 0, 0, -2, 4, 5, 4, 16, 27, 0, false}}};
        }

        // DyeColor.getTextureDiffuseColor.
        glm::vec3 DyeTint(const std::string& color) {
            static const std::array<std::pair<const char*, uint32_t>, 16> kDyes = {{
                {"white", 0xF9FFFE},   {"orange", 0xF9801D}, {"magenta", 0xC74EBD}, {"light_blue", 0x3AB3DA},
                {"yellow", 0xFED83D},  {"lime", 0x80C71F},   {"pink", 0xF38BAA},    {"gray", 0x474F52},
                {"light_gray", 0x9D9D97}, {"cyan", 0x169C9C}, {"purple", 0x8932B8}, {"blue", 0x3C44AA},
                {"brown", 0x835432},   {"green", 0x5E7C16},  {"red", 0xB02E26},     {"black", 0x1D1D21},
            }};
            for (const auto& [name, rgb] : kDyes)
                if (color == name)
                    return glm::vec3(static_cast<float>((rgb >> 16) & 0xFF), static_cast<float>((rgb >> 8) & 0xFF),
                                     static_cast<float>(rgb & 0xFF)) /
                           255.0f;
            return glm::vec3(1.0f);
        }

        // The block-entity defs are y-down models centred on the block's bottom face; the Java item
        // transformations for heads and banners (translate(0.5, 0, 0.5) * scale(1, -1, -1)) and the chest's
        // block-space model both come out as this placement.
        const glm::mat4 kFloorPlacement = T(0.5f, 0.0f, 0.5f) * S(1.0f / 16.0f, -1.0f / 16.0f, -1.0f / 16.0f);

        void EmitDef(const Def& def, const glm::mat4& placement, std::vector<Part>& out,
                     const glm::vec3& flagTint = glm::vec3(1.0f)) {
            Part body;
            body.texture = def.texture;
            Part flag;
            flag.texture = def.texture;
            flag.tint = flagTint;
            for (const Box& b : def.boxes) {
                Part& part = b.bone == def.flagBone && def.flagBone >= 0 ? flag : body;
                AddJavaCube(part.quads, placement * BoneMatrix(def, b.bone), {b.x, b.y, b.z}, {b.w, b.h, b.d},
                            {b.u, b.v}, {def.texW, def.texH}, b.grow, b.mirror);
            }
            if (!body.quads.empty())
                out.push_back(std::move(body));
            if (!flag.quads.empty())
                out.push_back(std::move(flag));
        }

        std::string ChestTexture(const std::string& arg) {
            if (arg == "trapped")
                return "textures/entity/chest/trapped";
            if (arg == "ender")
                return "textures/entity/chest/ender";
            if (arg == "christmas")
                return "textures/entity/chest/christmas";
            if (arg == "copper")
                return "textures/entity/chest/copper_default";
            if (arg.starts_with("copper_"))
                return "textures/entity/chest/" + arg;
            return "textures/entity/chest/normal";
        }

        std::string ShulkerTexture(const std::string& arg) {
            // Java "shulker" is the undyed box; Bedrock names it shulker_undyed.
            if (arg.empty() || arg == "shulker")
                return "textures/entity/shulker/shulker_undyed";
            return "textures/entity/shulker/" + arg;
        }

        std::string CopperGolemTexture(std::string arg) {
            if (arg.ends_with(".png"))
                arg.resize(arg.size() - 4);
            return arg.empty() ? "textures/entity/copper_golem/copper_golem" : arg;
        }
    }

    namespace {
        // block/template_bed_head and template_bed_foot (Java 26.3): faces as (direction, uv rect, tile of the
        // 64x48 bed sheet the client extracts from the jar: foot up/south/east/west, head up/north/east/west,
        // then the shared underside). Directions: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
        struct BedFace {
            int dir;
            float u0, v0, u1, v1;
            int tile;
        };
        struct BedBox {
            float from[3], to[3];
            std::vector<BedFace> faces;
        };
        const BedBox kBedFoot[] = {
            {{0, 3, 0}, {16, 9, 16}, {{0, 0, 0, 16, 16, 8}, {1, 0, 0, 16, 16, 0}, {3, 0, 7, 16, 13, 1},
                                      {4, 0, 7, 16, 13, 3}, {5, 0, 7, 16, 13, 2}}},
            {{0, 0, 13}, {3, 3, 16}, {{0, 7, 13, 10, 16, 3}, {2, 10, 13, 13, 16, 3}, {3, 0, 13, 3, 16, 1},
                                      {4, 13, 13, 16, 16, 3}, {5, 3, 13, 6, 16, 1}}},
            {{13, 0, 13}, {16, 3, 16}, {{0, 6, 13, 9, 16, 2}, {2, 3, 13, 6, 16, 2}, {3, 13, 13, 16, 16, 1},
                                        {4, 10, 13, 13, 16, 1}, {5, 0, 13, 3, 16, 2}}},
        };
        const BedBox kBedHead[] = {
            {{0, 3, 0}, {16, 9, 16}, {{0, 0, 0, 16, 16, 8}, {1, 0, 0, 16, 16, 4}, {2, 0, 7, 16, 13, 5},
                                      {4, 0, 7, 16, 13, 7}, {5, 0, 7, 16, 13, 6}}},
            {{0, 0, 0}, {3, 3, 3}, {{0, 6, 13, 9, 16, 7}, {2, 13, 13, 16, 16, 5}, {3, 3, 13, 6, 16, 7},
                                    {4, 0, 13, 3, 16, 7}, {5, 10, 13, 13, 16, 5}}},
            {{13, 0, 0}, {16, 3, 3}, {{0, 7, 13, 10, 16, 6}, {2, 0, 13, 3, 16, 5}, {3, 10, 13, 13, 16, 6},
                                      {4, 3, 13, 6, 16, 5}, {5, 13, 13, 16, 16, 6}}},
        };

        // The item is the composite of the head model and the foot model translated one block south.
        void EmitBedPiece(const BedBox* boxes, size_t count, float zOffset, std::vector<attachable::Quad>& out) {
            static const glm::vec3 kNormals[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
            for (size_t b = 0; b < count; b++) {
                const BedBox& box = boxes[b];
                const float x0 = box.from[0], y0 = box.from[1], z0 = box.from[2];
                const float x1 = box.to[0], y1 = box.to[1], z1 = box.to[2];
                const auto P = [&](float x, float y, float z) { return glm::vec3(x, y, z) / 16.0f + glm::vec3(0, 0, zOffset); };
                for (const BedFace& f : box.faces) {
                    glm::vec3 tl, tr, br, bl;
                    switch (f.dir) {
                    case 0: tl = P(x0, y0, z1); tr = P(x1, y0, z1); br = P(x1, y0, z0); bl = P(x0, y0, z0); break;
                    case 1: tl = P(x0, y1, z0); tr = P(x1, y1, z0); br = P(x1, y1, z1); bl = P(x0, y1, z1); break;
                    case 2: tl = P(x1, y1, z0); tr = P(x0, y1, z0); br = P(x0, y0, z0); bl = P(x1, y0, z0); break;
                    case 3: tl = P(x0, y1, z1); tr = P(x1, y1, z1); br = P(x1, y0, z1); bl = P(x0, y0, z1); break;
                    case 4: tl = P(x0, y1, z0); tr = P(x0, y1, z1); br = P(x0, y0, z1); bl = P(x0, y0, z0); break;
                    default: tl = P(x1, y1, z1); tr = P(x1, y1, z0); br = P(x1, y0, z0); bl = P(x1, y0, z1); break;
                    }
                    const float tu = static_cast<float>((f.tile % 4) * 16), tv = static_cast<float>((f.tile / 4) * 16);
                    const float u0 = (tu + f.u0) / 64.0f, v0 = (tv + f.v0) / 48.0f;
                    const float u1 = (tu + f.u1) / 64.0f, v1 = (tv + f.v1) / 48.0f;
                    attachable::Quad q;
                    q.p[0] = tl;
                    q.p[1] = tr;
                    q.p[2] = br;
                    q.p[3] = bl;
                    q.uv[0] = {u0, v0};
                    q.uv[1] = {u1, v0};
                    q.uv[2] = {u1, v1};
                    q.uv[3] = {u0, v1};
                    q.n = kNormals[f.dir];
                    out.push_back(q);
                }
            }
        }
    }

    void AddJavaCube(std::vector<attachable::Quad>& out, const glm::mat4& pose, glm::vec3 origin, glm::vec3 size,
                     glm::vec2 uv, glm::vec2 textureSize, float grow, bool mirror) {
        float x0 = origin.x - grow, y0 = origin.y - grow, z0 = origin.z - grow;
        float x1 = origin.x + size.x + grow, y1 = origin.y + size.y + grow, z1 = origin.z + size.z + grow;
        if (mirror)
            std::swap(x0, x1);
        const glm::vec3 v[8] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                                {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
        const float w = size.x, h = size.y, d = size.z;
        const float u0 = uv.x, u1 = uv.x + d, u2 = uv.x + d + w, u3 = uv.x + d + w + w, u4 = uv.x + d + w + d,
                    u5 = uv.x + d + w + d + w;
        const float v0 = uv.y, v1 = uv.y + d, v2 = uv.y + d + h;
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(pose)));
        // ModelPart.Polygon: vertex 0 takes (u2, v1), 1 (u1, v1), 2 (u1, v2), 3 (u2, v2) of its rectangle.
        const auto polygon = [&](int a, int b, int c, int e, float pu1, float pv1, float pu2, float pv2,
                                 glm::vec3 normal) {
            attachable::Quad q;
            const int idx[4] = {a, b, c, e};
            const glm::vec2 t[4] = {{pu2, pv1}, {pu1, pv1}, {pu1, pv2}, {pu2, pv2}};
            for (int i = 0; i < 4; i++) {
                q.p[i] = glm::vec3(pose * glm::vec4(v[idx[i]], 1.0f));
                q.uv[i] = t[i] / textureSize;
            }
            if (mirror) {
                std::swap(q.p[0], q.p[3]);
                std::swap(q.p[1], q.p[2]);
                std::swap(q.uv[0], q.uv[3]);
                std::swap(q.uv[1], q.uv[2]);
                normal.x = -normal.x;
            }
            const glm::vec3 n = normalMatrix * normal;
            const float len = glm::length(n);
            q.n = len > 1e-6f ? n / len : normal;
            out.push_back(q);
        };
        polygon(5, 4, 0, 1, u1, v0, u2, v1, {0, -1, 0});  // down
        polygon(2, 3, 7, 6, u2, v1, u3, v0, {0, 1, 0});   // up
        polygon(0, 4, 7, 3, u0, v1, u1, v2, {-1, 0, 0});  // west
        polygon(1, 0, 3, 2, u1, v1, u2, v2, {0, 0, -1});  // north
        polygon(5, 1, 2, 6, u2, v1, u4, v2, {1, 0, 0});   // east
        polygon(4, 5, 6, 7, u4, v1, u5, v2, {0, 0, 1});   // south
    }

    bool Build(const itemdisplay::Entry& entry, const std::string& itemName, std::vector<Part>& out) {
        out.clear();
        if (entry.kind != itemdisplay::Entry::Kind::Special)
            return false;
        const std::string& type = entry.special;
        const glm::mat4 local = entry.hasLocal ? entry.local : glm::mat4(1.0f);
        const glm::mat4 px = S(1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f);
        if (type == "shield") {
            // ShieldModel: plate and handle, drawn at the special renderer's pose.
            Part p;
            p.texture = "textures/entity/shield";
            AddJavaCube(p.quads, local * px, {-6, -11, -2}, {12, 22, 1}, {0, 0}, {64, 64});
            AddJavaCube(p.quads, local * px, {-1, -3, -1}, {2, 6, 6}, {26, 0}, {64, 64});
            out.push_back(std::move(p));
            return true;
        }
        if (type == "conduit") {
            // ConduitRenderer.createShellLayer: the closed shell.
            Part p;
            p.texture = "textures/blocks/conduit_base";
            AddJavaCube(p.quads, local * px, {-3, -3, -3}, {6, 6, 6}, {0, 0}, {32, 16});
            out.push_back(std::move(p));
            return true;
        }
        if (type == "shulker_box") {
            // ShulkerModel shell, closed: base and lid both hang from (0, 24, 0).
            Part p;
            p.texture = ShulkerTexture(entry.Arg("texture"));
            const glm::mat4 bone = local * px * T(0.0f, 24.0f, 0.0f);
            AddJavaCube(p.quads, bone, {-8, -16, -8}, {16, 12, 16}, {0, 0}, {64, 64});
            AddJavaCube(p.quads, bone, {-8, -8, -8}, {16, 8, 16}, {0, 28}, {64, 64});
            out.push_back(std::move(p));
            return true;
        }
        if (type == "chest") {
            EmitDef(ChestDef(ChestTexture(entry.Arg("texture")).c_str()), kFloorPlacement, out);
            return !out.empty();
        }
        if (type == "head" || type == "player_head") {
            Def def;
            if (!HeadDef(type == "player_head" ? std::string("player") : entry.Arg("kind"), def))
                return false;
            EmitDef(def, kFloorPlacement, out);
            return !out.empty();
        }
        if (type == "banner") {
            std::string color = entry.Arg("color");
            if (color.empty()) {
                const std::string bare = itemName.starts_with("minecraft:") ? itemName.substr(10) : itemName;
                color = bare.ends_with("_banner") ? bare.substr(0, bare.size() - 7) : "white";
            }
            EmitDef(BannerDef(), kFloorPlacement, out, DyeTint(color));
            return !out.empty();
        }
        if (type == "bed") {
            // The sheet ClientAssets extracts into the data folder uses Bedrock's dye names.
            std::string color = entry.Arg("color", "white");
            if (color == "light_gray")
                color = "silver";
            Part p;
            p.texture = "bed_" + color + ".png";
            EmitBedPiece(kBedHead, std::size(kBedHead), 0.0f, p.quads);
            EmitBedPiece(kBedFoot, std::size(kBedFoot), 1.0f, p.quads);
            if (entry.hasLocal)
                for (attachable::Quad& q : p.quads)
                    for (glm::vec3& v : q.p)
                        v = glm::vec3(entry.local * glm::vec4(v, 1.0f));
            out.push_back(std::move(p));
            return true;
        }
        if (type == "copper_golem_statue") {
            const std::string texture = CopperGolemTexture(entry.Arg("texture"));
            const Def def = CopperGolemDef(texture.c_str());
            EmitDef(def, kFloorPlacement, out);
            return !out.empty();
        }
        return false;
    }
}
