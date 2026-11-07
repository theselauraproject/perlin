#include "client/particle/DataParticles.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "client/ui/Json.hpp"
#include "util/FileIo.hpp"

namespace perlin::particle {
    namespace fs = std::filesystem;
    using ui::Json;
    using ExprPtr = std::shared_ptr<molang::Expr>;

    namespace {
        constexpr float kDeg = 0.017453292519943295f;

        ExprPtr Ex(const Json* j) {
            if (!j)
                return nullptr;
            if (j->IsNumber())
                return molang::Expr::Constant(j->number);
            if (j->IsBool())
                return molang::Expr::Constant(j->boolean ? 1.0 : 0.0);
            if (j->IsString())
                return molang::Expr::Parse(j->string);
            return nullptr;
        }

        double Num(const ExprPtr& e, molang::Scope& scope, double fallback) {
            return e ? e->Number(scope) : fallback;
        }

        void Vec(const Json* j, ExprPtr* out, int n) {
            if (!j || !j->IsArray())
                return;
            for (int i = 0; i < n && i < static_cast<int>(j->array.size()); i++)
                out[i] = Ex(&j->array[static_cast<size_t>(i)]);
        }

        std::string StripVariable(const std::string& name) {
            for (const char* prefix : {"variable.", "v."})
                if (name.starts_with(prefix))
                    return name.substr(std::string(prefix).size());
            return name;
        }

        bool ParseHex(const std::string& text, glm::vec4& out) {
            std::string h = text;
            if (!h.empty() && h[0] == '#')
                h.erase(0, 1);
            if (h.size() != 6 && h.size() != 8)
                return false;
            try {
                const auto byte = [&](size_t i) {
                    return static_cast<float>(std::stoi(h.substr(i, 2), nullptr, 16)) / 255.0f;
                };
                if (h.size() == 8)
                    out = {byte(2), byte(4), byte(6), byte(0)};
                else
                    out = {byte(0), byte(2), byte(4), 1.0f};
            } catch (...) {
                return false;
            }
            return true;
        }

        uint32_t Pack(const glm::vec4& c) {
            const auto b = [](float v) {
                return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return (b(c.a) << 24) | (b(c.b) << 16) | (b(c.g) << 8) | b(c.r);
        }
    }

    double DataParticles::Rand() const {
        return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
    }

    void DataParticles::Configure(const std::vector<std::string>& dirs) {
        std::string k;
        for (const std::string& d : dirs) {
            k += d;
            k += '|';
            std::error_code ec;
            size_t count = 0;
            for (const auto& e : fs::directory_iterator(fs::path(d) / "particles", ec))
                if (e.is_regular_file())
                    count++;
            k += std::to_string(count);
            k += ';';
        }
        if (k == key)
            return;
        key = k;
        defs.clear();
        emitters.clear();
        particles.clear();
        for (const std::string& d : dirs)
            LoadDirectory(d);
    }

    void DataParticles::LoadDirectory(const std::string& dir) {
        std::error_code ec;
        const fs::path root = fs::path(dir) / "particles";
        if (!fs::exists(root, ec))
            return;
        std::vector<fs::path> files;
        for (const auto& e :
             fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
            if (e.is_regular_file() && e.path().extension() == ".json")
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files)
            LoadFile(f.string());
    }

