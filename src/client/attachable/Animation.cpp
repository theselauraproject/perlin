#include <cstdlib>
#include "util/Env.hpp"
#include "client/attachable/Animation.hpp"
#include "client/attachable/Geometry.hpp"
#include <cctype>

#include <algorithm>
#include <cmath>

namespace perlin::attachable {
    namespace {
        using molang::Expr;

        std::shared_ptr<Expr> ParseValue(const Json& v) {
            if (v.IsNumber())
                return Expr::Constant(v.number);
            if (v.IsString())
                return Expr::Parse(v.string);
            if (v.IsBool())
                return Expr::Constant(v.boolean ? 1.0 : 0.0);
            return Expr::Constant(0);
        }

        bool ParseVec3(const Json& v, Vec3Expr& out) {
            if (v.IsArray() && v.array.size() >= 3) {
                for (int i = 0; i < 3; i++)
                    out.e[i] = ParseValue(v.array[static_cast<size_t>(i)]);
                out.present = true;
                return true;
            }
            if (v.IsNumber() || v.IsString() || (v.IsArray() && v.array.size() == 1)) {
                auto e = ParseValue(v.IsArray() ? v.array[0] : v);
                out.e[0] = out.e[1] = out.e[2] = e;
                out.present = true;
                return true;
            }
            return false;
        }

        void ParseCues(const Json& v, std::vector<ParticleCue>& out) {
            const auto one = [&](const Json& o) {
                if (!o.IsObject())
                    return;
                ParticleCue cue;
                if (const Json* e = o.Find("effect"))
                    cue.effect = e->StringOr("");
                if (const Json* l = o.Find("locator"))
                    cue.locator = l->StringOr("");
                if (const Json* s = o.Find("pre_effect_script"); s && s->IsString())
                    cue.preScript = s->string;
                if (!cue.effect.empty())
                    out.push_back(std::move(cue));
            };
            if (v.IsArray())
                for (const Json& item : v.array)
                    one(item);
            else
                one(v);
        }

