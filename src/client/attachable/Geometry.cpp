#include <algorithm>
#include "util/Env.hpp"
#include <cstdlib>
#include "client/attachable/Geometry.hpp"
#include "util/Strings.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <functional>

namespace perlin::attachable {
    namespace {
        glm::vec3 Vec3(const Json* v, glm::vec3 fallback = glm::vec3(0.0f)) {
            if (!v || !v->IsArray() || v->array.size() < 3)
                return fallback;
            return {static_cast<float>(v->array[0].NumberOr(0)), static_cast<float>(v->array[1].NumberOr(0)),
                    static_cast<float>(v->array[2].NumberOr(0))};
        }

        glm::vec2 Vec2(const Json* v) {
            if (!v || !v->IsArray() || v->array.size() < 2)
                return glm::vec2(0.0f);
            return {static_cast<float>(v->array[0].NumberOr(0)), static_cast<float>(v->array[1].NumberOr(0))};
        }

        // poly_mesh: indexed polygons ([position, normal, uv] per corner), or parallel arrays grouped by "tri_list" /
        // "quad_list". Bedrock counts mesh UVs from the bottom of the texture.
        void ParsePolyMesh(const Json& mesh, Bone& bone) {
            const auto list = [&](const char* key) -> const std::vector<Json>* {
                const Json* v = mesh.Find(key);
                return v && v->IsArray() ? &v->array : nullptr;
            };
            const std::vector<Json>* positions = list("positions");
            const std::vector<Json>* normals = list("normals");
            const std::vector<Json>* uvs = list("uvs");
            const Json* polys = mesh.Find("polys");
            if (!positions || !polys)
                return;
            bone.polyNormalized = mesh.Find("normalized_uvs") && mesh.Find("normalized_uvs")->BoolOr(false);
            const auto at3 = [](const std::vector<Json>* v, long i, glm::vec3 fallback) {
                return v && i >= 0 && static_cast<size_t>(i) < v->size()
                           ? Vec3(&(*v)[static_cast<size_t>(i)], fallback)
                           : fallback;
            };
            const auto at2 = [](const std::vector<Json>* v, long i) {
                return v && i >= 0 && static_cast<size_t>(i) < v->size() ? Vec2(&(*v)[static_cast<size_t>(i)])
                                                                         : glm::vec2(0.0f);
            };
            const auto emit = [&](const glm::vec3* p, const glm::vec3* n, const glm::vec2* uv, int count) {
                if (count < 3)
                    return;
                Quad q;
                for (int k = 0; k < 4; k++) {
                    const int c = std::min(k, count - 1);
                    q.p[k] = p[c];
                    q.uv[k] = uv[c];
                }
                glm::vec3 normal = n[0] + n[1] + n[2];
                if (glm::dot(normal, normal) < 1e-8f)
                    normal = glm::cross(p[1] - p[0], p[2] - p[0]);
                q.n = glm::dot(normal, normal) > 1e-12f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
                bone.polys.push_back(q);
            };
            if (polys->IsArray()) {
                for (const Json& poly : polys->array) {
                    if (!poly.IsArray())
                        continue;
                    glm::vec3 p[4], n[4];
                    glm::vec2 uv[4];
                    int count = 0;
                    for (const Json& corner : poly.array) {
                        if (count == 4 || !corner.IsArray() || corner.array.size() < 3)
                            break;
                        const long pi = static_cast<long>(corner.array[0].NumberOr(-1));
                        const long ni = static_cast<long>(corner.array[1].NumberOr(-1));
                        const long ui = static_cast<long>(corner.array[2].NumberOr(-1));
                        p[count] = at3(positions, pi, glm::vec3(0.0f));
                        n[count] = at3(normals, ni, glm::vec3(0.0f));
                        uv[count] = at2(uvs, ui);
                        count++;
                    }
                    emit(p, n, uv, count);
                }
            } else if (polys->IsString()) {
                const int group = polys->string == "quad_list" ? 4 : polys->string == "tri_list" ? 3 : 0;
                if (!group)
                    return;
                for (size_t i = 0; i + group <= positions->size(); i += group) {
                    glm::vec3 p[4], n[4];
                    glm::vec2 uv[4];
                    for (int k = 0; k < group; k++) {
                        p[k] = at3(positions, static_cast<long>(i + k), glm::vec3(0.0f));
                        n[k] = at3(normals, static_cast<long>(i + k), glm::vec3(0.0f));
                        uv[k] = at2(uvs, static_cast<long>(i + k));
                    }
                    emit(p, n, uv, group);
                }
            }
        }

