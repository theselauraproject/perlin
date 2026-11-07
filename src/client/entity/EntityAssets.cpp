#include <functional>
#include "util/Env.hpp"
#include "client/entity/EntityAssets.hpp"

#include "client/ui/Json.hpp"
#include "client/resources/ServerPacks.hpp"
#include "util/Paths.hpp"
#include "util/ThreadPool.hpp"
#include "util/FileIo.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "util/Log.hpp"

namespace perlin::entity {
    namespace fs = std::filesystem;
    using Json = ui::Json;
    using molang::Expr;
    using molang::Value;

    std::string LowerCase(std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string AfterDot(const std::string& ref) {
        const size_t d = ref.find('.');
        return d == std::string::npos ? ref : ref.substr(d + 1);
    }

    namespace {

        std::vector<fs::path> JsonFiles(const fs::path& dir) {
            std::vector<fs::path> files;
            std::error_code ec;
            for (const auto& e :
                 fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
                if (e.is_regular_file() && e.path().extension() == ".json")
                    files.push_back(e.path());
            const auto legacy = [](const fs::path& p) { return p.filename().string().contains(".v1."); };
            std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
                if (legacy(a) != legacy(b))
                    return legacy(a);
                return a < b;
            });
            return files;
        }

        ExprPtr ParseValue(const Json& v) {
            if (v.IsString())
                return Expr::Parse(v.string);
            if (v.IsNumber())
                return Expr::Constant(v.number);
            if (v.IsBool())
                return Expr::Constant(v.boolean ? 1.0 : 0.0);
            return nullptr;
        }

        void ParseEntity(const Json& doc, std::unordered_map<std::string, ClientEntityDef>& out) {
            const Json* client = doc.Find("minecraft:client_entity");
            const Json* desc = client ? client->Find("description") : nullptr;
            if (!desc || !desc->Find("identifier"))
                return;
            ClientEntityDef d;
            d.identifier = desc->Find("identifier")->StringOr("");
            const auto readMap = [&](const char* key, std::map<std::string, std::string>& dest) {
                if (const Json* m = desc->Find(key); m && m->IsObject())
                    for (const auto& [k, v] : m->object)
                        if (v.IsString())
                            dest[LowerCase(k)] = v.string;
            };
            readMap("textures", d.textures);
            readMap("geometry", d.geometries);
            readMap("animations", d.animations);
            readMap("materials", d.materials);
            readMap("particle_effects", d.particleEffects);
            if (const Json* scripts = desc->Find("scripts")) {
                const auto readScripts = [&](const char* key, std::vector<ExprPtr>& dest) {
                    if (const Json* l = scripts->Find(key); l && l->IsArray())
                        for (const Json& s : l->array)
                            if (s.IsString())
                                dest.push_back(Expr::Parse(s.string));
                };
                readScripts("initialize", d.initialize);
                if (const Json* l = scripts->Find("pre_animation"); l && l->IsArray()) {
                    std::string joined;
                    for (const Json& s : l->array)
                        if (s.IsString())
                            joined += s.string + "\n";
                    d.preAnimation.push_back(Expr::Parse(joined));
                }
                if (const Json* l = scripts->Find("animate"); l && l->IsArray())
                    for (const Json& a : l->array) {
                        if (a.IsString())
                            d.animate.push_back({LowerCase(a.string), nullptr});
                        else if (a.IsObject())
                            for (const auto& [target, cond] : a.object)
                                d.animate.push_back({LowerCase(target), ParseValue(cond)});
                    }
                if (const Json* s = scripts->Find("scale"))
                    d.scale = ParseValue(*s);
            }
            if (d.animate.empty())
                if (const Json* acs = desc->Find("animation_controllers"); acs && acs->IsArray())
                    for (const Json& a : acs->array)
                        if (a.IsObject())
                            for (const auto& [shortName, id] : a.object)
                                if (id.IsString()) {
                                    const std::string key = LowerCase(shortName);
                                    d.controllers[key] = id.string;
                                    d.animate.push_back({"ctrl:" + key, nullptr});
                                }
            if (const Json* rcs = desc->Find("render_controllers"); rcs && rcs->IsArray())
                for (const Json& r : rcs->array) {
                    if (r.IsString())
                        d.renderControllers.push_back({r.string, nullptr});
                    else if (r.IsObject())
                        for (const auto& [id, cond] : r.object)
                            d.renderControllers.push_back({id, ParseValue(cond)});
                }
            d.ok = !d.geometries.empty() || !d.renderControllers.empty();
            if (d.ok)
                out[d.identifier] = std::move(d);
        }

        void ParseRenderControllers(const Json& doc,
                                    std::unordered_map<std::string, RenderControllerDef>& out) {
            const Json* list = doc.Find("render_controllers");
            if (!list || !list->IsObject())
                return;
            for (const auto& [id, def] : list->object) {
                RenderControllerDef rc;
                if (const Json* g = def.Find("geometry"); g && g->IsString())
                    rc.geometry = Expr::Parse(g->string);
                if (const Json* t = def.Find("textures"); t && t->IsArray())
                    for (const Json& e : t->array)
                        if (e.IsString())
                            rc.textures.push_back(Expr::Parse(e.string));
                if (const Json* arrays = def.Find("arrays"); arrays && arrays->IsObject())
                    for (const auto& [kind, group] : arrays->object)
                        if (group.IsObject())
                            for (const auto& [name, values] : group.object)
                                if (values.IsArray())
                                    for (const Json& v : values.array)
                                        rc.arrays[LowerCase(name)].push_back(
                                            Value::Str(LowerCase(v.StringOr(""))));
                if (const Json* parts = def.Find("part_visibility"); parts && parts->IsArray())
                    for (const Json& entry : parts->array)
                        if (entry.IsObject())
                            for (const auto& [pattern, expr] : entry.object)
                                rc.parts.push_back({LowerCase(pattern), ParseValue(expr)});
                if (const Json* il = def.Find("ignore_lighting"); il && il->IsBool())
                    rc.ignoreLighting = il->boolean;
                if (const Json* lm = def.Find("light_color_multiplier"); lm && lm->IsNumber())
                    rc.lightMultiplier = static_cast<float>(lm->number);
                else if (lm && lm->IsString())
                    rc.lightMultiplierExpr = ParseValue(*lm);
                const auto readRgba = [&](const char* key, ExprPtr* out, bool& has) {
                    const Json* c = def.Find(key);
                    if (!c || !c->IsObject())
                        return;
                    has = true;
                    static const char* kKeys[4] = {"r", "g", "b", "a"};
                    for (int i = 0; i < 4; i++)
                        if (const Json* v = c->Find(kKeys[i]))
                            out[i] = ParseValue(*v);
                };
                readRgba("overlay_color", rc.overlay, rc.hasOverlay);
                readRgba("is_hurt_color", rc.hurtColor, rc.hasHurtColor);
                readRgba("on_fire_color", rc.fireColor, rc.hasFireColor);
                if (const Json* uv = def.Find("uv_anim"); uv && uv->IsObject()) {
                    const auto pair = [&](const char* key, ExprPtr* out) {
                        if (const Json* a = uv->Find(key); a && a->IsArray())
                            for (size_t i = 0; i < 2 && i < a->array.size(); i++)
                                out[i] = ParseValue(a->array[i]);
                    };
                    pair("offset", rc.uvOffset);
                    pair("scale", rc.uvScale);
                    rc.hasUvAnim = true;
                }
                if (const Json* c = def.Find("color"); c && c->IsObject()) {
                    rc.hasColor = true;
                    static const char* kKeys[4] = {"r", "g", "b", "a"};
                    for (int i = 0; i < 4; i++)
                        if (const Json* v = c->Find(kKeys[i]))
                            rc.color[i] = ParseValue(*v);
                }
                if (const Json* m = def.Find("materials"); m && m->IsArray())
                    for (const Json& e : m->array)
                        if (e.IsObject())
                            for (const auto& [k, v] : e.object) {
                                rc.materials.push_back({LowerCase(k), ParseValue(v)});
                                const std::string mat = LowerCase(v.StringOr(""));
                                if (mat.contains("alpha") || mat.contains("translucent") ||
                                    mat.contains("spectator") || mat.contains("ghost"))
                                    rc.translucent = true;
                            }
                out[id] = std::move(rc);
            }
        }
    }