    void DataParticles::LoadFile(const std::string& path) {
        std::string text;
        Json doc;
        std::string error;
        if (!file::Read(path, text) || !Json::Parse(text, doc, error))
            return;
        const Json* effect = doc.Find("particle_effect");
        if (!effect || !effect->IsObject())
            return;
        const Json* desc = effect->Find("description");
        const Json* idj = desc ? desc->Find("identifier") : nullptr;
        if (!idj || !idj->IsString())
            return;
        Def d;
        d.identifier = idj->string;
        if (const Json* brp = desc->Find("basic_render_parameters")) {
            if (const Json* m = brp->Find("material")) {
                const std::string material = m->StringOr("");
                d.blend = material.contains("add") ? 1 : (material.contains("blend") ? 2 : 0);
            }
            if (const Json* t = brp->Find("texture"))
                d.texture = t->StringOr(d.texture);
        }
        if (const Json* curves = effect->Find("curves"); curves && curves->IsObject())
            for (const auto& [name, c] : curves->object) {
                Curve curve;
                curve.name = StripVariable(name);
                if (const Json* t = c.Find("type"))
                    curve.type = t->StringOr("linear");
                curve.input = Ex(c.Find("input"));
                curve.range = Ex(c.Find("horizontal_range"));
                if (const Json* nodes = c.Find("nodes"); nodes && nodes->IsArray())
                    for (const Json& n : nodes->array)
                        curve.nodes.push_back(Ex(&n));
                if (curve.input && curve.nodes.size() >= 2)
                    d.curves.push_back(std::move(curve));
            }
        const Json* comps = effect->Find("components");
        if (!comps || !comps->IsObject())
            return;
        const auto comp = [&](const char* name) { return comps->Find(std::string("minecraft:") + name); };
        if (const Json* c = comp("emitter_rate_instant")) {
            d.rateKind = 0;
            d.instantCount = Ex(c->Find("num_particles"));
            if (!d.instantCount)
                d.instantCount = molang::Expr::Constant(10);
        }
        if (const Json* c = comp("emitter_rate_steady")) {
            d.rateKind = 1;
            d.steadyRate = Ex(c->Find("spawn_rate"));
            d.steadyMax = Ex(c->Find("max_particles"));
        }
        if (const Json* c = comp("emitter_rate_manual")) {
            d.rateKind = 2;
            d.steadyMax = Ex(c->Find("max_particles"));
        }
        if (const Json* c = comp("emitter_lifetime_once")) {
            d.lifeKind = 0;
            d.activeTime = Ex(c->Find("active_time"));
        }
        if (const Json* c = comp("emitter_lifetime_looping")) {
            d.lifeKind = 1;
            d.activeTime = Ex(c->Find("active_time"));
            d.sleepTime = Ex(c->Find("sleep_time"));
        }
        if (const Json* c = comp("emitter_lifetime_expression")) {
            d.lifeKind = 2;
            d.activation = Ex(c->Find("activation_expression"));
            d.expiration = Ex(c->Find("expiration_expression"));
        }
        if (const Json* c = comp("emitter_initialization")) {
            if (const Json* e = c->Find("creation_expression"))
                d.creation.push_back(Ex(e));
            if (const Json* e = c->Find("per_update_expression"))
                d.perUpdate.push_back(Ex(e));
        }
        if (const Json* c = comp("emitter_local_space")) {
            d.localPosition = c->Find("position") && c->Find("position")->BoolOr(false);
            d.localRotation = c->Find("rotation") && c->Find("rotation")->BoolOr(false);
            d.localVelocity = c->Find("velocity") && c->Find("velocity")->BoolOr(false);
        }
        const auto direction = [&](const Json* c) {
            if (const Json* dj = c->Find("direction")) {
                if (dj->IsString()) {
                    d.dirMode = dj->string == "inwards" ? 1 : 0;
                } else if (dj->IsArray()) {
                    d.dirMode = 2;
                    Vec(dj, d.dir, 3);
                }
            }
        };
        if (const Json* c = comp("emitter_shape_point")) {
            d.shape = 0;
            Vec(c->Find("offset"), d.offset, 3);
            direction(c);
        } else if (const Json* c = comp("emitter_shape_sphere")) {
            d.shape = 1;
            Vec(c->Find("offset"), d.offset, 3);
            d.radius = Ex(c->Find("radius"));
            direction(c);
        } else if (const Json* c = comp("emitter_shape_box")) {
            d.shape = 2;
            Vec(c->Find("offset"), d.offset, 3);
            Vec(c->Find("half_dimensions"), d.halfDim, 3);
            direction(c);
        } else if (const Json* c = comp("emitter_shape_disc")) {
            d.shape = 3;
            Vec(c->Find("offset"), d.offset, 3);
            Vec(c->Find("plane_normal"), d.normal, 3);
            d.radius = Ex(c->Find("radius"));
            direction(c);
        } else if (const Json* c = comp("emitter_shape_custom")) {
            d.shape = 4;
            Vec(c->Find("offset"), d.offset, 3);
            direction(c);
        } else if (const Json* c = comp("emitter_shape_entity_aabb")) {
            d.shape = 0;
            direction(c);
        }
        if (const Json* c = comp("particle_initial_spin")) {
            d.spin = Ex(c->Find("rotation"));
            d.spinRate = Ex(c->Find("rotation_rate"));
        }
        if (const Json* c = comp("particle_initial_speed")) {
            if (c->IsArray())
                Vec(c, d.speed, 3);
            else
                d.speed[0] = Ex(c);
        }
        if (const Json* c = comp("particle_lifetime_expression")) {
            d.particleLife = Ex(c->Find("max_lifetime"));
            d.particleExpire = Ex(c->Find("expiration_expression"));
        }
        if (const Json* c = comp("particle_motion_dynamic")) {
            d.dynamic = true;
            Vec(c->Find("linear_acceleration"), d.accel, 3);
            d.drag = Ex(c->Find("linear_drag_coefficient"));
            d.rotAccel = Ex(c->Find("rotation_acceleration"));
            d.rotDrag = Ex(c->Find("rotation_drag_coefficient"));
        }
        if (const Json* c = comp("particle_motion_parametric")) {
            d.parametric = true;
            Vec(c->Find("relative_position"), d.relPos, 3);
        }
        if (const Json* c = comp("particle_motion_collision")) {
            const Json* en = c->Find("enabled");
            d.collide = !en || (en->IsBool() ? en->boolean : en->IsString() ? true : en->BoolOr(true));
            d.collideRadius = Ex(c->Find("collision_radius"));
            if (const Json* r = c->Find("coefficient_of_restitution"))
                d.restitution = static_cast<float>(r->NumberOr(0));
            if (const Json* r = c->Find("collision_drag"))
                d.collideDrag = static_cast<float>(r->NumberOr(0));
            if (const Json* r = c->Find("expire_on_contact"))
                d.expireOnContact = r->BoolOr(false);
        }
        if (const Json* c = comp("particle_appearance_billboard")) {
            Vec(c->Find("size"), d.size, 2);
            if (const Json* f = c->Find("facing_camera_mode"))
                d.facing = f->StringOr(d.facing);
            if (const Json* dj = c->Find("direction")) {
                if (const Json* m = dj->Find("mode"))
                    d.dirFacing =
                        m->StringOr("") == "custom" ? 2 : (m->StringOr("") == "derive_from_velocity" ? 1 : 0);
                if (const Json* m = dj->Find("min_speed_threshold"))
                    d.minSpeed = static_cast<float>(m->NumberOr(0.01));
            }
            if (const Json* uv = c->Find("uv")) {
                if (const Json* w = uv->Find("texture_width"))
                    d.texW = static_cast<float>(w->NumberOr(16));
                if (const Json* h = uv->Find("texture_height"))
                    d.texH = static_cast<float>(h->NumberOr(16));
                Vec(uv->Find("uv"), d.uv, 2);
                Vec(uv->Find("uv_size"), d.uvSize, 2);
                if (const Json* fb = uv->Find("flipbook")) {
                    d.flipbook = true;
                    Vec(fb->Find("base_UV"), d.baseUv, 2);
                    Vec(fb->Find("size_UV"), d.sizeUv, 2);
                    if (const Json* st = fb->Find("step_UV"); st && st->IsArray() && st->array.size() >= 2) {
                        d.stepUv[0] = static_cast<float>(st->array[0].NumberOr(0));
                        d.stepUv[1] = static_cast<float>(st->array[1].NumberOr(0));
                    }
                    d.fps = Ex(fb->Find("frames_per_second"));
                    d.maxFrame = Ex(fb->Find("max_frame"));
                    if (const Json* s = fb->Find("stretch_to_lifetime"))
                        d.stretch = s->BoolOr(false);
                    if (const Json* s = fb->Find("loop"))
                        d.loop = s->BoolOr(false);
                }
            }
        }
        if (const Json* c = comp("particle_appearance_tinting")) {
            if (const Json* col = c->Find("color")) {
                if (col->IsArray()) {
                    Vec(col, d.color, 4);
                    d.hasColor = true;
                } else if (col->IsObject()) {
                    d.interpolant = Ex(col->Find("interpolant"));
                    if (const Json* g = col->Find("gradient")) {
                        if (g->IsObject()) {
                            for (const auto& [stop, value] : g->object) {
                                glm::vec4 rgba;
                                if (value.IsString() && ParseHex(value.string, rgba)) {
                                    try {
                                        d.gradient.stops.emplace_back(std::stof(stop), rgba);
                                    } catch (...) {}
                                }
                            }
                        } else if (g->IsArray()) {
                            for (size_t i = 0; i < g->array.size(); i++) {
                                glm::vec4 rgba;
                                if (g->array[i].IsString() && ParseHex(g->array[i].string, rgba))
                                    d.gradient.stops.emplace_back(
                                        g->array.size() > 1
                                            ? static_cast<float>(i) / static_cast<float>(g->array.size() - 1)
                                            : 0.0f,
                                        rgba);
                            }
                        }
                        std::sort(d.gradient.stops.begin(), d.gradient.stops.end(),
                                  [](const auto& a, const auto& b) { return a.first < b.first; });
                        d.hasGradient = !d.gradient.stops.empty();
                    }
                }
            }
        }
        d.valid = true;
        defs[d.identifier] = std::move(d);
    }

