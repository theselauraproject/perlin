#include "client/attachable/Attachable.hpp"
#include "client/resources/ServerPacks.hpp"
#include "client/ui/UiAssets.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace perlin::attachable {
    namespace {
        using molang::Expr;
        using molang::Value;

        std::string Shorten(const std::string& id) {
            const size_t c = id.find(':');
            return c == std::string::npos ? id : id.substr(c + 1);
        }

        std::string AfterDot(const std::string& ref) {
            const size_t d = ref.find('.');
            return d == std::string::npos ? ref : ref.substr(d + 1);
        }
    }

    struct Host final : molang::Host {
        const ItemState* state = nullptr;
        double animTime = 0;
        bool allFinished = false, anyFinished = false;

        bool Query(const std::string& name, const molang::Args& args, Value& out) override {
            if (!state)
                return false;
            const ItemState& s = *state;
            const auto num = [&](double v) {
                out = Value::Num(v);
                return true;
            };
            if (name == "frame_alpha")
                return num(s.partial);
            if (name == "main_hand_item_use_duration")
                return num(s.using_ ? std::max(0.0f, s.maxUseTicks - s.usedTicks) : 0.0f);
            if (name == "main_hand_item_max_duration")
                return num(s.maxUseTicks);
            if (name == "get_animation_frame")
                return num(s.animationFrame);
            if (name == "life_time")
                return num(s.lifeSeconds);
            if (name == "anim_time")
                return num(animTime);
            if (name == "all_animations_finished")
                return num(allFinished);
            if (name == "any_animation_finished")
                return num(anyFinished);
            if (name == "blocking")
                return num(s.blocking ? 1.0 : 0.0);
            if (name == "can_damage_nearby_mobs" || name == "has_target" || name == "is_moving")
                return num(0);
            if (name == "get_equipped_item_name") {
                out = Value::Str(Shorten(s.mainHandItem));
                return true;
            }
            if (name == "item_slot_to_bone_name") {
                out = Value::Str(s.mainHand ? "rightitem" : "leftitem");
                return true;
            }
            if (name == "is_item_name_any") {
                if (args.empty())
                    return num(0);
                const std::string slot = args[0].s;
                const std::string& item = slot.contains("offhand") ? s.offHandItem : s.mainHandItem;
                for (size_t i = 1; i < args.size(); i++)
                    if (args[i].isString && (args[i].s == item || args[i].s == Shorten(item)))
                        return num(1);
                return num(0);
            }
            return false;
        }

        bool Context(const std::string& name, Value& out) override {
            if (!state)
                return false;
            if (name == "is_first_person") {
                out = Value::Num(state->firstPerson ? 1.0 : 0.0);
                return true;
            }
            if (name == "item_slot") {
                out = Value::Str(state->mainHand ? "main_hand" : "off_hand");
                return true;
            }
            if (name == "owning_entity")
                return false;
            return false;
        }
    };

    struct Attachables::Instance {
        molang::Scope scope;
        Host host;
        bool initialised = false;
        std::map<std::string, double> animStart;
        struct ControllerState {
            std::string state;
            double entered = 0;
            bool started = false;
        };
        std::map<std::string, ControllerState> controllerStates;
        double now = 0;
    };

    Attachables::Attachables() = default;
    Attachables::~Attachables() = default;

    void Attachables::Configure(const std::string& vanillaRoot) {
        if (root == vanillaRoot)
            return;
        root = vanillaRoot;
        Clear();
    }

    bool Attachables::ArmorInfo(const std::string& identifier, std::string& geometry, std::string& texture) {
        Entry* e = Load(identifier);
        if (!e)
            return false;
        const auto g = e->geometries.find("default");
        const auto t = e->textures.find("default");
        if (g == e->geometries.end() || t == e->textures.end())
            return false;
        geometry = g->second;
        texture = t->second;
        return true;
    }

    void Attachables::Clear() {
        entries.clear();
        animations = AnimationLibrary();
        geometries = GeometryLibrary();
        controllers.clear();
        loadedFiles.clear();
        instances.clear();
        textureCache.clear();
    }

    bool Attachables::ReadFile(const std::string& rel, std::string& out) const {
        if (root.empty())
            return false;
        std::ifstream f(root + "/" + rel, std::ios::binary);
        if (!f)
            return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    void Attachables::LoadRenderControllers(const std::string& text) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error))
            return;
        const Json* list = doc.Find("render_controllers");
        if (!list || !list->IsObject())
            return;
        for (const auto& [id, def] : list->object) {
            RenderController rc;
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
                                    rc.arrays[name].push_back(Value::Str(v.StringOr("")));
            controllers[id] = std::move(rc);
        }
    }

    void Attachables::RefreshServerIndex() {
        const std::vector<std::string> dirs = ServerPacks::Instance().Directories();
        std::string key;
        for (const std::string& d : dirs)
            key += d + "|";
        if (key == serverKey)
            return;
        serverKey = key;
        serverFiles.clear();
        entries.clear();
        namespace fs = std::filesystem;
        for (const std::string& dir : dirs) {
            std::error_code ec;
            for (fs::recursive_directory_iterator it(fs::path(dir) / "attachables", ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".json")
                    continue;
                std::ifstream f(it->path(), std::ios::binary);
                std::stringstream ss;
                ss << f.rdbuf();
                Json doc;
                std::string error;
                if (!Json::Parse(ss.str(), doc, error))
                    continue;
                const Json* att = doc.Find("minecraft:attachable");
                const Json* desc = att ? att->Find("description") : nullptr;
                const Json* id = desc ? desc->Find("identifier") : nullptr;
                if (id && id->IsString())
                    serverFiles.try_emplace(id->string, it->path().string());
            }
        }
    }

    Attachables::Entry* Attachables::Load(const std::string& identifier) {
        RefreshServerIndex();
        auto [it, inserted] = entries.try_emplace(identifier);
        Entry& e = it->second;
        if (!inserted)
            return e.ok ? &e : nullptr;
        e.loaded = true;
        const std::string name = Shorten(identifier);
        std::string text;
        if (const auto server = serverFiles.find(identifier); server != serverFiles.end()) {
            std::ifstream f(server->second, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            text = ss.str();
        }
        if (text.empty() && !ReadFile("attachables/" + name + ".json", text) &&
            !ReadFile("attachables/" + name + ".entity.json", text))
            return nullptr;
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error))
            return nullptr;
        const Json* att = doc.Find("minecraft:attachable");
        const Json* desc = att ? att->Find("description") : nullptr;
        if (!desc)
            return nullptr;
        e.identifier = identifier;
        e.name = name;
        const auto readMap = [&](const char* key, std::map<std::string, std::string>& dest) {
            if (const Json* m = desc->Find(key); m && m->IsObject())
                for (const auto& [k, v] : m->object)
                    if (v.IsString())
                        dest[k] = v.string;
        };
        readMap("textures", e.textures);
        readMap("geometry", e.geometries);
        readMap("animations", e.animations);
        if (const Json* scripts = desc->Find("scripts")) {
            const auto readScripts = [&](const char* key, std::vector<std::shared_ptr<Expr>>& dest) {
                if (const Json* l = scripts->Find(key); l && l->IsArray())
                    for (const Json& s : l->array)
                        if (s.IsString())
                            dest.push_back(Expr::Parse(s.string));
            };
            readScripts("initialize", e.initialize);
            readScripts("pre_animation", e.preAnimation);
            if (const Json* l = scripts->Find("animate"); l && l->IsArray())
                for (const Json& a : l->array) {
                    if (a.IsString())
                        e.animate.emplace_back(a.string, nullptr);
                    else if (a.IsObject())
                        for (const auto& [target, cond] : a.object)
                            e.animate.emplace_back(
                                target, cond.IsString() ? Expr::Parse(cond.string)
                                                        : Expr::Constant(cond.IsNumber() ? cond.number : 1));
                }
        }
        if (const Json* rcs = desc->Find("render_controllers"); rcs && rcs->IsArray())
            for (const Json& r : rcs->array) {
                if (r.IsString())
                    e.renderControllers.push_back(r.string);
                else if (r.IsObject())
                    for (const auto& [id, cond] : r.object) {
                        (void)cond;
                        e.renderControllers.push_back(id);
                    }
            }
        LoadSupport(e);
        e.ok = true;
        return &e;
    }

    void Attachables::LoadSupport(Entry& e) {
        const auto once = [&](const std::string& rel, const auto& apply) {
            if (loadedFiles.contains(rel))
                return;
            loadedFiles[rel] = true;
            std::string text;
            if (ReadFile(rel, text))
                apply(text);
        };
        for (const auto& [shortName, id] : e.animations) {
            (void)shortName;
            std::vector<std::string> parts;
            std::stringstream ss(id);
            for (std::string p; std::getline(ss, p, '.');)
                parts.push_back(p);
            if (id.starts_with("controller.animation.") && parts.size() > 2)
                once("animation_controllers/" + parts[2] + ".animation_controllers.json",
                     [&](const std::string& t) { animations.LoadControllers(t); });
            else if (id.starts_with("animation.") && parts.size() > 1)
                once("animations/" + parts[1] + ".animation.json",
                     [&](const std::string& t) { animations.LoadAnimations(t); });
        }
        for (const std::string& rc : e.renderControllers) {
            const std::string tail = rc.substr(rc.rfind('.') + 1);
            once("render_controllers/" + tail + ".render_controllers.json",
                 [&](const std::string& t) { LoadRenderControllers(t); });
        }
        for (const auto& [shortName, id] : e.geometries) {
            (void)shortName;
            std::string stem = id.substr(id.find('.') + 1);
            const size_t cut = stem.find_first_of("_.");
            if (cut != std::string::npos)
                stem.resize(cut);
            for (const std::string& candidate : {e.name, stem})
                once("models/entity/" + candidate + ".geo.json",
                     [&](const std::string& t) { geometries.Load(t); });
        }
    }

    bool Attachables::Has(const std::string& identifier) {
        return Load(identifier) != nullptr;
    }

    const Texture* Attachables::TextureFor(const std::string& path) {
        auto it = textureCache.find(path);
        if (it != textureCache.end())
            return it->second.get();
        auto tex = std::make_unique<Texture>();
        if (!ui::UiAssets::Instance().Image(path, *tex))
            tex.reset();
        return textureCache.emplace(path, std::move(tex)).first->second.get();
    }

    Frame Attachables::Evaluate(const ItemState& state, int slot) {
        Frame frame;
        Entry* e = Load(state.identifier);
        if (!e)
            return frame;
        const std::string key = state.identifier + "#" + std::to_string(slot);
        auto& inst = instances[key];
        if (!inst)
            inst = std::make_unique<Instance>();
        Instance& in = *inst;
        in.host.state = &state;
        in.scope.host = &in.host;
        in.now = state.lifeSeconds;
        in.scope.variables["is_enchanted"] = Value::Num(state.enchanted ? 1.0 : 0.0);
        in.scope.variables["is_patterned"] = Value::Num(0);
        if (!in.initialised) {
            in.initialised = true;
            for (const auto& s : e->initialize)
                s->Eval(in.scope);
        }
        for (const auto& s : e->preAnimation)
            s->Eval(in.scope);

        Pose pose;
        std::map<std::string, bool> active;
        const auto animationTime = [&](const std::string& id, const Animation& a, double since) {
            in.host.animTime = since;
            if (a.animTimeUpdate)
                since = a.animTimeUpdate->Number(in.scope);
            if (a.length > 0) {
                if (a.loop == Animation::Loop::Yes)
                    since = std::fmod(since, a.length);
                else
                    since = std::min(since, static_cast<double>(a.length));
            }
            (void)id;
            return static_cast<float>(since);
        };
        const auto play = [&](const std::string& id, double startedAt, float weight) {
            const Animation* a = animations.Find(id);
            if (!a)
                return;
            const double since = in.now - startedAt;
            in.host.animTime = since;
            Apply(*a, in.scope, animationTime(id, *a, since), weight, pose);
        };
        const auto activate = [&](const std::string& id) {
            active[id] = true;
            if (!in.animStart.contains(id))
                in.animStart[id] = in.now;
            return in.animStart[id];
        };

        for (const auto& [shortName, cond] : e->animate) {
            if (state.bindPose)
                break;
            const float rootWeight = cond ? static_cast<float>(cond->Number(in.scope)) : 1.0f;
            if (rootWeight == 0.0f)
                continue;
            const auto found = e->animations.find(shortName);
            if (found == e->animations.end())
                continue;
            const std::string& id = found->second;
            if (const auto* controller = animations.FindController(id)) {
                auto& cs = in.controllerStates[id];
                StartController(*controller, in.scope, cs.state, cs.entered, cs.started, in.now);
                in.host.animTime = in.now - cs.entered;
                in.host.allFinished = false;
                in.host.anyFinished = false;
                if (const auto current = controller->states.find(cs.state);
                    current != controller->states.end()) {
                    bool all = !current->second.animations.empty();
                    for (const auto& [name, blend] : current->second.animations) {
                        (void)blend;
                        const auto child = e->animations.find(name);
                        const auto* animation =
                            child != e->animations.end() ? animations.Find(child->second) : nullptr;
                        bool done = false;
                        if (animation) {
                            const std::string key = id + "/" + cs.state + "/" + child->second;
                            const auto start = in.animStart.find(key);
                            const double startedAt = start == in.animStart.end() ? cs.entered : start->second;
                            done = animation->loop != Animation::Loop::Yes && animation->length > 0 &&
                                   in.now - startedAt >= animation->length;
                        }
                        all = all && done;
                        in.host.anyFinished = in.host.anyFinished || done;
                    }
                    in.host.allFinished = all;
                }
                AdvanceController(*controller, in.scope, cs.state, cs.entered, in.now);
                const auto st = controller->states.find(cs.state);
                if (st == controller->states.end())
                    continue;
                for (const auto& [animShort, animCond] : st->second.animations) {
                    const float weight =
                        rootWeight * (animCond ? static_cast<float>(animCond->Number(in.scope)) : 1.0f);
                    if (weight == 0.0f)
                        continue;
                    const auto aid = e->animations.find(animShort);
                    if (aid == e->animations.end())
                        continue;
                    const std::string stateKey = id + "/" + cs.state + "/" + aid->second;
                    play(aid->second, activate(stateKey), weight);
                }
            } else {
                play(id, activate(id), rootWeight);
            }
        }
        for (auto it = in.animStart.begin(); it != in.animStart.end();)
            it = active.contains(it->first) ? std::next(it) : in.animStart.erase(it);

        std::string geometryRef = "geometry.default", textureRef = "texture.default";
        for (const std::string& rcId : e->renderControllers) {
            const auto rc = controllers.find(rcId);
            if (rc == controllers.end())
                continue;
            in.scope.arrays.clear();
            for (const auto& [name, values] : rc->second.arrays)
                in.scope.arrays[name] = values;
            if (rc->second.geometry) {
                const Value g = rc->second.geometry->Eval(in.scope);
                if (g.isString)
                    geometryRef = g.s;
            }
            if (!rc->second.textures.empty()) {
                const Value t = rc->second.textures.front()->Eval(in.scope);
                if (t.isString)
                    textureRef = t.s;
            }
            break;
        }
        const auto geoId = e->geometries.find(AfterDot(geometryRef));
        if (geoId == e->geometries.end())
            return frame;
        const Geometry* geometry = geometries.Find(geoId->second);
        if (!geometry)
            return frame;
        const auto texPath = e->textures.find(AfterDot(textureRef));
        const std::string mainTexture = texPath != e->textures.end() ? texPath->second : std::string();

        BuiltMesh mesh;
        BuildMesh(
            *geometry, pose,
            [&](const std::string& shortName) -> const Texture* {
                const auto p = e->textures.find(shortName);
                return p == e->textures.end() ? nullptr : TextureFor(p->second);
            },
            mesh);
        if (!mesh.body.empty() && !mainTexture.empty()) {
            Part body;
            body.texture = mainTexture;
            body.quads = std::move(mesh.body);
            frame.parts.push_back(std::move(body));
        }
        for (auto& extra : mesh.extras) {
            const auto p = e->textures.find(extra.texture);
            if (p == e->textures.end())
                continue;
            Part part;
            part.texture = p->second;
            part.quads = std::move(extra.quads);
            frame.parts.push_back(std::move(part));
        }
        frame.glint = state.enchanted;
        frame.valid = !frame.parts.empty();
        return frame;
    }
}
