#include <unordered_set>
#include <mutex>
#include <iostream>
#include "client/renderer/block/BlockGeometry.hpp"

#include "util/Json.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace perlin {
    namespace {
        using Mat3 = std::array<std::array<float, 3>, 3>;

        std::string ReadAll(const fs::path& p) {
            std::ifstream f(p, std::ios::binary);
            if (!f)
                return {};
            return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }

        Mat3 Identity() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }
        Mat3 Mul(const Mat3& a, const Mat3& b) {
            Mat3 r{};
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    for (int k = 0; k < 3; k++)
                        r[i][j] += a[i][k] * b[k][j];
            return r;
        }
        Mat3 Rx(float a) {
            const float c = std::cos(a), s = std::sin(a);
            return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
        }
        Mat3 Ry(float a) {
            const float c = std::cos(a), s = std::sin(a);
            return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
        }
        Mat3 Rz(float a) {
            const float c = std::cos(a), s = std::sin(a);
            return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
        }
        Mat3 BedrockRotation(const float r[3]) {
            constexpr float kRad = 3.14159265358979f / 180.0f;
            return Mul(Rz(r[2] * kRad), Mul(Ry(-r[1] * kRad), Rx(-r[0] * kRad)));
        }
        void Apply(const Mat3& m, const float pivot[3], float v[3]) {
            const float d[3] = {v[0] - pivot[0], v[1] - pivot[1], v[2] - pivot[2]};
            for (int i = 0; i < 3; i++)
                v[i] = pivot[i] + m[i][0] * d[0] + m[i][1] * d[1] + m[i][2] * d[2];
        }

        constexpr const char* kFaceNames[6] = {"down", "up", "north", "south", "west", "east"};
        constexpr int kFaceVerts[6][4][3] = {
            {{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}, {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
            {{1, 1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}}, {{0, 1, 1}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}},
            {{0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}}, {{1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}}};
        constexpr float kDirVec[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

        int NearestDir(const float n[3]) {
            int best = 1;
            float bd = -1e9f;
            for (int d = 0; d < 6; d++) {
                const float dot = n[0] * kDirVec[d][0] + n[1] * kDirVec[d][1] + n[2] * kDirVec[d][2];
                if (dot > bd + 1e-6f) {
                    bd = dot;
                    best = d;
                }
            }
            return best;
        }

        void ReadVec(const json::View v, float* out, int n) {
            for (int i = 0; i < n; i++)
                if (v[(size_t)i].isNumber())
                    out[i] = v[(size_t)i].real();
        }

        using States = std::vector<std::pair<std::string, BlockPalette::StateValue>>;

        bool EvalAtom(std::string atom, const States& states) {
            auto trim = [](std::string s) {
                while (!s.empty() && (s.front() == ' ' || s.front() == '(' || s.front() == '\t'))
                    s.erase(s.begin());
                while (!s.empty() && (s.back() == ' ' || s.back() == ')' || s.back() == '\t'))
                    s.pop_back();
                return s;
            };
            atom = trim(atom);
            if (atom == "true" || atom == "1")
                return true;
            if (atom == "false" || atom == "0")
                return false;
            size_t fn = atom.find("block_property(");
            size_t skip = 15;
            if (fn == std::string::npos) {
                fn = atom.find("block_state(");
                skip = 12;
            }
            if (fn == std::string::npos)
                return false;
            const size_t q0 = atom.find_first_of("'\"", fn + skip);
            if (q0 == std::string::npos)
                return false;
            const size_t q1 = atom.find(atom[q0], q0 + 1);
            if (q1 == std::string::npos)
                return false;
            const std::string name = atom.substr(q0 + 1, q1 - q0 - 1);
            const size_t close = atom.find(')', q1);
            std::string rest = close == std::string::npos ? "" : trim(atom.substr(close + 1));

            const BlockPalette::StateValue* sv = nullptr;
            for (const auto& [k, v] : states)
                if (k == name || k == "minecraft:" + name || "minecraft:" + k == name)
                    sv = &v;
            if (!sv)
                return false;
            if (rest.empty())
                return sv->kind == BlockPalette::StateValue::String ? !sv->s.empty() : sv->i != 0;
            std::string op;
            for (const char* o : {"==", "!=", ">=", "<=", ">", "<"})
                if (rest.rfind(o, 0) == 0) {
                    op = o;
                    break;
                }
            if (op.empty())
                return false;
            std::string rhs = trim(rest.substr(op.size()));
            int cmp;
            if (!rhs.empty() && (rhs[0] == '\'' || rhs[0] == '"')) {
                rhs = rhs.substr(1, rhs.size() >= 2 ? rhs.size() - 2 : 0);
                const std::string have = sv->kind == BlockPalette::StateValue::String ? sv->s : std::to_string(sv->i);
                cmp = have.compare(rhs);
            } else {
                double want = rhs == "true" ? 1 : (rhs == "false" ? 0 : std::atof(rhs.c_str()));
                const double have =
                    sv->kind == BlockPalette::StateValue::String ? (sv->s.empty() ? 0 : 1) : sv->i;
                cmp = have < want ? -1 : (have > want ? 1 : 0);
            }
            if (op == "==")
                return cmp == 0;
            if (op == "!=")
                return cmp != 0;
            if (op == ">=")
                return cmp >= 0;
            if (op == "<=")
                return cmp <= 0;
            if (op == ">")
                return cmp > 0;
            return cmp < 0;
        }

        bool EvalCondition(const std::string& cond, const States& states) {
            size_t pos = 0;
            while (pos <= cond.size()) {
                size_t or_ = cond.find("||", pos);
                const std::string term = cond.substr(pos, or_ == std::string::npos ? std::string::npos : or_ - pos);
                bool all = true;
                size_t p = 0;
                while (p <= term.size()) {
                    size_t and_ = term.find("&&", p);
                    const std::string atom = term.substr(p, and_ == std::string::npos ? std::string::npos : and_ - p);
                    if (!EvalAtom(atom, states)) {
                        all = false;
                        break;
                    }
                    if (and_ == std::string::npos)
                        break;
                    p = and_ + 2;
                }
                if (all)
                    return true;
                if (or_ == std::string::npos)
                    break;
                pos = or_ + 2;
            }
            return false;
        }
    }

    // poly_mesh: indexed polygons ([position, normal, uv] per corner), or parallel arrays grouped by "tri_list" / "quad_list".
    void BlockGeometry::ReadPolyMesh(const json::View mesh, Bone& bone) {
        const json::View positions = mesh["positions"], uvs = mesh["uvs"], polys = mesh["polys"];
        if (!positions.isArray())
            return;
        bone.polyNormalized = mesh["normalized_uvs"].boolean(false);
        const auto corner = [&](Bone::Poly& poly, int k, size_t pi, size_t ui) {
            if (pi < positions.size())
                ReadVec(positions[pi], poly.pos[k], 3);
            if (uvs.isArray() && ui < uvs.size())
                ReadVec(uvs[ui], poly.uv[k], 2);
        };
        const auto finish = [&](Bone::Poly& poly, int count) {
            if (count < 3)
                return;
            for (int k = count; k < 4; k++) {
                std::copy_n(poly.pos[count - 1], 3, poly.pos[k]);
                std::copy_n(poly.uv[count - 1], 2, poly.uv[k]);
            }
            bone.polys.push_back(poly);
        };
        if (polys.isArray()) {
            for (const json::View p : polys.items()) {
                Bone::Poly poly;
                int count = 0;
                for (const json::View c : p.items()) {
                    if (count == 4 || !c.isArray() || c.size() < 3)
                        break;
                    corner(poly, count++, static_cast<size_t>(c[0].integer(-1)), static_cast<size_t>(c[2].integer(-1)));
                }
                finish(poly, count);
            }
        } else if (polys.isString()) {
            const size_t group = polys.str() == "quad_list" ? 4 : polys.str() == "tri_list" ? 3 : 0;
            for (size_t i = 0; group && i + group <= positions.size(); i += group) {
                Bone::Poly poly;
                for (size_t k = 0; k < group; k++)
                    corner(poly, static_cast<int>(k), i + k, i + k);
                finish(poly, static_cast<int>(group));
            }
        }
    }

    void BlockGeometry::LoadGeometries(const std::string& dir) {
        std::error_code ec;
        if (!fs::exists(dir, ec))
            return;
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (!entry.is_regular_file())
                continue;
            // the game reads every json under models/: packs name block models plainly (models/blocks/pad.json)
            if (entry.path().extension() != ".json")
                continue;
            const auto doc = json::Document::Parse(ReadAll(entry.path()), true);
            if (!doc)
                continue;
            const json::View root = doc->root();
            auto addGeometry = [&](const std::string& id, const json::View g, bool legacy) {
                Geometry geo;
                const json::View desc = legacy ? g : g["description"];
                geo.tw = desc["texture_width"].real(desc["texturewidth"].real(16));
                geo.th = desc["texture_height"].real(desc["textureheight"].real(16));
                for (const json::View b : g["bones"].items()) {
                    Bone bone;
                    bone.name = b["name"].str();
                    bone.parent = b["parent"].str();
                    ReadVec(b["pivot"], bone.pivot, 3);
                    ReadVec(b["rotation"], bone.rot, 3);
                    for (const json::View c : b["cubes"].items()) {
                        Cube cube;
                        ReadVec(c["origin"], cube.origin, 3);
                        ReadVec(c["size"], cube.size, 3);
                        // Without a pivot a cube turns around its own centre.
                        if (c["pivot"].isArray())
                            ReadVec(c["pivot"], cube.pivot, 3);
                        else
                            for (int a = 0; a < 3; a++)
                                cube.pivot[a] = cube.origin[a] + cube.size[a] * 0.5f;
                        ReadVec(c["rotation"], cube.rot, 3);
                        cube.inflate = c["inflate"].real(b["inflate"].real(0));
                        cube.mirror = c["mirror"].boolean(b["mirror"].boolean(false));
                        const json::View uv = c["uv"];
                        if (uv.isArray()) {
                            cube.boxUv = true;
                            ReadVec(uv, cube.uvBox, 2);
                        } else if (uv.isObject()) {
                            for (int f = 0; f < 6; f++) {
                                const json::View fv = uv[kFaceNames[f]];
                                if (!fv.isObject())
                                    continue;
                                cube.faces[f].has = true;
                                ReadVec(fv["uv"], cube.faces[f].uv, 2);
                                ReadVec(fv["uv_size"], cube.faces[f].size, 2);
                                cube.faces[f].rotation = ((static_cast<int>(std::lround(fv["uv_rotation"].real(0) / 90.0f)) % 4) + 4) % 4;
                                cube.faces[f].mat = fv["material_instance"].str();
                            }
                        }
                        bone.cubes.push_back(cube);
                    }
                    if (const json::View mesh = b["poly_mesh"]; mesh.isObject())
                        ReadPolyMesh(mesh, bone);
                    geo.bones.push_back(std::move(bone));
                }
                geometries[id] = std::move(geo);
            };
            if (root["minecraft:geometry"].isArray()) {
                for (const json::View g : root["minecraft:geometry"].items()) {
                    std::string id = g["description"]["identifier"].str();
                    if (!id.empty())
                        addGeometry(id, g, false);
                }
            } else {
                for (const auto m : root.members())
                    if (m.key.rfind("geometry.", 0) == 0)
                        addGeometry(m.key, m.value, true);
            }
        }
    // Blocks that reference the builtin full cube (e.g. pillars with per-face materials) have no
    // geometry file anywhere; synthesize a unit cube whose faces use the standard material names
    // so per-face tiles (or the "*" fallback in Bake) resolve.
    {
        Geometry full;
        full.tw = 16;
        full.th = 16;
        Bone bone;
        bone.name = "body";
        Cube cube;
        cube.origin[0] = -8;
        cube.origin[1] = 0;
        cube.origin[2] = -8;
        cube.size[0] = 16;
        cube.size[1] = 16;
        cube.size[2] = 16;
        for (int f = 0; f < 6; f++) {
            cube.faces[f].has = true;
            cube.faces[f].uv[0] = 0;
            cube.faces[f].uv[1] = 0;
            cube.faces[f].size[0] = 16;
            cube.faces[f].size[1] = 16;
            cube.faces[f].mat = kFaceNames[f];
        }
        bone.cubes.push_back(std::move(cube));
        full.bones.push_back(std::move(bone));
        geometries["minecraft:geometry.full_block"] = std::move(full);
    }
    }

    static bool ParseComponentSet(const json::View comps, const std::unordered_map<std::string, std::string>& terrain,
                                  const std::string& packRoot, const BlockGeometry::FetchFn& fetch,
                                  std::string& geometryOut, bool& hasGeometry, bool& hasMaterials,
                                  std::unordered_map<std::string, int>& tiles,
                                  std::unordered_map<std::string, bool>& dimming, bool& hasTransform, float* rot,
                                  float* trans, float* scale) {
        const json::View g = comps["minecraft:geometry"];
        if (g.isString()) {
            geometryOut = g.str();
            hasGeometry = true;
        } else if (g.isObject()) {
            geometryOut = g["identifier"].str();
            hasGeometry = !geometryOut.empty();
        }
        const json::View mi = comps["minecraft:material_instances"];
        if (mi.isObject()) {
            hasMaterials = true;
            // resource packs write instances directly ({"*": {...}, "up": {...}}); server block definitions (network nbt)
            // nest them under "materials", with "mappings" naming the instance per face
            const json::View table = mi["materials"].isObject() ? mi["materials"] : mi;
            for (const auto m : table.members()) {
                if (!m.value.isObject())
                    continue;
                const std::string key = m.key;
                const std::string tex = m.value["texture"].str();
                int tile = 0;
                auto it = terrain.find(tex);
                if (it != terrain.end())
                    tile = fetch(it->second, packRoot);
                if (!tile) {
                    // Network material names are namespaced ("hive:small_pot"); the atlas stems
                    // and terrain_texture.json paths are not, so retry with the bare name.
                    std::string bare = tex;
                    const size_t colon = bare.find(':');
                    if (colon != std::string::npos)
                        bare.erase(0, colon + 1);
                    tile = fetch("textures/blocks/" + bare, packRoot);
                }
                tiles[key] = tile;
                dimming[key] = m.value["face_dimming"].boolean(true);
            }
            if (mi["mappings"].isObject())
                for (const auto m : mi["mappings"].members()) {
                    if (!m.value.isString())
                        continue;
                    const auto target = tiles.find(m.value.str());
                    if (target != tiles.end() && !tiles.count(m.key)) {
                        tiles[m.key] = target->second;
                        dimming[m.key] = dimming[m.value.str()];
                    }
                }
        }
        const json::View tr = comps["minecraft:transformation"];
        if (tr.isObject()) {
            hasTransform = true;
            ReadVec(tr["rotation"], rot, 3);
            ReadVec(tr["translation"], trans, 3);
            ReadVec(tr["scale"], scale, 3);
            // Network-serialized blocks use single-letter keys (TX/TY/TZ/RX/RY/RZ/SX/SY/SZ);
            // rotations are quarter-turns, so scale them to degrees like the array form.
            if (!tr["rotation"].isArray() && !tr["translation"].isArray() && !tr["scale"].isArray()) {
                auto num = [&](const char* key) -> float {
                    const json::View v = tr[key];
                    return v.isNumber() ? static_cast<float>(v.real()) : 0.0f;
                };
                auto numOr = [&](const char* key, float fallback) -> float {
                    const json::View v = tr[key];
                    return v.isNumber() ? static_cast<float>(v.real()) : fallback;
                };
                const float rx = num("RX"), ry = num("RY"), rz = num("RZ");
                const float tx = num("TX"), ty = num("TY"), tz = num("TZ");
                if (tr["RX"].isNumber() || tr["RY"].isNumber() || tr["RZ"].isNumber()) {
                    rot[0] = rx * 90.0f;
                    rot[1] = ry * 90.0f;
                    rot[2] = rz * 90.0f;
                }
                if (tr["TX"].isNumber() || tr["TY"].isNumber() || tr["TZ"].isNumber()) {
                    trans[0] = tx;
                    trans[1] = ty;
                    trans[2] = tz;
                }
                if (tr["SX"].isNumber() || tr["SY"].isNumber() || tr["SZ"].isNumber()) {
                    scale[0] = numOr("SX", 1.0f);
                    scale[1] = numOr("SY", 1.0f);
                    scale[2] = numOr("SZ", 1.0f);
                }
            }
        }
        return hasGeometry || hasMaterials || hasTransform;
    }

    void BlockGeometry::ScanPack(const std::string& packRoot, const std::string& blocksDir, const FetchFn& fetch) {
        LoadGeometries(packRoot + "/models");
        if (geometries.empty())
            return;

        std::unordered_map<std::string, std::string> terrain;
        if (const auto doc = json::Document::Parse(ReadAll(packRoot + "/textures/terrain_texture.json"), true)) {
            const json::View root = doc->root();
            const json::View data = root.contains("texture_data") ? root["texture_data"] : root;
            for (const auto m : data.members()) {
                std::string path;
                const json::View t = m.value["textures"];
                if (t.isString())
                    path = t.str();
                else if (t.isArray() && t.size() > 0)
                    path = t[(size_t)0].isString() ? t[(size_t)0].str() : t[(size_t)0]["path"].str();
                if (!path.empty())
                    terrain[m.key] = path;
            }
        }

        std::error_code ec;
        if (!fs::exists(blocksDir, ec))
            return;
        int found = 0;
        for (const auto& entry : fs::recursive_directory_iterator(blocksDir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            const auto doc = json::Document::Parse(ReadAll(entry.path()), true);
            if (!doc)
                continue;
            const json::View def = doc->root()["minecraft:block"];
            const std::string id = def["description"]["identifier"].str();
            if (id.empty())
                continue;
            Block block;
            ParseComponentSet(def["components"], terrain, packRoot, fetch, block.base.geometry,
                              block.base.hasGeometry, block.base.hasMaterials, block.base.tiles, block.base.dimming,
                              block.base.hasTransform, block.base.rot, block.base.trans, block.base.scale);
            bool any = block.base.hasGeometry;
            for (const json::View p : def["permutations"].items()) {
                Components c;
                if (ParseComponentSet(p["components"], terrain, packRoot, fetch, c.geometry, c.hasGeometry,
                                      c.hasMaterials, c.tiles, c.dimming, c.hasTransform, c.rot, c.trans, c.scale)) {
                    any = any || c.hasGeometry;
                    block.perms.emplace_back(p["condition"].str(), std::move(c));
                }
            }
            if (!any)
                continue;
            blocks[id] = std::move(block);
            found++;
        }
        std::printf("[geometry] %s: %zu geometries, %d geometry blocks\n", packRoot.c_str(), geometries.size(),
                    found);
    }

    std::vector<ModelQuad> BlockGeometry::Bake(const Geometry& g, const Components& c) const {
        std::vector<ModelQuad> out;
        const Mat3 blockRot = c.hasTransform ? BedrockRotation(c.rot) : Identity();
        auto place = [&](float v[3]) {
            v[0] = v[0] / 16.0f + 0.5f;
            v[1] = v[1] / 16.0f;
            v[2] = v[2] / 16.0f + 0.5f;
            if (c.hasTransform) {
                const float centre[3] = {0.5f, 0.5f, 0.5f};
                for (int i = 0; i < 3; i++)
                    v[i] = centre[i] + (v[i] - centre[i]) * c.scale[i];
                Apply(blockRot, centre, v);
                for (int i = 0; i < 3; i++)
                    v[i] += c.trans[i] / 16.0f;
            }
        };
        std::unordered_map<std::string, const Bone*> byName;
        for (const Bone& b : g.bones)
            byName[b.name] = &b;

        for (const Bone& bone : g.bones) {
            std::vector<const Bone*> chain;
            for (const Bone* b = &bone; b && chain.size() < 16;) {
                chain.push_back(b);
                auto it = b->parent.empty() ? byName.end() : byName.find(b->parent);
                b = it == byName.end() ? nullptr : it->second;
            }
            for (const Cube& cube : bone.cubes) {
                float lo[3], hi[3];
                for (int a = 0; a < 3; a++) {
                    lo[a] = cube.origin[a] - cube.inflate;
                    hi[a] = cube.origin[a] + cube.size[a] + cube.inflate;
                }
                const bool rotated = cube.rot[0] != 0 || cube.rot[1] != 0 || cube.rot[2] != 0;
                const Mat3 cubeRot = rotated ? BedrockRotation(cube.rot) : Identity();
                // Box UV lays the faces out with whole-pixel sizes.
                const float w = std::floor(cube.size[0]), h = std::floor(cube.size[1]), d = std::floor(cube.size[2]);
                for (int f = 0; f < 6; f++) {
                    float u0, v0, u1, v1;
                    std::string mat;
                    if (cube.boxUv) {
                        const float u = cube.uvBox[0], v = cube.uvBox[1];
                        float fu, fv, fw, fh;
                        switch (f) {
                        case 1: fu = u + d; fv = v; fw = w; fh = d; break;
                        case 0: fu = u + d + w; fv = v; fw = w; fh = d; break;
                        case 2: fu = u + d; fv = v + d; fw = w; fh = h; break;
                        case 5: fu = u; fv = v + d; fw = d; fh = h; break;
                        case 3: fu = u + d + w + d; fv = v + d; fw = w; fh = h; break;
                        default: fu = u + d + w; fv = v + d; fw = d; fh = h; break;
                        }
                        u0 = fu; v0 = fv; u1 = fu + fw; v1 = fv + fh;
                        mat = "*";
                    } else {
                        if (!cube.faces[f].has)
                            continue;
                        const auto& fc = cube.faces[f];
                        u0 = fc.uv[0]; v0 = fc.uv[1];
                        u1 = fc.uv[0] + fc.size[0]; v1 = fc.uv[1] + fc.size[1];
                        mat = fc.mat.empty() ? "*" : fc.mat;
                    }
                    auto tit = c.tiles.find(mat);
                    if (tit == c.tiles.end())
                        tit = c.tiles.find("*");
                    if (tit == c.tiles.end() || tit->second == 0)
                        continue;
                    if (cube.mirror)
                        std::swap(u0, u1);
                    ModelQuad q{};
                    q.tile = tit->second;
                    q.tex = 0xFFFF;
                    q.tint = -1;
                    q.cull = -1;
                    q.light = 0;
                    auto dit = c.dimming.find(mat);
                    q.shade = (dit == c.dimming.end() || dit->second) ? 1 : 0;
                    float cu[4] = {u0, u0, u1, u1}, cv[4] = {v0, v1, v1, v0};
                    // uv_rotation turns the texture clockwise: going round the face (corners 0, 3, 2, 1 here), each corner
                    // takes the UV of the one before it.
                    if (const int turns = cube.boxUv ? 0 : cube.faces[f].rotation) {
                        static constexpr int kRing[4] = {0, 3, 2, 1};
                        float ru[4], rv[4];
                        for (int k = 0; k < 4; k++) {
                            ru[k] = cu[kRing[k]];
                            rv[k] = cv[kRing[k]];
                        }
                        for (int k = 0; k < 4; k++) {
                            cu[kRing[k]] = ru[(k + 4 - turns) % 4];
                            cv[kRing[k]] = rv[(k + 4 - turns) % 4];
                        }
                    }
                    bool flush = true;
                    for (int i = 0; i < 4; i++) {
                        float p[3];
                        for (int a = 0; a < 3; a++)
                            p[a] = kFaceVerts[f][i][a] ? hi[a] : lo[a];
                        if (rotated)
                            Apply(cubeRot, cube.pivot, p);
                        for (const Bone* b : chain) {
                            const bool br = b->rot[0] != 0 || b->rot[1] != 0 || b->rot[2] != 0;
                            if (br)
                                Apply(BedrockRotation(b->rot), b->pivot, p);
                        }
                        place(p);
                        for (int a = 0; a < 3; a++)
                            q.pos[i][a] = p[a];
                        q.uv[i][0] = cu[i] * 16.0f / g.tw;
                        q.uv[i][1] = cv[i] * 16.0f / g.th;
                    }
                    const float e1[3] = {q.pos[1][0] - q.pos[0][0], q.pos[1][1] - q.pos[0][1], q.pos[1][2] - q.pos[0][2]};
                    const float e2[3] = {q.pos[2][0] - q.pos[0][0], q.pos[2][1] - q.pos[0][1], q.pos[2][2] - q.pos[0][2]};
                    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                        e1[0] * e2[1] - e1[1] * e2[0]};
                    if (std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]) < 1e-9f)
                        continue;
                    q.dir = static_cast<uint8_t>(NearestDir(n));
                    q.shadeDir = q.dir;
                    const int axis = q.dir < 2 ? 1 : (q.dir < 4 ? 2 : 0);
                    const float plane = (q.dir & 1) ? 1.0f : 0.0f;
                    for (int i = 0; i < 4; i++)
                        if (std::fabs(q.pos[i][axis] - plane) > 1e-4f)
                            flush = false;
                    if (flush)
                        q.cull = static_cast<int8_t>(q.dir);
                    out.push_back(q);
                }
            }
            if (bone.polys.empty())
                continue;
            auto tit = c.tiles.find("*");
            if (tit == c.tiles.end() || tit->second == 0)
                continue;
            for (const Bone::Poly& poly : bone.polys) {
                ModelQuad q{};
                q.tile = tit->second;
                q.tex = 0xFFFF;
                q.tint = -1;
                q.cull = -1;
                q.light = 0;
                const auto dit = c.dimming.find("*");
                q.shade = (dit == c.dimming.end() || dit->second) ? 1 : 0;
                for (int i = 0; i < 4; i++) {
                    float p[3] = {poly.pos[i][0], poly.pos[i][1], poly.pos[i][2]};
                    for (const Bone* b : chain)
                        if (b->rot[0] != 0 || b->rot[1] != 0 || b->rot[2] != 0)
                            Apply(BedrockRotation(b->rot), b->pivot, p);
                    place(p);
                    std::copy_n(p, 3, q.pos[i]);
                    // Mesh UVs count from the bottom of the texture.
                    const float u = bone.polyNormalized ? poly.uv[i][0] : poly.uv[i][0] / g.tw;
                    const float v = bone.polyNormalized ? poly.uv[i][1] : poly.uv[i][1] / g.th;
                    q.uv[i][0] = u * 16.0f;
                    q.uv[i][1] = (1.0f - v) * 16.0f;
                }
                const float e1[3] = {q.pos[1][0] - q.pos[0][0], q.pos[1][1] - q.pos[0][1], q.pos[1][2] - q.pos[0][2]};
                const float e2[3] = {q.pos[2][0] - q.pos[0][0], q.pos[2][1] - q.pos[0][1], q.pos[2][2] - q.pos[0][2]};
                const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                if (std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]) < 1e-9f)
                    continue;
                q.dir = static_cast<uint8_t>(NearestDir(n));
                q.shadeDir = q.dir;
                out.push_back(q);
            }
        }
        return out;
    }

    void BlockGeometry::Clear() {
        std::unique_lock lock(mutex);
        blocks.clear();
        geometries.clear();
        byId.clear();
        loadedPacks.clear();
        terrainKey.clear();
        customTerrain.clear();
    }

    bool EvaluateBlockCondition(const std::string& condition, uint16_t localId) {
        return EvalCondition(condition, BlockPalette::Instance().States(localId));
    }

    void BlockGeometry::AddCustom(const std::string& name, const std::string& definitionJson, const std::vector<std::string>& packRoots,
                                  const std::string& textureRoot, const FetchFn& fetch) {
        const auto doc = json::Document::Parse(definitionJson, true);
        if (!doc)
            return;
        std::unique_lock lock(mutex);
        std::string key;
        for (const std::string& root : packRoots)
            key += root + "|";
        for (const std::string& root : packRoots) {
            if (std::find(loadedPacks.begin(), loadedPacks.end(), root) == loadedPacks.end()) {
                loadedPacks.push_back(root);
                LoadGeometries(root + "/models");
            }
        }
        if (key != terrainKey) {
            terrainKey = key;
            customTerrain.clear();
            for (const std::string& root : packRoots) {
                const auto terrainDoc = json::Document::Parse(ReadAll(root + "/textures/terrain_texture.json"), true);
                if (!terrainDoc)
                    continue;
                const json::View troot = terrainDoc->root();
                const json::View data = troot.contains("texture_data") ? troot["texture_data"] : troot;
                for (const auto m : data.members()) {
                    std::string path;
                    const json::View t = m.value["textures"];
                    if (t.isString())
                        path = t.str();
                    else if (t.isArray() && t.size() > 0)
                        path = t[(size_t)0].isString() ? t[(size_t)0].str() : t[(size_t)0]["path"].str();
                    if (!path.empty())
                        customTerrain[m.key] = path;
                }
            }
        }
        const json::View root = doc->root();
        Block block;
        ParseComponentSet(root["components"], customTerrain, textureRoot, fetch, block.base.geometry, block.base.hasGeometry,
                          block.base.hasMaterials, block.base.tiles, block.base.dimming, block.base.hasTransform, block.base.rot,
                          block.base.trans, block.base.scale);
        bool any = block.base.hasGeometry;
        for (const json::View p : root["permutations"].items()) {
            Components c;
            if (ParseComponentSet(p["components"], customTerrain, textureRoot, fetch, c.geometry, c.hasGeometry, c.hasMaterials, c.tiles,
                                  c.dimming, c.hasTransform, c.rot, c.trans, c.scale)) {
                any = any || c.hasGeometry;
                block.perms.emplace_back(p["condition"].str(), std::move(c));
            }
        }
        if (any)
            blocks[name] = std::move(block);
        else
            blocks.erase(name);
        byId.clear();
    }

    const ModelEntry* BlockGeometry::Find(uint16_t localId) {
        std::shared_lock readLock(mutex);
        if (blocks.empty())
            return nullptr;
        {
            auto it = byId.find(localId);
            if (it != byId.end())
                return it->second;
        }
        auto& pal = BlockPalette::Instance();
        const std::string name = pal.Name(localId);
        const ModelEntry* result = nullptr;
        ModelEntry pending;
        bool havePending = false;
        auto bit = blocks.find(name);
        if (bit != blocks.end()) {
            const Block& block = bit->second;
            const States states = pal.States(localId);
            Components eff = block.base;
            for (const auto& [cond, comp] : block.perms) {
                if (!EvalCondition(cond, states))
                    continue;
                if (comp.hasGeometry) {
                    eff.hasGeometry = true;
                    eff.geometry = comp.geometry;
                }
                if (comp.hasMaterials) {
                    for (const auto& [k, v] : comp.tiles)
                        eff.tiles[k] = v;
                    for (const auto& [k, v] : comp.dimming)
                        eff.dimming[k] = v;
                }
                if (comp.hasTransform) {
                    eff.hasTransform = true;
                    for (int i = 0; i < 3; i++) {
                        eff.rot[i] = comp.rot[i];
                        eff.trans[i] = comp.trans[i];
                        eff.scale[i] = comp.scale[i];
                    }
                }
            }
            if (eff.hasGeometry) {
                auto git = geometries.find(eff.geometry);
                if (git == geometries.end() && eff.geometry.rfind("minecraft:", 0) == 0)
                    git = geometries.find(eff.geometry.substr(10));
                if (git == geometries.end())
                    git = geometries.find("minecraft:" + eff.geometry);
                if (git != geometries.end()) {
                    ModelEntry entry;
                    entry.direct = true;
                    entry.quads = Bake(git->second, eff);
                    constexpr size_t kMaxQuadsPerBlock = 160;
                    if (entry.quads.size() > kMaxQuadsPerBlock) {
                        static std::mutex reportMutex;
                        static std::unordered_set<std::string> reported;
                        {
                            std::lock_guard<std::mutex> reportLock(reportMutex);
                            if (reported.insert(name).second)
                                std::cout << "[perf] block " << name << " has " << entry.quads.size()
                                          << " quads; keeping the " << kMaxQuadsPerBlock << " largest" << std::endl;
                        }
                        const auto area = [](const ModelQuad& q) {
                            const float ax = q.pos[1][0] - q.pos[0][0], ay = q.pos[1][1] - q.pos[0][1], az = q.pos[1][2] - q.pos[0][2];
                            const float bx = q.pos[3][0] - q.pos[0][0], by = q.pos[3][1] - q.pos[0][1], bz = q.pos[3][2] - q.pos[0][2];
                            const float cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
                            return cx * cx + cy * cy + cz * cz;
                        };
                        std::vector<size_t> order(entry.quads.size());
                        for (size_t i = 0; i < order.size(); i++)
                            order[i] = i;
                        std::nth_element(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(kMaxQuadsPerBlock), order.end(),
                                         [&](size_t l, size_t r) { return area(entry.quads[l]) > area(entry.quads[r]); });
                        order.resize(kMaxQuadsPerBlock);
                        std::sort(order.begin(), order.end());
                        std::vector<ModelQuad> kept;
                        kept.reserve(kMaxQuadsPerBlock);
                        for (const size_t i : order)
                            kept.push_back(entry.quads[i]);
                        entry.quads = std::move(kept);
                    }
                    if (!entry.quads.empty()) {
                        pending = std::move(entry);
                        havePending = true;
                    }
                }
            }
        }
        readLock.unlock();
        std::unique_lock lock(mutex);
        if (havePending) {
            storage.push_back(std::move(pending));
            result = &storage.back();
        }
        byId[localId] = result;
        return result;
    }
}