    bool DataParticles::Has(const std::string& identifier) const {
        return defs.find(identifier) != defs.end();
    }

    size_t DataParticles::Count() const {
        return particles.size();
    }

    void DataParticles::Clear() {
        emitters.clear();
        particles.clear();
    }

    void DataParticles::Prepare(molang::Scope& scope, const Emitter& e, const Particle* p) const {
        scope.variables = e.variables;
        scope.variables["emitter_age"] = molang::Value::Num(e.age);
        scope.variables["emitter_lifetime"] = molang::Value::Num(e.lifetime);
        for (int i = 0; i < 4; i++)
            scope.variables["emitter_random_" + std::to_string(i + 1)] = molang::Value::Num(e.random[i]);
        if (p) {
            scope.variables["particle_age"] = molang::Value::Num(p->age);
            scope.variables["particle_lifetime"] = molang::Value::Num(p->lifetime);
            for (int i = 0; i < 4; i++)
                scope.variables["particle_random_" + std::to_string(i + 1)] =
                    molang::Value::Num(p->random[i]);
        }
        for (const Curve& c : e.def->curves) {
            const double range = c.range ? c.range->Number(scope) : 1.0;
            const double t = range != 0.0 ? c.input->Number(scope) / range : 0.0;
            const int n = static_cast<int>(c.nodes.size());
            double value = 0.0;
            if (c.type == "catmull_rom" && n >= 4) {
                const double span = std::clamp(t, 0.0, 1.0) * (n - 3);
                const int seg = std::min(static_cast<int>(span), n - 4);
                const double f = span - seg;
                const double p0 = c.nodes[static_cast<size_t>(seg)]->Number(scope),
                             p1 = c.nodes[static_cast<size_t>(seg + 1)]->Number(scope),
                             p2 = c.nodes[static_cast<size_t>(seg + 2)]->Number(scope),
                             p3 = c.nodes[static_cast<size_t>(seg + 3)]->Number(scope);
                value = 0.5 * ((2 * p1) + (-p0 + p2) * f + (2 * p0 - 5 * p1 + 4 * p2 - p3) * f * f +
                               (-p0 + 3 * p1 - 3 * p2 + p3) * f * f * f);
            } else {
                const double span = std::clamp(t, 0.0, 1.0) * (n - 1);
                const int seg = std::min(static_cast<int>(span), n - 2);
                const double f = span - seg;
                const double a = c.nodes[static_cast<size_t>(seg)]->Number(scope),
                             b = c.nodes[static_cast<size_t>(seg + 1)]->Number(scope);
                value = a + (b - a) * f;
            }
            scope.variables[c.name] = molang::Value::Num(value);
        }
    }