        void ParseChannel(const Json& v, Channel& out) {
            if (ParseVec3(v, out.constant)) {
                out.present = true;
                return;
            }
            if (!v.IsObject())
                return;
            for (const auto& [key, frame] : v.object) {
                Keyframe k;
                k.time = static_cast<float>(std::atof(key.c_str()));
                if (frame.IsObject()) {
                    if (const Json* mode = frame.Find("lerp_mode"))
                        k.catmullRom = mode->StringOr("") == "catmullrom";
                    if (const Json* pre = frame.Find("pre"))
                        ParseVec3(*pre, k.pre);
                    if (const Json* post = frame.Find("post"))
                        ParseVec3(*post, k.post);
                    if (!k.pre.present)
                        k.pre = k.post;
                    if (!k.post.present)
                        k.post = k.pre;
                } else {
                    ParseVec3(frame, k.post);
                    k.pre = k.post;
                }
                if (k.post.present)
                    out.keys.push_back(std::move(k));
            }
            std::sort(out.keys.begin(), out.keys.end(),
                      [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
            out.present = !out.keys.empty();
        }
    }

    glm::vec3 Vec3Expr::Eval(molang::Scope& scope) const {
        if (!present)
            return glm::vec3(0.0f);
        glm::vec3 out;
        for (int i = 0; i < 3; i++) {
            scope.thisValue = scope.thisVec[i];
            out[i] = static_cast<float>(e[i]->Number(scope));
        }
        return out;
    }

    glm::vec3 Channel::Eval(molang::Scope& scope, float time) const {
        struct RestoreLerp {
            double& value;
            double previous;
            ~RestoreLerp() { value = previous; }
        } restore{scope.keyFrameLerpTime, scope.keyFrameLerpTime};
        scope.keyFrameLerpTime = 0;
        if (keys.empty())
            return constant.Eval(scope);
        if (time < keys.front().time)
            return keys.front().pre.Eval(scope);
        if (time >= keys.back().time) {
            scope.keyFrameLerpTime = 1;
            return keys.back().post.Eval(scope);
        }
        size_t i = 0;
        while (i + 1 < keys.size() && keys[i + 1].time <= time)
            i++;
        const Keyframe& a = keys[i];
        const Keyframe& b = keys[i + 1];
        const float span = b.time - a.time;
        const float t = span > 0 ? (time - a.time) / span : 0.0f;
        scope.keyFrameLerpTime = t;
        const glm::vec3 p1 = a.post.Eval(scope), p2 = b.pre.Eval(scope);
        if (!a.catmullRom)
            return glm::mix(p1, p2, t);
        const glm::vec3 p0 = i > 0 ? keys[i - 1].post.Eval(scope) : p1;
        const glm::vec3 p3 = i + 2 < keys.size() ? keys[i + 2].pre.Eval(scope) : p2;
        return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
                       (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
    }

    void AnimationLibrary::LoadAnimations(const std::string& text) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error) || !doc.IsObject())
            return;
        const Json* list = doc.Find("animations");
        if (!list || !list->IsObject())
            return;
        for (const auto& [id, def] : list->object) {
            if (!def.IsObject())
                continue;
            Animation a;
            if (const Json* loop = def.Find("loop")) {
                if (loop->IsBool() && loop->boolean)
                    a.loop = Animation::Loop::Yes;
                else if (loop->IsString() && loop->string == "hold_on_last_frame")
                    a.loop = Animation::Loop::HoldOnLastFrame;
            }
            if (const Json* len = def.Find("animation_length"))
                a.length = static_cast<float>(len->NumberOr(0));
            a.overridePrevious = def.Find("override_previous_animation") &&
                                 def.Find("override_previous_animation")->BoolOr(false);
            if (const Json* u = def.Find("anim_time_update"))
                a.animTimeUpdate = ParseValue(*u);
            if (const Json* w = def.Find("blend_weight"))
                a.blendWeight = ParseValue(*w);
            if (const Json* bones = def.Find("bones"); bones && bones->IsObject()) {
                for (const auto& [name, anim] : bones->object) {
                    BoneAnimation b;
                    if (const Json* r = anim.Find("rotation"))
                        ParseChannel(*r, b.rotation);
                    if (const Json* p = anim.Find("position"))
                        ParseChannel(*p, b.position);
                    if (const Json* s = anim.Find("scale"))
                        ParseChannel(*s, b.scale);
                    std::string lower = name;
                    for (char& c : lower)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    a.bones[lower] = std::move(b);
                    for (const Channel* c :
                         {&a.bones[lower].rotation, &a.bones[lower].position, &a.bones[lower].scale})
                        if (!c->keys.empty())
                            a.length = std::max(a.length, c->keys.back().time);
                }
            }
            if (const Json* fx = def.Find("particle_effects"); fx && fx->IsObject()) {
                for (const auto& [key, cues] : fx->object) {
                    std::vector<ParticleCue> list;
                    ParseCues(cues, list);
                    const float time = static_cast<float>(std::atof(key.c_str()));
                    for (ParticleCue& cue : list)
                        a.particles.emplace_back(time, std::move(cue));
                }
                std::sort(a.particles.begin(), a.particles.end(),
                          [](const auto& x, const auto& y) { return x.first < y.first; });
            }
            if (!def.Find("animation_length")) {
                for (const auto& [bone, channels] : a.bones) {
                    (void)bone;
                    for (const Channel* channel : {&channels.position, &channels.rotation, &channels.scale})
                        if (!channel->keys.empty())
                            a.length = std::max(a.length, channel->keys.back().time);
                }
                if (!a.particles.empty())
                    a.length = std::max(a.length, a.particles.back().first);
            }
            animations[id] = std::move(a);
        }
    }

    const Animation* AnimationLibrary::Find(const std::string& id) const {
        const auto it = animations.find(id);
        return it == animations.end() ? nullptr : &it->second;
    }