        void ParseBones(const Json& bones, Geometry& g) {
            for (const Json& b : bones.array) {
                if (!b.IsObject())
                    continue;
                Bone bone;
                bone.name = str::Lower(b.Find("name") ? b.Find("name")->StringOr("") : "");
                bone.pivot = Vec3(b.Find("pivot"));
                bone.rotation = Vec3(b.Find("rotation"));
                if (const Json* binding = b.Find("binding"))
                    bone.binding = binding->StringOr("");
                if (const Json* parent = b.Find("parent"))
                    bone.parentName = str::Lower(parent->StringOr(""));
                if (const Json* n = b.Find("neverRender")) {
                    bone.neverRender = n->BoolOr(false);
                    bone.hasNeverRender = true;
                }
                if (const Json* r = b.Find("reset"))
                    bone.reset = r->BoolOr(false);
                const Json* bind = b.Find("bind_pose_rotation");
                if (const Json* inf = b.Find("inflate")) {
                    bone.inflate = static_cast<float>(inf->NumberOr(0));
                    bone.hasInflate = true;
                }
                if (const Json* cubes = b.Find("cubes"); cubes && cubes->IsArray()) {
                    for (const Json& c : cubes->array) {
                        Cube cube;
                        cube.origin = Vec3(c.Find("origin"));
                        cube.size = Vec3(c.Find("size"));
                        cube.pivot = Vec3(c.Find("pivot"));
                        cube.rotation = Vec3(c.Find("rotation"));
                        cube.hasRotation = c.Find("rotation") != nullptr;
                        if (cube.hasRotation && !c.Find("pivot"))
                            cube.pivot = cube.origin + cube.size * 0.5f;
                        if (bind && !cube.hasRotation) {
                            cube.pivot = bone.pivot;
                            cube.rotation = Vec3(bind);
                            cube.hasRotation = true;
                        }
                        cube.inflate = c.Find("inflate") ? static_cast<float>(c.Find("inflate")->NumberOr(0))
                                                         : bone.inflate;
                        cube.mirror = c.Find("mirror")
                                          ? c.Find("mirror")->BoolOr(false)
                                          : (b.Find("mirror") && b.Find("mirror")->BoolOr(false));
                        if (const Json* uv = c.Find("uv")) {
                            if (uv->IsArray()) {
                                cube.uv = Vec2(uv);
                            } else if (uv->IsObject()) {
                                cube.boxUv = false;
                                static const char* kFaces[6] = {"east",  "west", "north",
                                                                "south", "up",   "down"};
                                for (int i = 0; i < 6; i++)
                                    if (const Json* f = uv->Find(kFaces[i])) {
                                        cube.faces[i].present = true;
                                        cube.faces[i].uv = Vec2(f->Find("uv"));
                                        cube.faces[i].size = Vec2(f->Find("uv_size"));
                                        if (const Json* r = f->Find("uv_rotation"))
                                            cube.faces[i].rotation =
                                                ((static_cast<int>(std::lround(r->NumberOr(0) / 90.0)) % 4) +
                                                 4) %
                                                4;
                                    }
                            }
                        }
                        bone.cubes.push_back(cube);
                    }
                }
                if (const Json* mesh = b.Find("poly_mesh"); mesh && mesh->IsObject())
                    ParsePolyMesh(*mesh, bone);
                if (const Json* meshes = b.Find("texture_meshes"); meshes && meshes->IsArray()) {
                    for (const Json& m : meshes->array) {
                        TextureMesh mesh;
                        mesh.texture = m.Find("texture") ? m.Find("texture")->StringOr("default") : "default";
                        mesh.position = Vec3(m.Find("position"));
                        mesh.rotation = Vec3(m.Find("rotation"));
                        mesh.scale = Vec3(m.Find("scale"), glm::vec3(1.0f));
                        mesh.localPivot = Vec3(m.Find("local_pivot"));
                        bone.meshes.push_back(std::move(mesh));
                    }
                }
                g.bones.push_back(std::move(bone));
            }
        }
    }