    bool DataParticles::Spawn(const std::string& identifier, const glm::dvec3& at,
                              const std::string& molangVariables, const std::string& preScript) {
        const auto it = defs.find(identifier);
        if (it == defs.end())
            return false;
        if (emitters.size() > 256)
            emitters.erase(emitters.begin());
        Live live;
        live.id = nextEmitter++;
        live.e.def = &it->second;
        live.e.origin = at;
        for (float& i : live.e.random)
            i = static_cast<float>(Rand());
        if (!molangVariables.empty()) {
            Json vars;
            std::string error;
            if (Json::Parse(molangVariables, vars, error)) {
                const auto take = [&](const std::string& name, const Json& value) {
                    if (value.IsNumber())
                        live.e.variables[StripVariable(name)] = molang::Value::Num(value.number);
                    else if (const Json* v = value.Find("value"); v && v->IsNumber())
                        live.e.variables[StripVariable(name)] = molang::Value::Num(v->number);
                };
                if (vars.IsObject())
                    for (const auto& [name, value] : vars.object)
                        take(name, value);
                else if (vars.IsArray())
                    for (const Json& item : vars.array) {
                        const Json* n = item.Find("name");
                        const Json* v = item.Find("value");
                        if (n && v && n->IsString())
                            take(n->string, *v);
                    }
            }
        }
        molang::Scope scope;
        Prepare(scope, live.e, nullptr);
        if (!preScript.empty()) {
            auto& parsed = preScripts[preScript];
            if (!parsed)
                parsed = molang::Expr::Parse(preScript);
            if (parsed)
                parsed->Eval(scope);
        }
        const Def& d = it->second;
        for (const ExprPtr& c : d.creation)
            if (c)
                c->Eval(scope);
        for (const auto& [name, value] : scope.variables)
            live.e.variables[name] = value;
        live.e.lifetime = d.activeTime ? d.activeTime->Number(scope) : 10.0;
        emitters.push_back(std::move(live));
        return true;
    }