    void AnimationLibrary::LoadControllers(const std::string& text) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error) || !doc.IsObject())
            return;
        const Json* list = doc.Find("animation_controllers");
        if (!list || !list->IsObject())
            return;
        for (const auto& [id, def] : list->object) {
            Controller c;
            if (const Json* init = def.Find("initial_state"))
                c.initial = init->StringOr("default");
            if (const Json* states = def.Find("states"); states && states->IsObject()) {
                for (const auto& [name, st] : states->object) {
                    State s;
                    const auto readList =
                        [&](const char* key,
                            std::vector<std::pair<std::string, std::shared_ptr<Expr>>>& dest) {
                            const Json* arr = st.Find(key);
                            if (!arr || !arr->IsArray())
                                return;
                            for (const Json& item : arr->array) {
                                if (item.IsString())
                                    dest.emplace_back(item.string, nullptr);
                                else if (item.IsObject())
                                    for (const auto& [target, cond] : item.object)
                                        dest.emplace_back(target, ParseValue(cond));
                            }
                        };
                    readList("animations", s.animations);
                    for (auto& entry : s.animations)
                        for (char& ch : entry.first)
                            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    readList("transitions", s.transitions);
                    for (const char* key : {"on_entry", "on_exit"}) {
                        const Json* arr = st.Find(key);
                        if (!arr || !arr->IsArray())
                            continue;
                        for (const Json& item : arr->array)
                            if (item.IsString())
                                (std::string(key) == "on_entry" ? s.onEntry : s.onExit)
                                    .push_back(Expr::Parse(item.string));
                    }
                    if (const Json* fx = st.Find("particle_effects"))
                        ParseCues(*fx, s.particles);
                    c.states[name] = std::move(s);
                }
            }
            controllers[id] = std::move(c);
        }
    }

    const AnimationLibrary::Controller* AnimationLibrary::FindController(const std::string& id) const {
        const auto it = controllers.find(id);
        return it == controllers.end() ? nullptr : &it->second;
    }

    void StartController(const AnimationLibrary::Controller& controller, molang::Scope& scope,
                         std::string& state, double& entered, bool& started, double now) {
        if (started)
            return;
        started = true;
        state = controller.initial;
        entered = now;
        if (const auto initial = controller.states.find(state); initial != controller.states.end())
            for (const auto& script : initial->second.onEntry)
                script->Eval(scope);
    }

    bool AdvanceController(const AnimationLibrary::Controller& controller, molang::Scope& scope,
                           std::string& state, double& entered, double now) {
        const auto current = controller.states.find(state);
        if (current == controller.states.end())
            return false;

        for (const auto& [target, condition] : current->second.transitions) {
            if (!condition || !condition->Eval(scope).Truthy())
                continue;
            const auto next = controller.states.find(target);
            if (next == controller.states.end())
                continue;
            for (const auto& script : current->second.onExit)
                script->Eval(scope);
            state = target;
            entered = now;
            for (const auto& script : next->second.onEntry)
                script->Eval(scope);
            return true;
        }
        return false;
    }

    void Apply(const Animation& a, molang::Scope& scope, float time, float weight, Pose& pose,
               const BindLookup& bindRotation, const BindLookup& bindTranslation) {
        if (a.blendWeight)
            weight *= static_cast<float>(a.blendWeight->Number(scope));
        if (a.overridePrevious)
            for (const auto& entry : a.bones)
                pose.erase(entry.first);
        for (const auto& [bone, anim] : a.bones) {
            BoneXform& x = pose[bone];
            const auto setThis = [&](const glm::vec3& v) {
                scope.thisVec[0] = v.x;
                scope.thisVec[1] = v.y;
                scope.thisVec[2] = v.z;
            };
            if (anim.rotation.present) {
                setThis(bindRotation ? x.rotation + bindRotation(bone) : x.rotation);
                glm::vec3 r = anim.rotation.Eval(scope, time);
                static const float armZ = PERLIN_ENV("PERLIN_ARMZ")
                                              ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_ARMZ")))
                                              : 1.0f;
                if (bone == "rightarm" || bone == "leftarm")
                    r.z *= gFirstPersonPose ? 1.0f : armZ;
                x.rotation += r * weight;
            }
            if (anim.position.present) {
                setThis(bindTranslation ? x.position + bindTranslation(bone) : x.position);
                x.position += anim.position.Eval(scope, time) * weight;
            }
            if (anim.scale.present) {
                setThis(x.scale);
                x.scale *= glm::mix(glm::vec3(1.0f), anim.scale.Eval(scope, time), weight);
            }
        }
    }

    BindLookup RestRotationLookup(const Geometry* g) {
        if (!g)
            return nullptr;
        return [g](const std::string& bone) {
            for (const Bone& b : g->bones)
                if (b.name == bone)
                    return b.rotation;
            return glm::vec3(0.0f);
        };
    }

    BindLookup DefaultTranslationLookup(const Geometry* g) {
        if (!g)
            return nullptr;
        return [g](const std::string& bone) {
            for (const Bone& b : g->bones)
                if (b.name == bone) {
                    const glm::vec3 origin = (b.parent >= 0 && b.parent < static_cast<int>(g->bones.size()))
                                                 ? g->bones[static_cast<size_t>(b.parent)].pivot
                                                 : glm::vec3(0.0f, 24.0f, 0.0f);
                    return b.pivot - origin;
                }
            return glm::vec3(0.0f);
        };
    }
}