    glm::mat4 EulerZYX(const glm::vec3& d) {
        glm::mat4 m(1.0f);
        static const float zs =
            PERLIN_ENV("PERLIN_ZSIGN") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_ZSIGN"))) : -1.0f;
        static const bool alt = PERLIN_ENV("PERLIN_EULER_ORDER") != nullptr;
        if (alt) {
            m = glm::rotate(m, glm::radians(-d.x), glm::vec3(1, 0, 0));
            m = glm::rotate(m, glm::radians(d.y), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(d.z * zs), glm::vec3(0, 0, 1));
            return m;
        }
        m = glm::rotate(m, glm::radians(d.z * zs), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(d.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(-d.x), glm::vec3(1, 0, 0));
        return m;
    }

    void GeometryLibrary::Load(const std::string& text) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error) || !doc.IsObject())
            return;
        if (const Json* list = doc.Find("minecraft:geometry"); list && list->IsArray()) {
            for (const Json& entry : list->array) {
                Geometry g;
                const Json* desc = entry.Find("description");
                if (!desc || !desc->Find("identifier"))
                    continue;
                g.id = desc->Find("identifier")->StringOr("");
                if (const size_t colon = g.id.find(':'); colon != std::string::npos) {
                    g.parentId = g.id.substr(colon + 1);
                    g.id.resize(colon);
                }
                g.textureWidth = desc->Find("texture_width")
                                     ? static_cast<float>(desc->Find("texture_width")->NumberOr(64))
                                     : 64.0f;
                g.textureHeight = desc->Find("texture_height")
                                      ? static_cast<float>(desc->Find("texture_height")->NumberOr(64))
                                      : 64.0f;
                if (const Json* bones = entry.Find("bones"); bones && bones->IsArray())
                    ParseBones(*bones, g);
                geometries[g.id] = std::move(g);
            }
        }
        for (const auto& [key, value] : doc.object) {
            if (!key.starts_with("geometry.") || !value.IsObject())
                continue;
            Geometry g;
            g.id = key.substr(0, key.find(':'));
            if (const size_t colon = key.find(':'); colon != std::string::npos)
                g.parentId = key.substr(colon + 1);
            g.textureWidth = value.Find("texturewidth")
                                 ? static_cast<float>(value.Find("texturewidth")->NumberOr(64))
                                 : 64.0f;
            g.textureHeight = value.Find("textureheight")
                                  ? static_cast<float>(value.Find("textureheight")->NumberOr(64))
                                  : 64.0f;
            if (!g.parentId.empty()) {
                if (!value.Find("texturewidth"))
                    g.textureWidth = 0;
                if (!value.Find("textureheight"))
                    g.textureHeight = 0;
            }
            if (const Json* bones = value.Find("bones"); bones && bones->IsArray())
                ParseBones(*bones, g);
            geometries[g.id] = std::move(g);
        }
        Link();
    }

    namespace {
        void LinkBones(Geometry& g) {
            std::unordered_map<std::string, size_t> byName;
            for (size_t i = 0; i < g.bones.size(); i++)
                byName[g.bones[i].name] = i;
            std::vector<Bone> ordered;
            std::vector<int> state(g.bones.size(), 0);
            std::unordered_map<size_t, int> newIndex;
            const std::function<void(size_t)> visit = [&](size_t i) {
                if (state[i] == 2)
                    return;
                if (state[i] == 1)
                    return;
                state[i] = 1;
                const auto p = byName.find(g.bones[i].parentName);
                if (!g.bones[i].parentName.empty() && p != byName.end() && p->second != i)
                    visit(p->second);
                newIndex[i] = static_cast<int>(ordered.size());
                ordered.push_back(g.bones[i]);
                state[i] = 2;
            };
            for (size_t i = 0; i < g.bones.size(); i++)
                visit(i);
            std::unordered_map<std::string, int> finalIndex;
            for (size_t i = 0; i < ordered.size(); i++)
                finalIndex[ordered[i].name] = static_cast<int>(i);
            for (size_t i = 0; i < ordered.size(); i++) {
                const auto p = finalIndex.find(ordered[i].parentName);
                ordered[i].parent = (ordered[i].parentName.empty() || p == finalIndex.end() ||
                                     p->second >= static_cast<int>(i))
                                        ? -1
                                        : p->second;
            }
            g.bones = std::move(ordered);
            g.linked = true;
        }
    }

    void GeometryLibrary::Link() {
        for (int pass = 0; pass < 8; pass++) {
            bool changed = false;
            for (auto& [id, g] : geometries) {
                if (g.linked)
                    continue;
                if (!g.parentId.empty()) {
                    const auto p = geometries.find(g.parentId);
                    if (p == geometries.end() || !p->second.linked)
                        continue;
                    std::vector<Bone> merged = p->second.bones;
                    for (const Bone& b : g.bones) {
                        bool replaced = false;
                        for (Bone& m : merged)
                            if (m.name == b.name) {
                                if (b.cubes.empty() && (b.reset || b.hasInflate || b.hasNeverRender) &&
                                    b.meshes.empty()) {
                                    if (b.reset)
                                        m.cubes.clear();
                                    if (b.hasInflate)
                                        for (Cube& c : m.cubes)
                                            c.inflate = b.inflate;
                                    if (b.hasNeverRender)
                                        m.neverRender = b.neverRender;
                                    replaced = true;
                                    continue;
                                }
                                const bool overlay =
                                    !b.cubes.empty() && !m.cubes.empty() &&
                                    std::all_of(b.cubes.begin(), b.cubes.end(),
                                                [](const Cube& c) { return c.inflate > 0.0f; });
                                if (overlay)
                                    m.cubes.insert(m.cubes.end(), b.cubes.begin(), b.cubes.end());
                                else
                                    m = b;
                                replaced = true;
                            }
                        if (!replaced)
                            merged.push_back(b);
                    }
                    g.bones = std::move(merged);
                    if (g.textureWidth <= 0)
                        g.textureWidth = p->second.textureWidth;
                    if (g.textureHeight <= 0)
                        g.textureHeight = p->second.textureHeight;
                }
                LinkBones(g);
                changed = true;
            }
            if (!changed)
                break;
        }
    }

    const Geometry* GeometryLibrary::Find(const std::string& id) const {
        const auto it = geometries.find(id);
        return it == geometries.end() || !it->second.linked ? nullptr : &it->second;
    }

    namespace {
        void AddCube(const Cube& c, const Geometry& g, const glm::mat4& m, std::vector<Quad>& out,
                     bool mirrorTextures) {
            const float inf = c.inflate;
            const glm::vec3 lo = c.origin - glm::vec3(inf), hi = c.origin + c.size + glm::vec3(inf);
            const float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
            glm::mat4 cubeM = m;
            if (c.hasRotation)
                cubeM = m * glm::translate(glm::mat4(1.0f), c.pivot) * EulerZYX(c.rotation) *
                        glm::translate(glm::mat4(1.0f), -c.pivot);

            struct FaceDef {
                glm::vec3 tl, tr, br, bl;
                glm::vec3 n;
            };
            const FaceDef faces[6] = {
                {{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}, {1, 0, 0}},
                {{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}, {-1, 0, 0}},
                {{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}, {0, 0, -1}},
                {{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}, {0, 0, 1}},
                {{x1, y1, z1}, {x0, y1, z1}, {x0, y1, z0}, {x1, y1, z0}, {0, 1, 0}},
                {{x1, y0, z0}, {x0, y0, z0}, {x0, y0, z1}, {x1, y0, z1}, {0, -1, 0}},
            };
            const float w = c.size.x, h = c.size.y, d = c.size.z;
            glm::vec2 uvOrigin[6], uvSize[6];
            if (c.boxUv) {
                // Box UV lays the faces out with whole-pixel sizes.
                const float bw = std::floor(w), bh = std::floor(h), bd = std::floor(d);
                const float u = c.uv.x, v = c.uv.y;
                uvOrigin[0] = {u, v + bd};
                uvSize[0] = {bd, bh};
                uvOrigin[1] = {u + bd + bw, v + bd};
                uvSize[1] = {bd, bh};
                uvOrigin[2] = {u + bd, v + bd};
                uvSize[2] = {bw, bh};
                uvOrigin[3] = {u + bd + bw + bd, v + bd};
                uvSize[3] = {bw, bh};
                uvOrigin[4] = {u + bd, v};
                uvSize[4] = {bw, bd};
                uvOrigin[5] = {u + bd + bw, v};
                uvSize[5] = {bw, bd};
            } else {
                for (int i = 0; i < 6; i++) {
                    uvOrigin[i] = c.faces[i].uv;
                    uvSize[i] = c.faces[i].size;
                    if (uvSize[i].x == 0.0f && uvSize[i].y == 0.0f)
                        uvSize[i] = i < 2 ? glm::vec2(d, h) : i < 4 ? glm::vec2(w, h) : glm::vec2(w, d);
                }
            }
            for (int i = 0; i < 6; i++) {
                if (!c.boxUv && !c.faces[i].present)
                    continue;
                int fi = i;
                const bool mirrored = c.mirror != mirrorTextures;
                bool flip = mirrored;
                if (mirrored && i < 2)
                    fi = 1 - i;
                const FaceDef& f = faces[i];
                const glm::vec2 o = uvOrigin[fi], s = uvSize[fi];
                glm::vec2 a = o, b = o + glm::vec2(s.x, 0), cc = o + s, dd = o + glm::vec2(0, s.y);
                if (flip) {
                    std::swap(a, b);
                    std::swap(dd, cc);
                }
                // uv_rotation turns the texture clockwise on the face: each corner takes the UV of the one before it.
                if (const int turns = c.boxUv ? 0 : c.faces[fi].rotation) {
                    const glm::vec2 ring[4] = {a, b, cc, dd};
                    a = ring[(4 - turns) % 4];
                    b = ring[(5 - turns) % 4];
                    cc = ring[(6 - turns) % 4];
                    dd = ring[(7 - turns) % 4];
                }
                Quad q;
                const glm::vec3* corners[4] = {&f.tl, &f.tr, &f.br, &f.bl};
                const glm::vec2* uvs[4] = {&a, &b, &cc, &dd};
                for (int k = 0; k < 4; k++) {
                    q.p[k] = glm::vec3(cubeM * glm::vec4(*corners[k], 1.0f));
                    q.uv[k] = {uvs[k]->x / g.textureWidth, uvs[k]->y / g.textureHeight};
                }
                q.n = glm::normalize(glm::mat3(cubeM) * f.n);
                out.push_back(q);
            }
        }

        void AddTextureMesh(const TextureMesh& tm, const Texture& tex, const glm::mat4& boneM,
                            std::vector<Quad>& out) {
            if (tex.width <= 0 || tex.height <= 0)
                return;
            const int w = tex.width, h = tex.height;
            const float sx = 16.0f / static_cast<float>(w), sz = 16.0f / static_cast<float>(h);
            glm::mat4 m = boneM * glm::translate(glm::mat4(1.0f), tm.position) * EulerZYX(tm.rotation) *
                          glm::scale(glm::mat4(1.0f), tm.scale) *
                          glm::translate(glm::mat4(1.0f), -tm.localPivot);
            const auto P = [&](float px, float pz, float y) {
                return glm::vec3(m * glm::vec4(px * sx, y, pz * sz, 1.0f));
            };
            const auto alpha = [&](int x, int y) {
                return x < 0 || y < 0 || x >= w || y >= h
                           ? 0
                           : tex.pixels[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4 + 3];
            };
            const glm::vec3 up = glm::normalize(glm::mat3(m) * glm::vec3(0, 1, 0));
            Quad top;
            top.p[0] = P(0, 0, 1);
            top.p[1] = P(w, 0, 1);
            top.p[2] = P(w, h, 1);
            top.p[3] = P(0, h, 1);
            top.uv[0] = {0, 0};
            top.uv[1] = {1, 0};
            top.uv[2] = {1, 1};
            top.uv[3] = {0, 1};
            top.n = up;
            out.push_back(top);
            Quad bottom;
            bottom.p[0] = P(w, 0, 0);
            bottom.p[1] = P(0, 0, 0);
            bottom.p[2] = P(0, h, 0);
            bottom.p[3] = P(w, h, 0);
            bottom.uv[0] = {1, 0};
            bottom.uv[1] = {0, 0};
            bottom.uv[2] = {0, 1};
            bottom.uv[3] = {1, 1};
            bottom.n = -up;
            out.push_back(bottom);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    if (alpha(x, y) == 0)
                        continue;
                    const glm::vec2 c((static_cast<float>(x) + 0.5f) / static_cast<float>(w),
                                      (static_cast<float>(y) + 0.5f) / static_cast<float>(h));
                    const float fx = static_cast<float>(x), fz = static_cast<float>(y);
                    const auto side = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc, glm::vec3 d, glm::vec3 n) {
                        Quad q;
                        q.p[0] = a;
                        q.p[1] = b;
                        q.p[2] = cc;
                        q.p[3] = d;
                        for (auto& t : q.uv)
                            t = c;
                        q.n = glm::normalize(glm::mat3(m) * n);
                        out.push_back(q);
                    };
                    if (alpha(x - 1, y) == 0)
                        side(P(fx, fz, 0), P(fx, fz, 1), P(fx, fz + 1, 1), P(fx, fz + 1, 0), {-1, 0, 0});
                    if (alpha(x + 1, y) == 0)
                        side(P(fx + 1, fz, 1), P(fx + 1, fz, 0), P(fx + 1, fz + 1, 0), P(fx + 1, fz + 1, 1),
                             {1, 0, 0});
                    if (alpha(x, y - 1) == 0)
                        side(P(fx, fz, 1), P(fx + 1, fz, 1), P(fx + 1, fz, 0), P(fx, fz, 0), {0, 0, -1});
                    if (alpha(x, y + 1) == 0)
                        side(P(fx, fz + 1, 0), P(fx + 1, fz + 1, 0), P(fx + 1, fz + 1, 1), P(fx, fz + 1, 1),
                             {0, 0, 1});
                }
        }
    }

    Geometry VoxelizeSkinLayers(const Geometry& source, const uint8_t* rgba, int width, int height) {
        Geometry out = source;
        if (!rgba || width <= 0 || height <= 0)
            return out;
        static const char* const kLayers[] = {"hat",         "jacket",    "leftsleeve",
                                              "rightsleeve", "leftpants", "rightpants"};
        const float sx = static_cast<float>(width) / source.textureWidth,
                    sy = static_cast<float>(height) / source.textureHeight;
        const auto alphaAt = [&](int x, int y) -> int {
            if (x < 0 || y < 0 || x >= width || y >= height)
                return 0;
            return rgba[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4 +
                        3];
        };
        for (Bone& bone : out.bones) {
            bool layer = false;
            for (const char* name : kLayers)
                layer = layer || bone.name == name;
            if (!layer || bone.cubes.empty())
                continue;
            std::vector<Cube> kept;
            for (const Cube& c : bone.cubes) {
                if (!c.boxUv || c.hasRotation || c.size.x < 1 || c.size.y < 1 || c.size.z < 1) {
                    kept.push_back(c);
                    continue;
                }
                const float w = c.size.x, h = c.size.y, d = c.size.z;
                const float inflate = c.inflate > 0.0f ? c.inflate : bone.inflate;
                const float thickness = std::max(0.5f, inflate * 2.0f);
                const float x0 = c.origin.x, y0 = c.origin.y, z0 = c.origin.z, x1 = x0 + w, y1 = y0 + h,
                            z1 = z0 + d;
                struct FaceDef {
                    glm::vec3 tl, tr, br, bl, n;
                };
                const FaceDef faces[6] = {
                    {{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}, {1, 0, 0}},
                    {{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}, {-1, 0, 0}},
                    {{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}, {0, 0, -1}},
                    {{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}, {0, 0, 1}},
                    {{x1, y1, z1}, {x0, y1, z1}, {x0, y1, z0}, {x1, y1, z0}, {0, 1, 0}},
                    {{x1, y0, z0}, {x0, y0, z0}, {x0, y0, z1}, {x1, y0, z1}, {0, -1, 0}},
                };
                const float u = c.uv.x, v = c.uv.y;
                const glm::vec2 uvOrigin[6] = {{u, v + d},     {u + d + w, v + d},
                                               {u + d, v + d}, {u + d + w + d, v + d},
                                               {u + d, v},     {u + d + w, v}};
                const glm::vec2 uvSize[6] = {{d, h}, {d, h}, {w, h}, {w, h}, {w, d}, {w, d}};
                const bool mirrored = !c.mirror;
                for (int i = 0; i < 6; i++) {
                    const int fi = mirrored && i < 2 ? 1 - i : i;
                    const FaceDef& f = faces[i];
                    const int sw = static_cast<int>(uvSize[fi].x), sh = static_cast<int>(uvSize[fi].y);
                    const glm::vec3 ex = (f.tr - f.tl) / static_cast<float>(sw),
                                    ey = (f.bl - f.tl) / static_cast<float>(sh);
                    const glm::vec3 up = f.n * thickness;
                    const auto texel = [&](int pi, int pj) {
                        const int px = static_cast<int>(uvOrigin[fi].x * sx) +
                                       (mirrored ? sw - 1 - pi : pi) * static_cast<int>(sx);
                        const int py = static_cast<int>(uvOrigin[fi].y * sy) + pj * static_cast<int>(sy);
                        return glm::ivec2(px, py);
                    };
                    const auto opaque = [&](int pi, int pj) {
                        if (pi < 0 || pj < 0 || pi >= sw || pj >= sh)
                            return false;
                        const glm::ivec2 t = texel(pi, pj);
                        return alphaAt(t.x, t.y) > 0;
                    };
                    for (int pj = 0; pj < sh; pj++)
                        for (int pi = 0; pi < sw; pi++) {
                            if (!opaque(pi, pj))
                                continue;
                            const glm::ivec2 t = texel(pi, pj);
                            const glm::vec2 centre(
                                (static_cast<float>(t.x) + 0.5f) / static_cast<float>(width),
                                (static_cast<float>(t.y) + 0.5f) / static_cast<float>(height));
                            const glm::vec3 a =
                                f.tl + ex * static_cast<float>(pi) + ey * static_cast<float>(pj);
                            const glm::vec3 b = a + ex, cc = a + ex + ey, dd = a + ey;
                            const auto push = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                                                  glm::vec3 n) {
                                if (glm::dot(glm::cross(p1 - p0, p2 - p0), n) > 0.0f)
                                    std::swap(p1, p3);
                                Quad q;
                                q.p[0] = p0;
                                q.p[1] = p1;
                                q.p[2] = p2;
                                q.p[3] = p3;
                                for (glm::vec2& uv : q.uv)
                                    uv = centre;
                                q.n = n;
                                bone.voxels.push_back(q);
                            };
                            push(a + up, b + up, cc + up, dd + up, f.n);
                            const glm::vec3 nx = glm::normalize(ex), ny = glm::normalize(ey);
                            if (!opaque(pi - 1, pj))
                                push(a, a + up, dd + up, dd, -nx);
                            if (!opaque(pi + 1, pj))
                                push(b + up, b, cc, cc + up, nx);
                            if (!opaque(pi, pj - 1))
                                push(a + up, a, b, b + up, -ny);
                            if (!opaque(pi, pj + 1))
                                push(dd, dd + up, cc + up, cc, ny);
                        }
                }
            }
            bone.cubes = std::move(kept);
        }
        return out;
    }

    std::vector<Quad> ExtrudeSprite(const Texture& atlas, int tx, int ty, int tw, int th, float width,
                                    float height, float depth) {
        std::vector<Quad> out;
        if (tw <= 0 || th <= 0 || tx < 0 || ty < 0 || tx + tw > atlas.width || ty + th > atlas.height ||
            atlas.pixels.size() < static_cast<size_t>(atlas.width) * atlas.height * 4 || depth <= 0)
            return out;
        const float z = depth * 0.5f, x0 = -width * 0.5f, bottom = 0.4f;
        const auto filled = [&](int x, int y) {
            return x >= 0 && x < tw && y >= 0 && y < th &&
                   atlas.pixels[(static_cast<size_t>(ty + y) * atlas.width + tx + x) * 4 + 3] >= 16;
        };
        const auto emit = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 normal,
                              glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3) {
            Quad q;
            q.p[0] = a;
            q.p[1] = b;
            q.p[2] = c;
            q.p[3] = d;
            q.uv[0] = uv0;
            q.uv[1] = uv1;
            q.uv[2] = uv2;
            q.uv[3] = uv3;
            q.n = normal;
            out.push_back(q);
        };
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                if (!filled(x, y))
                    continue;
                const float l = x0 + width * x / tw, r = x0 + width * (x + 1) / tw;
                const float t = bottom + height * (th - y) / th, b = bottom + height * (th - y - 1) / th;
                const glm::vec2 uv((tx + x + 0.5f) / atlas.width, (ty + y + 0.5f) / atlas.height);
                const auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
                    emit(a, b, c, d, n, uv, uv, uv, uv);
                };
                face({l, b, z}, {r, b, z}, {r, t, z}, {l, t, z}, {0, 0, 1});
                face({r, b, -z}, {l, b, -z}, {l, t, -z}, {r, t, -z}, {0, 0, -1});
                if (!filled(x - 1, y))
                    face({l, b, -z}, {l, b, z}, {l, t, z}, {l, t, -z}, {-1, 0, 0});
                if (!filled(x + 1, y))
                    face({r, b, z}, {r, b, -z}, {r, t, -z}, {r, t, z}, {1, 0, 0});
                if (!filled(x, y - 1))
                    face({l, t, z}, {r, t, z}, {r, t, -z}, {l, t, -z}, {0, 1, 0});
                if (!filled(x, y + 1))
                    face({l, b, -z}, {r, b, -z}, {r, b, z}, {l, b, z}, {0, -1, 0});
            }
        return out;
    }

    void GeometryLibrary::Detach(const std::string& id, const std::vector<std::string>& bones) {
        const auto it = geometries.find(id);
        if (it == geometries.end())
            return;
        for (Bone& b : it->second.bones)
            if (std::find(bones.begin(), bones.end(), b.name) != bones.end()) {
                b.parent = -1;
                b.parentName.clear();
            }
    }

    void ComputeBoneWorlds(const Geometry& g, const Pose& pose, BoneWorlds& out) {
        std::vector<glm::mat4> world(g.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < g.bones.size(); i++) {
            const Bone& b = g.bones[i];
            BoneXform x;
            if (const auto it = pose.find(b.name); it != pose.end())
                x = it->second;
            const glm::mat4 local = glm::translate(glm::mat4(1.0f), x.position) *
                                    glm::translate(glm::mat4(1.0f), b.pivot) *
                                    EulerZYX(b.rotation + x.rotation) * glm::scale(glm::mat4(1.0f), x.scale) *
                                    glm::translate(glm::mat4(1.0f), -b.pivot);
            world[i] = (b.parent >= 0 ? world[static_cast<size_t>(b.parent)] : glm::mat4(1.0f)) * local;
            out[b.name] = world[i];
        }
    }

    void BuildMesh(const Geometry& g, const Pose& pose, const TextureLookup& textures, BuiltMesh& out,
                   const BoneFilter& visible, const glm::mat4* rootParent, bool mirrorTextures,
                   const BoneWorlds* attach) {
        std::vector<glm::mat4> world(g.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < g.bones.size(); i++) {
            const Bone& b = g.bones[i];
            const auto worn = attach ? attach->find(b.name) : BoneWorlds::const_iterator();
            if (attach && worn != attach->end()) {
                world[i] = worn->second;
            } else {
                BoneXform x;
                if (const auto it = pose.find(b.name); it != pose.end())
                    x = it->second;
                const glm::mat4 local =
                    glm::translate(glm::mat4(1.0f), x.position) * glm::translate(glm::mat4(1.0f), b.pivot) *
                    EulerZYX(b.rotation + x.rotation) * glm::scale(glm::mat4(1.0f), x.scale) *
                    glm::translate(glm::mat4(1.0f), -b.pivot);
                world[i] = (b.parent >= 0 ? world[static_cast<size_t>(b.parent)]
                                          : (rootParent ? *rootParent : glm::mat4(1.0f))) *
                           local;
            }
            if (b.neverRender || (visible && !visible(b.name)))
                continue;
            for (const Cube& c : b.cubes)
                AddCube(c, g, world[i], out.body, mirrorTextures);
            for (const Quad& pq : b.polys) {
                Quad q = pq;
                for (int k = 0; k < 4; k++) {
                    q.p[k] = glm::vec3(world[i] * glm::vec4(q.p[k], 1.0f));
                    const glm::vec2 uv =
                        b.polyNormalized ? q.uv[k] : q.uv[k] / glm::vec2(g.textureWidth, g.textureHeight);
                    q.uv[k] = {uv.x, 1.0f - uv.y};
                }
                q.n = glm::normalize(glm::mat3(world[i]) * q.n);
                out.body.push_back(q);
            }
            for (const Quad& vq : b.voxels) {
                Quad q = vq;
                for (glm::vec3& pt : q.p)
                    pt = glm::vec3(world[i] * glm::vec4(pt, 1.0f));
                q.n = glm::normalize(glm::mat3(world[i]) * q.n);
                out.body.push_back(q);
            }
            for (const TextureMesh& tm : b.meshes) {
                const Texture* tex = textures ? textures(tm.texture) : nullptr;
                if (!tex)
                    continue;
                BuiltMesh::Extra extra;
                extra.texture = tm.texture;
                AddTextureMesh(tm, *tex, world[i], extra.quads);
                out.extras.push_back(std::move(extra));
            }
        }
    }

    bool BoneWorld(const Geometry& g, const Pose& pose, const std::string& name, glm::mat4& out,
                   glm::vec3& pivot) {
        std::vector<glm::mat4> world(g.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < g.bones.size(); i++) {
            const Bone& b = g.bones[i];
            BoneXform x;
            if (const auto it = pose.find(b.name); it != pose.end())
                x = it->second;
            const glm::mat4 local = glm::translate(glm::mat4(1.0f), x.position) *
                                    glm::translate(glm::mat4(1.0f), b.pivot) *
                                    EulerZYX(b.rotation + x.rotation) * glm::scale(glm::mat4(1.0f), x.scale) *
                                    glm::translate(glm::mat4(1.0f), -b.pivot);
            world[i] = (b.parent >= 0 ? world[static_cast<size_t>(b.parent)] : glm::mat4(1.0f)) * local;
            if (b.name == name) {
                out = world[i];
                pivot = b.pivot;
                return true;
            }
        }
        return false;
    }
}