    void DataParticles::SpawnOne(Live& live) {
        Emitter& e = live.e;
        const Def& d = *e.def;
        if (particles.size() >= 4096)
            return;
        Particle p;
        p.def = &d;
        p.emitterId = live.id;
        p.origin = e.origin;
        for (float& i : p.random)
            i = static_cast<float>(Rand());
        molang::Scope scope;
        Prepare(scope, e, &p);
        glm::dvec3 offset(Num(d.offset[0], scope, 0), Num(d.offset[1], scope, 0), Num(d.offset[2], scope, 0));
        glm::dvec3 local(0.0);
        const auto unit = [&]() {
            glm::dvec3 v(Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1);
            for (int i = 0; i < 8 && glm::dot(v, v) > 1.0; i++)
                v = glm::dvec3(Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1);
            const double len = glm::length(v);
            return len > 1e-6 ? v / len : glm::dvec3(0.0, 1.0, 0.0);
        };
        if (d.shape == 1) {
            const double r = Num(d.radius, scope, 1.0);
            local = unit() * r * std::cbrt(Rand());
        } else if (d.shape == 2) {
            local = glm::dvec3((Rand() * 2 - 1) * Num(d.halfDim[0], scope, 0),
                               (Rand() * 2 - 1) * Num(d.halfDim[1], scope, 0),
                               (Rand() * 2 - 1) * Num(d.halfDim[2], scope, 0));
        } else if (d.shape == 3) {
            glm::dvec3 n(Num(d.normal[0], scope, 0), Num(d.normal[1], scope, 1), Num(d.normal[2], scope, 0));
            if (glm::dot(n, n) < 1e-9)
                n = {0, 1, 0};
            n = glm::normalize(n);
            glm::dvec3 a = std::fabs(n.y) < 0.99 ? glm::normalize(glm::cross(n, glm::dvec3(0, 1, 0)))
                                                 : glm::dvec3(1, 0, 0);
            const glm::dvec3 b = glm::cross(n, a);
            const double r = Num(d.radius, scope, 1.0) * std::sqrt(Rand());
            const double ang = Rand() * 6.283185307179586;
            local = a * (std::cos(ang) * r) + b * (std::sin(ang) * r);
        }
        p.pos = e.origin + offset + local;
        glm::dvec3 dir(0.0);
        if (d.dirMode == 2) {
            dir = {Num(d.dir[0], scope, 0), Num(d.dir[1], scope, 0), Num(d.dir[2], scope, 0)};
        } else if (glm::dot(local, local) > 1e-12) {
            dir = glm::normalize(local);
            if (d.dirMode == 1)
                dir = -dir;
        } else {
            dir = unit();
        }
        if (d.speed[1] || d.speed[2]) {
            p.vel = {Num(d.speed[0], scope, 0), Num(d.speed[1], scope, 0), Num(d.speed[2], scope, 0)};
        } else {
            if (glm::dot(dir, dir) > 1e-12)
                dir = glm::normalize(dir);
            p.vel = dir * Num(d.speed[0], scope, 0);
        }
        p.lifetime = std::max(0.05, Num(d.particleLife, scope, 1.0));
        p.rotation = static_cast<float>(Num(d.spin, scope, 0)) * kDeg;
        p.rotationRate = static_cast<float>(Num(d.spinRate, scope, 0)) * kDeg;
        particles.push_back(std::move(p));
        e.alive++;
    }