    void EntityAssets::Configure(const std::string& newRoot) {
        if (loaded && root == newRoot) {
            SyncOverlays();
            return;
        }
        root = newRoot;
        loaded = false;
        overlaysLoaded = 0;
        overlayGeneration = 0;
        entities.clear();
        controllers.clear();
        animations = attachable::AnimationLibrary();
        geometries = attachable::GeometryLibrary();
        if (root.empty())
            return;
        materialLibrary.Load(root);
        LoadAll();
        loaded = true;
        SyncOverlays();
    }

    void EntityAssets::SyncOverlays() {
        const uint32_t gen = ServerPacks::Instance().Generation();
        if (gen == overlayGeneration || ServerPacks::Instance().Mounting())
            return;
        const std::vector<std::string> dirs = ServerPacks::Instance().Directories();
        if (dirs.size() < overlaysLoaded) {
            const std::string keep = root;
            loaded = false;
            root.clear();
            Configure(keep);
            return;
        }
        overlayGeneration = gen;
        for (size_t i = overlaysLoaded; i < dirs.size(); i++) {
            materialLibrary.AddDirectory(dirs[i]);
            LoadDirectory(dirs[i], true);
        }
        overlaysLoaded = dirs.size();
    }

    namespace {
        const char* kBuiltinGeometry = R"GEO({
  "format_version": "1.8.0",
  "geometry.boat": {
    "texturewidth": 128, "textureheight": 64,
    "bones": [
      {"name": "root", "pivot": [0, 0, 0], "rotation": [0, -90, 0]},
      {"name": "bottom", "parent": "root", "pivot": [0, 3, 1], "rotation": [90, 0, 0],
       "cubes": [{"origin": [-14, -4, -2], "size": [28, 16, 3], "uv": [0, 0]}]},
      {"name": "back", "parent": "root", "pivot": [15, 2, 4], "rotation": [0, 270, 0],
       "cubes": [{"origin": [10, 3, 3], "size": [18, 6, 2], "uv": [0, 19]}]},
      {"name": "front", "parent": "root", "pivot": [-15, 2, 0], "rotation": [0, 90, 0],
       "cubes": [{"origin": [-23, 3, -1], "size": [16, 6, 2], "uv": [0, 27]}]},
      {"name": "right", "parent": "root", "pivot": [0, 2, -9], "rotation": [0, 180, 0],
       "cubes": [{"origin": [-14, 3, -10], "size": [28, 6, 2], "uv": [0, 35]}]},
      {"name": "left", "parent": "root", "pivot": [0, 2, 9],
       "cubes": [{"origin": [-14, 3, 8], "size": [28, 6, 2], "uv": [0, 43]}]},
      {"name": "paddle_left", "parent": "root", "pivot": [-3, 11, 9], "rotation": [-11.25, 0, -22.5],
       "cubes": [{"origin": [-4, 9, 4], "size": [2, 2, 18], "uv": [62, 0]}, {"origin": [-4.001, 8, 17], "size": [1, 6, 7], "uv": [62, 20]}]},
      {"name": "paddle_right", "parent": "root", "pivot": [-3, 11, -9], "rotation": [-11.25, 180, -22.5],
       "cubes": [{"origin": [-4, 9, -14], "size": [2, 2, 18], "uv": [62, 0]}, {"origin": [-4.001, 8, -24], "size": [1, 6, 7], "uv": [62, 20]}]}
    ]
  }
})GEO";
    }

    namespace {
        const char* kJavaAnimations = R"JAVA({
  "format_version": "1.8.0",
  "animations": {
    "animation.player.bob": {
      "loop": true,
      "bones": {
        "rightarm": {"rotation": ["math.sin(query.life_time * 76.8) * 2.865", 0.0, "(math.cos(query.life_time * 103.2) * 2.865) + 2.865"]},
        "leftarm": {"rotation": ["-math.sin(query.life_time * 76.8) * 2.865", 0.0, "-((math.cos(query.life_time * 103.2) * 2.865) + 2.865)"]}
      }
    },
    "animation.player.bob.stationary": {
      "loop": true,
      "bones": {
        "rightarm": {"rotation": ["math.sin(query.life_time * 76.8) * 2.865", 0.0, "(math.cos(query.life_time * 103.2) * 2.865) + 2.865"]},
        "leftarm": {"rotation": ["-math.sin(query.life_time * 76.8) * 2.865", 0.0, "-((math.cos(query.life_time * 103.2) * 2.865) + 2.865)"]}
      }
    },
    "animation.player.move.legs": {
      "loop": true,
      "bones": {
        "leftleg": {"rotation": ["variable.tcos0 * -1.4", -0.2865, -0.2865]},
        "rightleg": {"rotation": ["variable.tcos0 * 1.4", 0.2865, 0.2865]}
      }
    },
    "animation.player.sneaking": {
      "loop": true,
      "bones": {}
    },
    "animation.player.java_marker_v2": {"loop": true, "bones": {}},
    "animation.humanoid.bob": {
      "loop": true,
      "bones": {
        "rightarm": {"rotation": ["math.sin(query.life_time * 76.8) * 2.865", 0.0, "(math.cos(query.life_time * 103.2) * 2.865) + 2.865"]},
        "leftarm": {"rotation": ["-math.sin(query.life_time * 76.8) * 2.865", 0.0, "((math.cos(query.life_time * 103.2) * 2.865) + 2.865) * -1.0"]}
      }
    },
    "animation.humanoid.move": {
      "loop": true,
      "bones": {
        "leftarm": {"rotation": ["variable.tcos0", 0.0, 0.0]},
        "rightarm": {"rotation": ["-variable.tcos0", 0.0, 0.0]},
        "leftleg": {"rotation": ["variable.tcos0 * -1.4", -0.2865, -0.2865]},
        "rightleg": {"rotation": ["variable.tcos0 * 1.4", 0.2865, 0.2865]}
      }
    },
    "animation.polarbear.move": {
      "loop": true,
      "bones": {
        "body": {"rotation": ["(query.standing_scale * 63) - this", 0.0, 0.0]}
      }
    },
    "animation.humanoid.attack.rotations": {
      "loop": true,
      "bones": {
        "body": {"rotation": [0.0, "variable.attack_time > 0 ? math.sin(math.sqrt(variable.attack_time) * 360) * 11.46 - this : 0.0", 0.0]},
        "leftarm": {"rotation": ["variable.attack_time > 0 ? math.sin(math.sqrt(variable.attack_time) * 360) * 11.46 : 0.0", "variable.attack_time > 0 ? math.sin(math.sqrt(variable.attack_time) * 360) * 11.46 : 0.0", 0.0]},
        "rightarm": {"rotation": [
          "variable.attack_time > 0 ? -((math.sin((1.0 - math.pow(1.0 - variable.attack_time, 4.0)) * 180.0) * 1.2 + math.sin(variable.attack_time * 180.0) * -((query.target_x_rotation * 0.017453) - 0.7) * 0.75) * 57.2958) : 0.0",
          "variable.attack_time > 0 ? math.sin(math.sqrt(variable.attack_time) * 360) * 11.46 * 3.0 : 0.0",
          "variable.attack_time > 0 ? math.sin(variable.attack_time * 180.0) * -22.92 : 0.0"]}
      }
    }
  }
})JAVA";
        void LoadJavaOverrides(attachable::AnimationLibrary& animations) {
            if (PERLIN_ENV("PERLIN_NO_JAVA_ANIMATIONS"))
                return;
            const fs::path dir = fs::path(paths::DataDir()) / "animation_overrides";
            std::error_code ec;
            const fs::path file = dir / "java.animation.json";
            {
                bool current = false;
                if (std::ifstream in(file, std::ios::binary); in) {
                    std::stringstream ss;
                    ss << in.rdbuf();
                    current = ss.str().contains("animation.player.java_marker_v2");
                }
                if (!current) {
                    fs::create_directories(dir, ec);
                    std::ofstream out(file, std::ios::binary);
                    out << kJavaAnimations;
                }
            }
            std::string text;
            for (const fs::path& p : fs::directory_iterator(dir, ec))
                if (p.extension() == ".json") {
                    std::ifstream in(p, std::ios::binary);
                    std::stringstream ss;
                    ss << in.rdbuf();
                    animations.LoadAnimations(ss.str());
                    Log("entity", "animation override {}", p.filename().string());
                }
        }
    }

    void EntityAssets::LoadAll() {
        const auto start = std::chrono::steady_clock::now();
        std::string text;
        geometries.Load(kBuiltinGeometry);
        for (const fs::path& p : JsonFiles(fs::path(root) / "models" / "entity"))
            if (file::Read(p, text))
                geometries.Load(text);
        if (file::Read(fs::path(root) / "models" / "mobs.json", text))
            geometries.Load(text);
        for (const fs::path& p : JsonFiles(fs::path(root) / "animations"))
            if (file::Read(p, text))
                animations.LoadAnimations(text);
        geometries.Detach("geometry.polarbear", {"head", "leg0", "leg1", "leg2", "leg3"});
        LoadJavaOverrides(animations);
        for (const fs::path& p : JsonFiles(fs::path(root) / "animation_controllers"))
            if (file::Read(p, text))
                animations.LoadControllers(text);
        ThreadPool pool(HardwareThreads());
        const auto parseAll = [&pool](const std::vector<fs::path>& files, std::vector<Json>& docs,
                                      std::vector<char>& ok) {
            docs.assign(files.size(), Json());
            ok.assign(files.size(), 0);
            pool.ParallelFor(files.size(), [&](size_t i) {
                std::string body, error;
                ok[i] = file::Read(files[i], body) && Json::Parse(body, docs[i], error) ? 1 : 0;
            });
        };
        {
            const std::vector<fs::path> files = JsonFiles(fs::path(root) / "render_controllers");
            std::vector<Json> docs;
            std::vector<char> ok;
            parseAll(files, docs, ok);
            for (size_t i = 0; i < files.size(); i++)
                if (ok[i])
                    ParseRenderControllers(docs[i], controllers);
        }
        {
            const std::vector<fs::path> files = JsonFiles(fs::path(root) / "entity");
            std::vector<Json> docs;
            std::vector<char> ok;
            parseAll(files, docs, ok);
            for (size_t i = 0; i < files.size(); i++)
                if (ok[i])
                    ParseEntity(docs[i], entities);
        }
        {
            std::function<const Json*(const Json&)> findBox = [&](const Json& j) -> const Json* {
                if (j.IsObject()) {
                    if (const Json* box = j.Find("minecraft:collision_box"); box && box->IsObject())
                        return box;
                    for (const auto& [k, v] : j.object)
                        if (const Json* r = findBox(v))
                            return r;
                }
                return nullptr;
            };
            const std::vector<fs::path> behaviorFiles =
                JsonFiles(fs::path(root).parent_path() / "behavior_pack" / "entities");
            std::vector<Json> behaviorDocs;
            std::vector<char> behaviorOk;
            parseAll(behaviorFiles, behaviorDocs, behaviorOk);
            for (size_t behaviorIndex = 0; behaviorIndex < behaviorFiles.size(); behaviorIndex++) {
                if (!behaviorOk[behaviorIndex])
                    continue;
                const Json& doc = behaviorDocs[behaviorIndex];
                const Json* entity = doc.Find("minecraft:entity");
                const Json* desc = entity ? entity->Find("description") : nullptr;
                const Json* id = desc ? desc->Find("identifier") : nullptr;
                if (!id || !id->IsString() || !entity)
                    continue;
                {
                    std::function<const Json*(const Json&)> findRide = [&](const Json& j) -> const Json* {
                        if (j.IsObject()) {
                            if (const Json* r = j.Find("minecraft:rideable"); r && r->IsObject())
                                return r;
                            for (const auto& [k, v] : j.object)
                                if (const Json* r = findRide(v))
                                    return r;
                        }
                        return nullptr;
                    };
                    if (const Json* ride = findRide(*entity))
                        if (const Json* seat = ride->Find("seats")) {
                            const Json* first =
                                seat->IsArray() && !seat->array.empty() ? &seat->array[0] : seat;
                            if (const Json* pos = first->IsObject() ? first->Find("position") : nullptr)
                                if (pos->IsArray() && pos->array.size() >= 3 && pos->array[0].IsNumber())
                                    seats[id->string] = {static_cast<float>(pos->array[0].number),
                                                         static_cast<float>(pos->array[1].number),
                                                         static_cast<float>(pos->array[2].number)};
                        }
                }
                if (const Json* box = findBox(*entity)) {
                    const Json* w = box->Find("width");
                    const Json* hgt = box->Find("height");
                    if (w && hgt && w->IsNumber() && hgt->IsNumber())
                        collisionBoxes[id->string] = {static_cast<float>(w->number),
                                                      static_cast<float>(hgt->number)};
                }
            }
        }
        for (auto& [id, def] : entities)
            if (def.renderControllers.empty() && !def.geometries.empty())
                def.renderControllers.push_back({"controller.render.perlin_implicit_default", nullptr});
        for (const auto& [id, def] : entities)
            for (const auto& ref : def.renderControllers)
                if (!controllers.contains(ref.id)) {
                    RenderControllerDef rc;
                    rc.geometry = Expr::Parse("Geometry.default");
                    rc.textures.push_back(Expr::Parse("Texture.default"));
                    controllers[ref.id] = std::move(rc);
                }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        Log("entity", "loaded {} entity definitions, {} geometries, {} render controllers in {} ms",
            entities.size(), geometries.Size(), controllers.size(), static_cast<int>(ms));
    }

    void EntityAssets::LoadDirectory(const std::string& dir, bool overlay) {
        (void)overlay;
        std::string text;
        size_t before = entities.size();
        for (const fs::path& p : JsonFiles(fs::path(dir) / "models" / "entity"))
            if (file::Read(p, text))
                geometries.Load(text);
        if (file::Read(fs::path(dir) / "models" / "mobs.json", text))
            geometries.Load(text);
        for (const fs::path& p : JsonFiles(fs::path(dir) / "models" / "blocks"))
            if (file::Read(p, text))
                geometries.Load(text);
        for (const fs::path& p : JsonFiles(fs::path(dir) / "animations"))
            if (file::Read(p, text))
                animations.LoadAnimations(text);
        for (const fs::path& p : JsonFiles(fs::path(dir) / "animation_controllers"))
            if (file::Read(p, text))
                animations.LoadControllers(text);
        for (const fs::path& p : JsonFiles(fs::path(dir) / "render_controllers")) {
            Json doc;
            std::string error;
            if (file::Read(p, text) && Json::Parse(text, doc, error))
                ParseRenderControllers(doc, controllers);
        }
        size_t failed = 0;
        for (const fs::path& p : JsonFiles(fs::path(dir) / "entity")) {
            Json doc;
            std::string error;
            if (!file::Read(p, text))
                continue;
            if (Json::Parse(text, doc, error))
                ParseEntity(doc, entities);
            else if (failed++ < 3)
                Log("entity", "cannot parse {}: {}", p.string(), error);
        }
        for (auto& [id, def] : entities)
            if (def.renderControllers.empty() && !def.geometries.empty())
                def.renderControllers.push_back({"controller.render.perlin_implicit_default", nullptr});
        for (const auto& [id, def] : entities)
            for (const auto& ref : def.renderControllers)
                if (!controllers.contains(ref.id)) {
                    RenderControllerDef rc;
                    rc.geometry = Expr::Parse("Geometry.default");
                    rc.textures.push_back(Expr::Parse("Texture.default"));
                    controllers[ref.id] = std::move(rc);
                }
        Log("entity", "server pack {}: {} new entity definitions, {} geometries total", dir,
            (entities.size() - before), geometries.Size());
    }

    const ClientEntityDef* EntityAssets::Entity(const std::string& identifier) const {
        const auto it = entities.find(identifier);
        return it == entities.end() ? nullptr : &it->second;
    }

    bool EntityAssets::Seat(const std::string& identifier, float& x, float& y, float& z) const {
        const auto it = seats.find(identifier);
        if (it == seats.end())
            return false;
        x = it->second[0];
        y = it->second[1];
        z = it->second[2];
        return true;
    }

    bool EntityAssets::CollisionBox(const std::string& identifier, float& width, float& height) const {
        const auto it = collisionBoxes.find(identifier);
        if (it == collisionBoxes.end())
            return false;
        width = it->second.first;
        height = it->second.second;
        return true;
    }

    const RenderControllerDef* EntityAssets::RenderController(const std::string& id) const {
        const auto it = controllers.find(id);
        return it == controllers.end() ? nullptr : &it->second;
    }
}