    void DataParticles::Update(float dt, const DataWorld& world) {
        dt = std::min(dt, 0.1f);
        for (Live& live : emitters) {
            Emitter& e = live.e;
            const Def& d = *e.def;
            if (e.done)
                continue;
            molang::Scope scope;
            Prepare(scope, e, nullptr);
            for (const ExprPtr& u : d.perUpdate)
                if (u)
                    u->Eval(scope);
            for (const auto& [name, value] : scope.variables)
                if (!name.starts_with("emitter_"))
                    e.variables[name] = value;
            const double before = e.age;
            e.age += dt;
            bool active = true;
            if (d.lifeKind == 0) {
                active = before < e.lifetime;
            } else if (d.lifeKind == 1) {
                const double on = d.activeTime ? d.activeTime->Number(scope) : 10.0;
                const double off = d.sleepTime ? d.sleepTime->Number(scope) : 0.0;
                const double cycle = std::max(0.001, on + off);
                const double phase = std::fmod(before, cycle);
                active = phase < on;
                if (phase + dt >= cycle)
                    e.fired = false;
            } else if (d.lifeKind == 2) {
                active = !d.activation || d.activation->Number(scope) != 0.0;
                if (d.expiration && d.expiration->Number(scope) != 0.0) {
                    e.done = true;
                    continue;
                }
            }
            if (active) {
                if (d.rateKind == 0) {
                    if (!e.fired) {
                        e.fired = true;
                        const int n = static_cast<int>(Num(d.instantCount, scope, 1));
                        for (int i = 0; i < n && i < 1024; i++)
                            SpawnOne(live);
                    }
                } else if (d.rateKind == 1) {
                    const double rate = Num(d.steadyRate, scope, 1.0);
                    const int max = static_cast<int>(Num(d.steadyMax, scope, 50.0));
                    e.spawnDebt += rate * dt;
                    while (e.spawnDebt >= 1.0) {
                        e.spawnDebt -= 1.0;
                        if (e.alive < max)
                            SpawnOne(live);
                    }
                }
            }
            if (d.lifeKind == 0 && e.age >= e.lifetime)
                e.done = true;
        }
        std::unordered_map<uint64_t, Live*> byId;
        for (Live& l : emitters)
            byId[l.id] = &l;
        for (size_t i = 0; i < particles.size();) {
            Particle& p = particles[i];
            const Def& d = *p.def;
            const auto owner = byId.find(p.emitterId);
            bool dead = false;
            if (owner != byId.end()) {
                molang::Scope scope;
                Prepare(scope, owner->second->e, &p);
                if (d.particleExpire && d.particleExpire->Number(scope) != 0.0)
                    dead = true;
                if (d.parametric) {
                    p.pos = p.origin + glm::dvec3(Num(d.relPos[0], scope, 0), Num(d.relPos[1], scope, 0),
                                                  Num(d.relPos[2], scope, 0));
                } else if (d.dynamic) {
                    glm::dvec3 acc(Num(d.accel[0], scope, 0), Num(d.accel[1], scope, 0),
                                   Num(d.accel[2], scope, 0));
                    acc -= p.vel * Num(d.drag, scope, 0);
                    p.vel += acc * static_cast<double>(dt);
                    glm::dvec3 next = p.pos + p.vel * static_cast<double>(dt);
                    if (d.collide && world.solid) {
                        const double radius = Num(d.collideRadius, scope, 0.01);
                        for (int axis = 0; axis < 3; axis++) {
                            glm::dvec3 probe = p.pos;
                            probe[axis] = next[axis] + (p.vel[axis] > 0 ? radius : -radius);
                            if (world.solid(static_cast<int>(std::floor(probe.x)),
                                            static_cast<int>(std::floor(probe.y)),
                                            static_cast<int>(std::floor(probe.z)))) {
                                next[axis] = p.pos[axis];
                                p.vel[axis] *= -d.restitution;
                                for (int other = 0; other < 3; other++)
                                    if (other != axis)
                                        p.vel[other] *= std::max(0.0f, 1.0f - d.collideDrag * dt);
                                if (d.expireOnContact)
                                    dead = true;
                            }
                        }
                    }
                    p.pos = next;
                    p.rotationRate += static_cast<float>(Num(d.rotAccel, scope, 0) * dt * kDeg);
                    p.rotationRate -= static_cast<float>(Num(d.rotDrag, scope, 0)) * p.rotationRate * dt;
                }
                p.rotation += p.rotationRate * dt;
            } else {
                dead = true;
            }
            p.age += dt;
            if (p.age >= p.lifetime)
                dead = true;
            if (dead) {
                if (owner != byId.end())
                    owner->second->e.alive = std::max(0, owner->second->e.alive - 1);
                particles[i] = particles.back();
                particles.pop_back();
            } else {
                i++;
            }
        }
        std::erase_if(emitters, [](const Live& l) { return l.e.done && l.e.alive <= 0; });
    }

    void DataParticles::Collect(const CameraView& cam, const DataWorld& world,
                                std::vector<CustomQuad>& out) const {
        std::unordered_map<uint64_t, const Live*> byId;
        for (const Live& l : emitters)
            byId[l.id] = &l;
        molang::Scope scope;
        for (const Particle& p : particles) {
            const auto owner = byId.find(p.emitterId);
            if (owner == byId.end())
                continue;
            const Def& d = *p.def;
            Prepare(scope, owner->second->e, &p);
            CustomQuad q;
            q.centre = p.pos;
            q.texture = d.texture;
            q.blend = d.blend;
            const float w = static_cast<float>(Num(d.size[0], scope, 0.1)),
                        h = static_cast<float>(Num(d.size[1], scope, 0.1));
            glm::vec3 right = cam.right, up = cam.up;
            const std::string& f = d.facing;
            if (f == "rotate_y" || f == "lookat_y") {
                right = glm::normalize(glm::vec3(cam.right.x, 0.0f, cam.right.z));
                up = {0.0f, 1.0f, 0.0f};
            } else if (f == "emitter_transform_xy") {
                right = {1, 0, 0};
                up = {0, 1, 0};
            } else if (f == "emitter_transform_xz") {
                right = {1, 0, 0};
                up = {0, 0, 1};
            } else if (f == "emitter_transform_yz") {
                right = {0, 0, 1};
                up = {0, 1, 0};
            } else if (f.starts_with("direction_") || d.dirFacing == 1) {
                glm::vec3 v = glm::vec3(p.vel);
                if (glm::dot(v, v) > d.minSpeed * d.minSpeed) {
                    up = glm::normalize(v);
                    glm::vec3 r = glm::cross(up, cam.forward);
                    right = glm::dot(r, r) > 1e-8f ? glm::normalize(r) : cam.right;
                }
            }
            if (f != "emitter_transform_xy" && f != "emitter_transform_xz" && f != "emitter_transform_yz" &&
                p.rotation != 0.0f) {
                const float c = std::cos(p.rotation), s = std::sin(p.rotation);
                const glm::vec3 r2 = right * c + up * s, u2 = up * c - right * s;
                right = r2;
                up = u2;
            }
            q.right = right * w;
            q.up = up * h;
            float u = 0, v = 0, uw = d.texW, vh = d.texH;
            if (d.flipbook) {
                u = static_cast<float>(Num(d.baseUv[0], scope, 0));
                v = static_cast<float>(Num(d.baseUv[1], scope, 0));
                uw = static_cast<float>(Num(d.sizeUv[0], scope, d.texW));
                vh = static_cast<float>(Num(d.sizeUv[1], scope, d.texH));
                const double maxFrame = std::max(1.0, Num(d.maxFrame, scope, 1.0));
                double frame = d.stretch ? p.age / p.lifetime * maxFrame : Num(d.fps, scope, 0.0) * p.age;
                frame = d.loop ? std::fmod(frame, maxFrame) : std::min(frame, maxFrame - 1.0);
                const double index = std::floor(std::max(0.0, frame));
                u += d.stepUv[0] * static_cast<float>(index);
                v += d.stepUv[1] * static_cast<float>(index);
            } else if (d.uv[0]) {
                u = static_cast<float>(Num(d.uv[0], scope, 0));
                v = static_cast<float>(Num(d.uv[1], scope, 0));
                uw = static_cast<float>(Num(d.uvSize[0], scope, d.texW));
                vh = static_cast<float>(Num(d.uvSize[1], scope, d.texH));
            }
            const float inset = 0.02f;
            q.u0 = (u + inset) / d.texW;
            q.v0 = (v + inset) / d.texH;
            q.u1 = (u + uw - inset) / d.texW;
            q.v1 = (v + vh - inset) / d.texH;
            glm::vec4 c(1.0f);
            if (d.hasGradient) {
                const float t = static_cast<float>(Num(d.interpolant, scope, 0.0));
                const auto& stops = d.gradient.stops;
                if (t <= stops.front().first) {
                    c = stops.front().second;
                } else if (t >= stops.back().first) {
                    c = stops.back().second;
                } else {
                    for (size_t i = 0; i + 1 < stops.size(); i++)
                        if (t >= stops[i].first && t <= stops[i + 1].first) {
                            const float span = std::max(1e-6f, stops[i + 1].first - stops[i].first);
                            c = glm::mix(stops[i].second, stops[i + 1].second, (t - stops[i].first) / span);
                            break;
                        }
                }
            } else if (d.hasColor) {
                c = {static_cast<float>(Num(d.color[0], scope, 1)),
                     static_cast<float>(Num(d.color[1], scope, 1)),
                     static_cast<float>(Num(d.color[2], scope, 1)),
                     static_cast<float>(Num(d.color[3], scope, 1))};
            }
            if (world.lightLevels) {
                const glm::ivec2 l = d.lit ? world.lightLevels(p.pos) : glm::ivec2(15, 15);
                q.blockLight = static_cast<int8_t>(l.x);
                q.skyLight = static_cast<int8_t>(l.y);
            } else if (d.lit && world.light) {
                c = glm::vec4(glm::vec3(c) * world.light(p.pos), c.a);
            }
            q.abgr = Pack(c);
            out.push_back(std::move(q));
        }
    }
}
