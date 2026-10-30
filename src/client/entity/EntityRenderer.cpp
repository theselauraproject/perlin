#include "client/entity/EntityRenderer.hpp"
#include "client/entity/CapeGeometry.inc"
#include "client/entity/CapePhysics.hpp"

#include <cstdio>
#include "util/Env.hpp"
#include "util/Paths.hpp"
#include "client/entity/EntityQuery.hpp"
#include "client/hand/HandRenderer.hpp"
#include "client/ui/ItemData.hpp"

#include "client/ui/UiAssets.hpp"
#include "client/ui/FormatCodes.hpp"
#include "client/ui/McFont.hpp"

#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>
#include <deque>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace perlin::entity {
    namespace {
        void ApplyJavaCrouch(attachable::Pose& pose, const attachable::Geometry* g) {
            static const float zs = PERLIN_ENV("PERLIN_ZSIGN") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_ZSIGN"))) : -1.0f;
            const auto find = [&](const char* name) -> const attachable::Bone* {
                if (g)
                    for (const auto& b : g->bones)
                        if (b.name == name)
                            return &b;
                return nullptr;
            };
            const auto pivotOf = [&](const char* name, glm::vec3 fallback) {
                const auto* b = find(name);
                return b ? b->pivot : fallback;
            };
            const auto restOf = [&](const char* name) {
                const auto* b = find(name);
                return b ? b->rotation : glm::vec3(0.0f);
            };
            const auto local = [&](const char* name, const glm::vec3& c, const glm::vec3& rot, const glm::vec3& pos) {
                return glm::translate(glm::mat4(1.0f), pos) * glm::translate(glm::mat4(1.0f), c) * attachable::EulerZYX(restOf(name) + rot) *
                       glm::translate(glm::mat4(1.0f), -c);
            };
            auto& body = pose["body"];
            const glm::vec3 bodyPivot = pivotOf("body", {0.0f, 24.0f, 0.0f});
            const glm::mat4 oldBody = local("body", bodyPivot, body.rotation, body.position);
            body.rotation.x += 28.64789f;
            body.position.y -= 3.2f;
            const glm::mat4 newBody = local("body", bodyPivot, body.rotation, body.position);
            const glm::mat4 invNew = glm::inverse(newBody);
            const auto refit = [&](const char* name, glm::vec3 fallbackPivot, float down, float extraX) {
                auto& x = pose[name];
                const glm::vec3 c = pivotOf(name, fallbackPivot);
                glm::vec3 rot = x.rotation;
                rot.x += extraX;
                const glm::mat4 desired = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -down, 0.0f)) * oldBody * local(name, c, rot, x.position);
                const glm::mat4 fit = invNew * desired;
                const float b = std::asin(std::clamp(-fit[0][2], -1.0f, 1.0f));
                const float cc = std::atan2(fit[1][2], fit[2][2]);
                const float a = std::atan2(fit[0][1], fit[0][0]);
                const glm::vec3 euler(-glm::degrees(cc), -glm::degrees(b), glm::degrees(a) / zs);
                x.rotation = euler - restOf(name);
                const glm::mat3 r(glm::rotate(glm::mat4(1.0f), glm::radians(zs * euler.z), glm::vec3(0, 0, 1)) *
                                  glm::rotate(glm::mat4(1.0f), glm::radians(-euler.y), glm::vec3(0, 1, 0)) *
                                  glm::rotate(glm::mat4(1.0f), glm::radians(-euler.x), glm::vec3(1, 0, 0)));
                const glm::vec3 t(fit[3]);
                x.position = t - c + r * c;
            };
            refit("head", {0.0f, 24.0f, 0.0f}, 4.2f, 0.0f);
            refit("leftarm", {5.0f, 22.0f, 0.0f}, 3.2f, 22.91831f);
            refit("rightarm", {-5.0f, 22.0f, 0.0f}, 3.2f, 22.91831f);
            pose["leftleg"].position.z += 4.0f;
            pose["rightleg"].position.z += 4.0f;
            pose["root"].position.y -= 2.0f;
        }

        void StyleFromMaterial(UiDraw& draw, const char* name) {
            const std::string_view materialName(name);
            if (materialName.starts_with("particles_")) draw.material = DrawMaterial::Particle;
            else if (materialName.starts_with("name_")) draw.material = DrawMaterial::Nametag;
            else if (materialName == "cracks_overlay") draw.material = DrawMaterial::Cracks;
            const material::Def* def = material::Registry::Instance().Get().Find(name);
            if (!def)
                return;
            const MaterialInfo m = InfoFromDef(*def);
            draw.depth = m.depthTest;
            draw.depthFunc = m.depthFunc;
            draw.depthWrite = m.depthWrite;
            draw.colorWrite = m.colorWrite;
            draw.alphaWrite = m.alphaWrite;
            draw.customBlend = false;
            if (!def->Blending()) {
                draw.blend = 0;
                return;
            }
            draw.blend = m.blend == MaterialInfo::Blend::Additive ? 2 : 1;
            if (m.customBlend) {
                draw.customBlend = true;
                draw.blendSrc = m.blendSrc;
                draw.blendDst = m.blendDst;
            }
        }
    }

    using attachable::Animation;
    using attachable::Pose;
    using molang::Value;

    namespace {
        constexpr float kDeg = 3.14159265358979323846f / 180.0f;

        float WrapDegrees(float a) {
            a = std::fmod(a + 180.0f, 360.0f);
            if (a < 0)
                a += 360.0f;
            return a - 180.0f;
        }

        uint32_t PackColor(const glm::vec3& c, float alpha = 1.0f) {
            auto q = [](float x) { return static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return (q(alpha) << 24) | (q(c.b) << 16) | (q(c.g) << 8) | q(c.r);
        }

        bool Matches(const std::string& pattern, const std::string& bone) {
            if (pattern == "*")
                return true;
            if (!pattern.empty() && pattern.back() == '*')
                return bone.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
            return pattern == bone;
        }
    }

    namespace {
        const float kAttachDrop = PERLIN_ENV("PERLIN_ATTACH_DROP") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_ATTACH_DROP"))) : -24.0f;
    }

    struct EntityRenderer::View {
        bool init = false;
        glm::dvec3 pos{0.0}, lastPos{0.0};
        uint32_t sampledSerial = 0;
        struct Sample {
            double t = 0;
            glm::dvec3 pos{0.0};
            float yaw = 0, headYaw = 0, pitch = 0;
        };
        std::deque<Sample> samples;
        double interval = 0.05, jitter = 0.0, delay = 0.1;
        double lastArrival = 0;
        float yaw = 0, headYaw = 0, pitch = 0;
        uint32_t moveSerial = 0, hurtSerial = 0, swingSerial = 0;
        float limbSwing = 0, limbAmount = 0;
        float groundSpeed = 0, verticalSpeed = 0;
        float capeFlap = 0;
        glm::dvec3 cloak{0.0}, cloakPrev{0.0}, tickPos{0.0}, tickPosPrev{0.0};
        bool limbInit = false;
        double limbAcc = 0;
        float limbSpd = 0, limbSpdPrev = 0, limbPos = 0, limbPosPrev = 0;
        float bob = 0, bobPrev = 0, walkDist = 0, walkDistPrev = 0, capeX = 6.0f, capeZ = 0.0f;
        glm::mat4 itemR{1.0f}, itemL{1.0f};
        bool hasItemR = false, hasItemL = false;
        bool attachedItem[2] = {false, false};
        bool fp = false;
        double usingSince = 0;
        bool wasUsing = false;
        float attackTime = 0, hurtTimer = 0, deathTime = 0;
        double born = 0, lastSeen = 0, now = 0;
        float dt = 0;
        float animTime = 0;
        float glideTime = 0, yawSpeed = 0, standAmount = 0, sitAmount = 0, swimAmount = 0, lieAmount = 0, swellAmount = 0;
        molang::Scope scope;
        bool initialised = false;
        struct AnimStamp { double start = 0; uint32_t seen = 0; };
        std::unordered_map<std::string, AnimStamp> animStart;
        uint32_t animEpoch = 0;
        struct CtrlState {
            std::string state;
            double entered = 0;
            bool started = false;
        };
        std::map<std::string, CtrlState> ctrl;
        std::vector<EntityRenderer::Layer> cachedLayers;
        float cachedScale = 1.0f;
        int cacheAge = 1 << 20;
        double cacheTime = -1e9;
        const Entity* entity = nullptr;
        std::string tag;
        glm::mat4 armR{1.0f}, armL{1.0f};
        bool hasArmR = false, hasArmL = false;
        bool bodyInit = false;
        float body = 0, bodyPrev = 0;
        double bodyTickAcc = 0;
        glm::dvec3 bodyTickPos{0.0};
        CapeChain cape;
    };

    namespace {
        struct EntityHost final : molang::Host, QueryContext {
            bool Query(const std::string& name, const molang::Args& args, Value& out) override {
                return EvalEntityQuery(*this, name, args, out);
            }

            bool Volatile(const std::string& name) const override {
                return name == "anim_time" || name == "all_animations_finished" || name == "any_animation_finished";
            }

            bool Context(const std::string& name, Value& out) override {
                if (name == "is_first_person") { out = Value::Num(firstPerson ? 1 : 0); return true; }
                if (name == "player_offhand_arm_height") { out = Value::Num(offArmHeight); return true; }
                return false;
            }
        };
    }

    EntityRenderer::EntityRenderer() = default;
    EntityRenderer::~EntityRenderer() = default;

    void EntityRenderer::Initialize(RenderContext& r) {
        renderer = &r;
    }

    glm::dvec3 EntityRenderer::SeatPosition(const Entity& v, bool playerRider) const {
        float sx = 0, sy = 0, sz = 0;
        if (!assets.Seat(v.type, sx, sy, sz))
            sy = -0.2f;
        const double yawRad = glm::radians(static_cast<double>(v.yaw));
        const glm::dvec3 forward(-std::sin(yawRad), 0.0, std::cos(yawRad)), side(std::cos(yawRad), 0.0, std::sin(yawRad));
        const bool rails = v.type.find("boat") != std::string::npos || v.type.find("minecart") != std::string::npos;
        double lift = rails ? 0.1 : 0.0;
        if (playerRider && !rails)
            lift -= v.type == "minecraft:cushion" ? 0.35 : 0.6;
        return v.pos + side * static_cast<double>(sx) + forward * static_cast<double>(sz) + glm::dvec3(0.0, sy + lift, 0.0);
    }

    void EntityRenderer::ResolveSeats(std::vector<Entity>& entities) const {
        std::unordered_map<int64_t, size_t> byUid;
        for (size_t i = 0; i < entities.size(); i++)
            byUid[entities[i].uniqueId] = i;
        for (Entity& e : entities) {
            if (e.vehicleUid == 0)
                continue;
            const auto it = byUid.find(e.vehicleUid);
            if (it == byUid.end())
                continue;
            const Entity& v = entities[it->second];
            const bool rails = v.type.find("boat") != std::string::npos || v.type.find("minecart") != std::string::npos;
            e.pos = SeatPosition(v, e.isPlayer);
            e.flags |= flag::Riding;
            e.onGround = true;
            if (rails)
                e.yaw = v.yaw;
        }
    }

    std::pair<float, float> EntityRenderer::HitBox(const Entity& e) const {
        const float scale = e.scale > 0.0f ? e.scale : 1.0f;
        // server's bounding box wins: metadata BOUNDINGBOX_WIDTH/HEIGHT is what it collides and raycasts with
        if (e.boxWidth > 0.0f && e.boxHeight > 0.0f)
            return {e.boxWidth * scale, e.boxHeight * scale};
        if (e.isPlayer)
            return {0.6f, (e.Has(flag::Gliding) || e.Has(flag::Swimming) || e.crawling) ? 0.6f : e.Has(flag::Sneaking) ? 1.5f : 1.8f};
        if (e.isItem)
            return {0.25f, 0.25f};
        float w, h;
        if (assets.CollisionBox(e.type, w, h))
            return {w * scale, h * scale};
        return {0.6f, 1.0f};
    }

    void EntityRenderer::ClearCaches() {
        if (renderer) {
            for (auto& [path, id] : textures)
                if (id)
                    renderer->UiDestroyTexture(id);
            for (auto& [s, id] : skinTextures)
                if (id)
                    renderer->UiDestroyTexture(id);
            for (auto& [s, id] : capeModelTextures)
                if (id)
                    renderer->UiDestroyTexture(id);
            for (auto& [s, id] : capeTextures)
                if (id)
                    renderer->UiDestroyTexture(id);
            for (auto& [tile, id] : particleTiles)
                if (id)
                    renderer->UiDestroyTexture(id);
            if (particleSheet)
                renderer->UiDestroyTexture(particleSheet);
            for (uint32_t m : pendingMeshes)
                renderer->UiDestroyMesh(m);
        }
        particleTiles.clear();
        particleSheet = 0;
        particleSheetTried = false;
        textures.clear();
        skinTextures.clear();
        capeTextures.clear();
        capeModelTextures.clear();
        pendingMeshes.clear();
        views.clear();
        skinGeometry.clear();
        voxelGeometry.clear();
        skinOwners.clear();
        for (auto& geometry : orbGeometry) geometry.clear();
    }

    void EntityRenderer::PruneSkinCaches() {
        for (auto it = skinOwners.begin(); it != skinOwners.end();) {
            if (!it->second.expired()) {
                ++it;
                continue;
            }
            const uint64_t serial = it->first;
            for (auto* cache : {&skinTextures, &capeTextures, &capeModelTextures}) {
                const auto texture = cache->find(serial);
                if (texture != cache->end()) {
                    if (texture->second)
                        renderer->UiDestroyTexture(texture->second);
                    cache->erase(texture);
                }
            }
            skinGeometry.erase(serial);
            voxelGeometry.erase(serial);
            it = skinOwners.erase(it);
        }
    }

    void EntityRenderer::Shutdown() {
        ClearCaches();
        renderer = nullptr;
    }

    namespace {
        uint32_t DyeColorRgb(int dye) {
            static constexpr uint32_t kDye[16] = {0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F,
                                                  0xF38BAA, 0x474F52, 0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA,
                                                  0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21};
            return (dye >= 0 && dye < 16) ? kDye[dye] : kDye[0];
        }

        void BakeColorMask(Texture& t, uint32_t rgb) {
            const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
            for (size_t i = 0; i + 3 < t.pixels.size(); i += 4) {
                uint8_t* p = &t.pixels[i];
                if (p[3] == 0)
                    continue;
                if (p[3] >= 128) {
                    p[0] = static_cast<uint8_t>(p[0] * r / 255);
                    p[1] = static_cast<uint8_t>(p[1] * g / 255);
                    p[2] = static_cast<uint8_t>(p[2] * b / 255);
                }
                p[3] = 255;
            }
        }
    }

    uint32_t EntityRenderer::TextureFor(const Entity& e, const std::string& key) {
        if (e.isPlayer && e.skin) {
            const SkinData& s = *e.skin;
            skinOwners[s.serial] = e.skin;
            if (key == "default" && s.skinWidth > 0) {
                auto& id = skinTextures[s.serial];
                if (!id)
                    id = renderer->UiCreateTexture(s.skinWidth, s.skinHeight, s.skinRgba.data(), false);
                return id;
            }
            if (key == "capemodel" && s.capeWidth > 0) {
                auto& id = capeModelTextures[s.serial];
                if (!id) {
                    std::vector<uint8_t> swapped = s.capeRgba;
                    const int sc = std::max(1, s.capeWidth / 64);
                    for (int y = sc; y < 17 * sc && y < s.capeHeight; y++)
                        for (int x = 0; x < 10 * sc; x++) {
                            const size_t a = (static_cast<size_t>(y) * static_cast<size_t>(s.capeWidth) + static_cast<size_t>(sc + x)) * 4;
                            const size_t b = (static_cast<size_t>(y) * static_cast<size_t>(s.capeWidth) + static_cast<size_t>(12 * sc + x)) * 4;
                            for (int c = 0; c < 4; c++)
                                std::swap(swapped[a + static_cast<size_t>(c)], swapped[b + static_cast<size_t>(c)]);
                        }
                    id = renderer->UiCreateTexture(s.capeWidth, s.capeHeight, swapped.data(), false);
                }
                return id;
            }
            if (key == "cape" && s.capeWidth > 0) {
                auto& id = capeTextures[s.serial];
                if (!id)
                    id = renderer->UiCreateTexture(s.capeWidth, s.capeHeight, s.capeRgba.data(), false);
                return id;
            }
        }
        if (e.isPlayer && key == "default") {
            const std::string path = e.skin && e.skin->slim ? "textures/entity/alex" : "textures/entity/steve";
            auto it = textures.find(path);
            if (it == textures.end()) {
                Texture t;
                uint32_t id = 0;
                if (ui::UiAssets::Instance().Image(path, t) && t.width > 0)
                    id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
                it = textures.emplace(path, id).first;
            }
            return it->second;
        }
        return 0;
    }

    void EntityRenderer::TickPlayerBody(View& v, float dt, float prevYaw) {
        const float look = v.headYaw;
        if (!v.bodyInit) {
            v.bodyInit = true;
            v.body = v.bodyPrev = look;
            v.bodyTickPos = v.pos;
        }
        const double xd = v.pos.x - v.bodyTickPos.x, zd = v.pos.z - v.bodyTickPos.z;
        v.bodyTickPos = v.pos;
        const float speed = static_cast<float>(std::sqrt(xd * xd + zd * zd)) / std::max(dt, 0.001f);
        v.bodyTickAcc += (speed - static_cast<float>(v.bodyTickAcc)) * std::min(1.0f, dt * 12.0f);
        const bool moving = v.bodyTickAcc > 0.35 || v.attackTime > 0.0f;
        const float tau = moving ? 0.045f : 0.09f;
        const float cap = moving ? 30.0f : 40.0f;
        float diff = WrapDegrees(look - v.body);
        v.body += diff * (1.0f - std::exp(-dt / tau));
        diff = WrapDegrees(look - v.body);
        if (std::fabs(diff) > cap)
            v.body += diff - (diff > 0.0f ? cap : -cap);
        v.body = WrapDegrees(v.body);
        v.yaw = v.body;
        v.yawSpeed = WrapDegrees(v.yaw - prevYaw) / std::max(dt, 0.001f);
    }

    void EntityRenderer::Track(const Entity& e, View& v, const FrameIn& in) {
        const float dt = std::clamp(in.dt, 0.001f, 0.25f);
        v.dt = dt;
        v.now = in.time;
        if (!v.init) {
            v.init = true;
            v.pos = v.lastPos = e.pos;
            v.yaw = e.yaw;
            v.headYaw = e.headYaw;
            v.pitch = e.pitch;
            v.born = in.time;
            v.moveSerial = e.moveSerial;
            v.hurtSerial = e.hurtSerial;
            v.swingSerial = e.swingSerial;
        }
        const bool projectile = e.type == "minecraft:arrow" || e.type == "minecraft:thrown_trident" ||
                                        e.type == "minecraft:snowball" || e.type == "minecraft:egg" || e.type == "minecraft:ender_pearl" ||
                                        e.type == "minecraft:splash_potion" || e.type == "minecraft:lingering_potion" ||
                                        e.type == "minecraft:xp_bottle" || e.type == "minecraft:fireball" ||
                                        e.type == "minecraft:wither_skull" || e.type == "minecraft:small_fireball" ||
                                        e.type == "minecraft:shulker_bullet" || e.type == "minecraft:llama_spit" ||
                                        e.type == "minecraft:dragon_fireball";
        const bool buffered = !e.isSelf && !projectile;
        const double follow = e.isSelf ? 1.0 : 1.0 - std::exp(-static_cast<double>(dt) / 0.06);
        v.lastPos = v.pos;
        const float prevYaw = v.yaw;
        glm::dvec3 d = e.pos - v.pos;
        if (buffered) {
            // replay every move since last frame, not just the newest: a batch of several would otherwise drop all
            // but one and leave a long gap. moves of one batch are a server tick apart, ending at this frame
            const uint32_t fresh = std::min(e.moveSerial - v.sampledSerial, Entity::kMoveRing);
            if (v.sampledSerial == 0 || e.moveSerial - v.sampledSerial > Entity::kMoveRing)
                v.sampledSerial = e.moveSerial - std::min<uint32_t>(fresh, 1);
            for (uint32_t serial = e.moveSerial - std::min(fresh, e.moveSerial - v.sampledSerial) + 1;
                 fresh > 0 && serial <= e.moveSerial; serial++) {
                const Entity::Move& m = e.moves[serial % Entity::kMoveRing];
                if (!v.samples.empty() && glm::length(m.pos - v.samples.back().pos) > 12.0) {
                    v.samples.clear();
                    v.pos = m.pos;
                    v.lastPos = m.pos;
                    v.yaw = m.yaw;
                    v.headYaw = m.headYaw;
                    v.pitch = m.pitch;
                    v.lastArrival = 0;
                }
                double t = in.time - static_cast<double>(e.moveSerial - serial) * 0.05;
                if (!v.samples.empty())
                    t = std::max(t, v.samples.back().t + 0.005);
                if (v.lastArrival > 0) {
                    const double gap = std::clamp(t - v.lastArrival, 0.005, 0.5);
                    const double dev = std::fabs(gap - v.interval);
                    v.interval += (gap - v.interval) * 0.1;
                    v.jitter += (dev - v.jitter) * 0.1;
                }
                v.lastArrival = t;
                v.samples.push_back({t, m.pos, m.yaw, m.headYaw, m.pitch});
                while (v.samples.size() > 64)
                    v.samples.pop_front();
            }
            v.sampledSerial = e.moveSerial;
            const double wantDelay = std::clamp(v.interval * 1.25 + v.jitter * 2.0, 0.06, 0.25);
            v.delay += (wantDelay - v.delay) * std::min(1.0, static_cast<double>(dt) * 2.0);
            const double renderT = in.time - v.delay;
            while (v.samples.size() > 2 && v.samples[1].t <= renderT)
                v.samples.pop_front();
            if (v.samples.size() >= 2 && renderT >= v.samples[0].t) {
                const auto& a = v.samples[0];
                const auto& b = v.samples[1];
                const double span = b.t - a.t;
                const double f = span > 1e-6 ? std::clamp((renderT - a.t) / span, 0.0, 1.0) : 1.0;
                v.pos = a.pos + (b.pos - a.pos) * f;
                const auto mix = [&](float x, float y) { return x + WrapDegrees(y - x) * static_cast<float>(f); };
                v.yaw = mix(a.yaw, b.yaw);
                v.headYaw = mix(a.headYaw, b.headYaw);
                v.pitch = a.pitch + WrapDegrees(b.pitch - a.pitch) * static_cast<float>(f);
            } else if (v.samples.size() >= 2) {
                // render time behind the oldest sample (the delay just grew): hold, don't snap back to it
            } else if (!v.samples.empty()) {
                const double catchUp = 1.0 - std::exp(-static_cast<double>(dt) / 0.06);
                v.pos += (v.samples.back().pos - v.pos) * catchUp;
                const auto turnTo = [&](float cur, float target) { return cur + WrapDegrees(target - cur) * static_cast<float>(catchUp); };
                v.yaw = turnTo(v.yaw, v.samples.back().yaw);
                v.headYaw = turnTo(v.headYaw, v.samples.back().headYaw);
                v.pitch += WrapDegrees(v.samples.back().pitch - v.pitch) * static_cast<float>(catchUp);
            }
            v.yawSpeed = WrapDegrees(v.yaw - prevYaw) / dt;
        } else {
            v.pos = glm::length(d) > 24.0 ? e.pos : v.pos + d * follow;
            const auto turn = [&](float cur, float target) { return cur + WrapDegrees(target - cur) * static_cast<float>(follow); };
            v.yaw = turn(v.yaw, e.yaw);
            v.yawSpeed = WrapDegrees(v.yaw - prevYaw) / dt;
            v.headYaw = turn(v.headYaw, e.headYaw);
            v.pitch = v.pitch + WrapDegrees(e.pitch - v.pitch) * static_cast<float>(follow);
        }
        // velocity-aligned flight: arrows, tridents and lobbed bottles alike
        if (projectile) {
            const double len = glm::length(d);
            if (len > 0.04) {
                const float targetYaw = static_cast<float>(std::atan2(-d.x, d.z) * 180.0 / 3.14159265358979);
                const float targetPitch = static_cast<float>(-std::asin(std::clamp(d.y / len, -1.0, 1.0)) * 180.0 / 3.14159265358979);
                const float aim = 1.0f - std::exp(-dt / 0.03f);
                v.yaw = v.yaw + WrapDegrees(targetYaw - v.yaw) * aim;
                v.pitch = v.pitch + (targetPitch - v.pitch) * aim;
            }
            v.headYaw = v.yaw;
            v.yawSpeed = 0.0f;
        }

        if (e.isPlayer && !v.fp)
            TickPlayerBody(v, dt, prevYaw);

        const glm::dvec3 step = v.pos - v.lastPos;
        const float horizontal = static_cast<float>(std::sqrt(step.x * step.x + step.z * step.z));
        v.groundSpeed = horizontal / dt;
        v.verticalSpeed = static_cast<float>(step.y) / dt;
        {
            if (!v.limbInit) {
                v.limbInit = true;
                v.tickPos = v.tickPosPrev = v.pos;
                v.cloak = v.cloakPrev = v.pos;
            }
            v.limbAcc += dt;
            int ticks = 0;
            while (v.limbAcc >= 0.05 && ticks < 8) {
                v.limbAcc -= 0.05;
                ticks++;
                v.tickPosPrev = v.tickPos;
                v.tickPos = v.pos;
                const glm::dvec3 moved = v.tickPos - v.tickPosPrev;
                const float dist = static_cast<float>(std::sqrt(moved.x * moved.x + moved.z * moved.z));
                if (dist > 8.0f) {
                    v.tickPosPrev = v.tickPos;
                    v.limbSpd = v.limbSpdPrev = 0.0f;
                    v.cloak = v.cloakPrev = v.tickPos;
                    v.cape.Reset();
                    continue;
                }
                v.limbSpdPrev = v.limbSpd;
                v.limbPosPrev = v.limbPos;
                v.limbSpd += (std::min(dist * 4.0f, 1.0f) - v.limbSpd) * 0.4f;
                v.limbPos += v.limbSpd;
                const bool onGround = std::fabs(moved.y) < 0.02;
                v.bobPrev = v.bob;
                v.bob += ((onGround ? std::min(0.1f, dist) : 0.0f) - v.bob) * 0.4f;
                v.walkDistPrev = v.walkDist;
                v.walkDist += dist * 0.6f;
                v.cloakPrev = v.cloak;
                const glm::dvec3 off = v.tickPos - v.cloak;
                if (std::fabs(off.x) > 10.0 || std::fabs(off.y) > 10.0 || std::fabs(off.z) > 10.0)
                    v.cloak = v.cloakPrev = v.tickPos;
                else
                    v.cloak += off * 0.25;
                if (e.isPlayer) {
                    CapeTick ct;
                    ct.cloakX = static_cast<float>(v.cloak.x - v.tickPos.x);
                    ct.cloakZ = static_cast<float>(v.cloak.z - v.tickPos.z);
                    ct.bodyYaw = v.yaw;
                    ct.yaw = v.headYaw;
                    ct.moveX = static_cast<float>(moved.x);
                    ct.moveY = static_cast<float>(moved.y);
                    ct.moveZ = static_cast<float>(moved.z);
                    ct.crouching = e.Has(flag::Sneaking);
                    v.cape.Tick(ct);
                }
            }
            if (ticks > 1 || v.limbAcc > 0.25) {
                v.limbPosPrev = v.limbPos;
                v.limbSpdPrev = v.limbSpd;
                v.cloakPrev = v.cloak;
                v.tickPosPrev = v.tickPos;
                v.bobPrev = v.bob;
                v.walkDistPrev = v.walkDist;
            }
            v.limbAcc = std::min(v.limbAcc, 0.05);
            const float p = static_cast<float>(v.limbAcc / 0.05);
            v.limbAmount = v.limbSpdPrev + (v.limbSpd - v.limbSpdPrev) * p;
            v.limbSwing = v.limbPosPrev + (v.limbPos - v.limbPosPrev) * p;
            v.capeFlap = std::clamp(v.groundSpeed / 7.0f, 0.0f, 1.0f) * 0.25f;

            const double pd = static_cast<double>(p);
            const glm::dvec3 cl = v.cloakPrev + (v.cloak - v.cloakPrev) * pd;
            const glm::dvec3 ps = v.tickPosPrev + (v.tickPos - v.tickPosPrev) * pd;
            const double dx = cl.x - ps.x, dy = cl.y - ps.y, dz = cl.z - ps.z;
            const float rad = v.yaw * 0.017453292f;
            const double sn = std::sin(rad), cs = std::cos(rad);
            float flap = std::clamp(static_cast<float>(dy) * 10.0f, -6.0f, 32.0f);
            const float lean = std::clamp(static_cast<float>(dx * sn - dz * cs) * 100.0f, 0.0f, 150.0f);
            const float lean2 = std::clamp(static_cast<float>(-dx * cs - dz * sn) * 100.0f, -20.0f, 20.0f);
            const float bob = v.bobPrev + (v.bob - v.bobPrev) * p;
            const float walk = v.walkDistPrev + (v.walkDist - v.walkDistPrev) * p;
            flap += std::sin(walk * 6.0f) * 32.0f * bob;
            v.capeX = 6.0f + lean / 2.0f + flap;
            v.capeZ = lean2 / 2.0f;

            if (e.isPlayer)
                v.cape.Sample(p, in.capePhysics == 2, walk * 6.0f,
                              in.capePhysics > 0 ? std::clamp(v.groundSpeed / 3.0f, 0.0f, 1.0f) * (std::fabs(v.verticalSpeed) < 2.0f ? 1.0f : 0.0f) : 0.0f, dt);
        }

        const bool usingItem = e.Has(flag::UsingItem) || e.AimingBow();
        if (usingItem && !v.wasUsing)
            v.usingSince = in.time;
        v.wasUsing = usingItem;
        if (e.swingSerial != v.swingSerial) {
            v.swingSerial = e.swingSerial;
            if (v.attackTime <= 0.0f || v.attackTime >= 0.5f)
                v.attackTime = 0.0001f;
        }
        if (static const char* forced = PERLIN_ENV("PERLIN_PREVIEW_ATTACK"); forced && e.isSelf)
            v.attackTime = static_cast<float>(std::atof(forced));
        else if (v.attackTime > 0)
            {
            const float seconds = std::max(1.0f, e.isSelf ? in.ownSwingTicks : 6.0f) / 20.0f;
            v.attackTime = v.attackTime + dt / seconds >= 1.0f ? 0.0f : v.attackTime + dt / seconds;
        }
        if (e.hurtSerial != v.hurtSerial) {
            v.hurtSerial = e.hurtSerial;
            v.hurtTimer = 0.5f;
        }
        v.hurtTimer = std::max(0.0f, v.hurtTimer - dt);
        if (e.dying) {
            v.deathTime = std::min(v.deathTime + dt, 1.0f);
            v.hurtTimer = 0.5f;
        }
        else
            v.deathTime = 0.0f;
        const auto ease = [&](float& value, bool on, float seconds) {
            const float step = dt / seconds;
            value = on ? std::min(1.0f, value + step) : std::max(0.0f, value - step);
        };
        ease(v.standAmount, e.Has(flag::Rearing), 0.5f);
        ease(v.sitAmount, e.Has(flag::Sitting), 0.3f);
        ease(v.swimAmount, e.Has(flag::Swimming), 0.4f);
        ease(v.lieAmount, e.Has(flag::LayingDown), 0.4f);
        v.glideTime = e.Has(flag::Gliding) ? v.glideTime + dt : 0.0f;
        ease(v.swellAmount, e.Has(flag::Ignited), 1.5f);
    }

    void EntityRenderer::RunAnimations(const Entity& e, View& v, const ClientEntityDef& def, Pose& pose, const FrameIn& in,
                                       const attachable::Geometry* bindGeometry) {
        (void)in;
        const attachable::BindLookup restRotation = attachable::RestRotationLookup(bindGeometry);
        const attachable::BindLookup defaultTranslation = attachable::DefaultTranslationLookup(bindGeometry);
        molang::Scope& scope = v.scope;
        const double now = v.now;
        const uint32_t epoch = ++v.animEpoch;
        const auto activate = [&](const std::string& key) {
            auto it = v.animStart.find(key);
            if (it == v.animStart.end())
                it = v.animStart.emplace(key, View::AnimStamp{now, epoch}).first;
            it->second.seen = epoch;
            return it->second.start;
        };
        EntityHost* host = static_cast<EntityHost*>(scope.host);

        std::function<void(const std::string&, float, int, const std::string&)> run;
        std::function<void(const std::string&, float, int, const std::string&)> runId;
        run = [&](const std::string& shortName, float weight, int depth, const std::string& path) {
            if (weight == 0.0f || depth > 4)
                return;
            const auto found = def.animations.find(shortName);
            if (found == def.animations.end())
                return;
            runId(found->second, weight, depth, path);
        };
        runId = [&](const std::string& id, float weight, int depth, const std::string& path) {
            if (weight == 0.0f || depth > 4)
                return;
            if (const auto* controller = assets.animations.FindController(id)) {
                auto& cs = v.ctrl[path + id];
                attachable::StartController(*controller, scope, cs.state, cs.entered, cs.started, now);
                std::string key;
                if (const auto st = controller->states.find(cs.state); st != controller->states.end()) {
                    host->animTime = static_cast<float>(now - cs.entered);
                    {
                        bool all = true, any = false;
                        for (const auto& [animShort, blend] : st->second.animations) {
                            (void)blend;
                            const auto child = def.animations.find(animShort);
                            const Animation* ca = child != def.animations.end() ? assets.animations.Find(child->second) : nullptr;
                            if (!ca) {
                                all = false;
                                continue;
                            }
                            key.assign(path).append(id).append("/").append(cs.state).append("/").append(child->second);
                            const auto startIt = v.animStart.find(key);
                            const double start = startIt != v.animStart.end() ? startIt->second.start : cs.entered;
                            const bool done = ca->loop != Animation::Loop::Yes && ca->length > 0 && now - start >= ca->length;
                            all = all && done;
                            any = any || done;
                        }
                        host->allFinished = all && !st->second.animations.empty();
                        host->anyFinished = any;
                    }
                    attachable::AdvanceController(*controller, scope, cs.state, cs.entered, now);
                }
                const auto st = controller->states.find(cs.state);
                if (st == controller->states.end())
                    return;
                if (PERLIN_ENV("PERLIN_POSE_DEBUG"))
                    std::cout << "[ctrl] " << id << " state " << cs.state << std::endl;
                const std::string childPath = path + id + "/" + cs.state + "/";
                for (const auto& [animShort, blend] : st->second.animations) {
                    const float w = blend ? static_cast<float>(blend->Number(scope)) : 1.0f;
                    run(animShort, w * weight, depth + 1, childPath);
                }
                return;
            }
            const Animation* a = assets.animations.Find(id);
            if (!a)
                return;
            const double start = activate(path + id);
            double since = now - start;
            host->animTime = static_cast<float>(since);
            if (a->animTimeUpdate)
                since = a->animTimeUpdate->Number(scope);
            if (a->length > 0) {
                if (a->loop == Animation::Loop::Yes)
                    since = std::fmod(since, static_cast<double>(a->length));
                else
                    since = std::min(since, static_cast<double>(a->length));
            }
            host->animTime = static_cast<float>(since);
            if (PERLIN_ENV("PERLIN_POSE_DEBUG"))
                std::cout << "[anim] " << e.type << " " << id << " w=" << weight << std::endl;
            attachable::Apply(*a, scope, static_cast<float>(since), weight, pose, restRotation, defaultTranslation);
        };
        for (const auto& entry : def.animate) {
            const float w = entry.weight ? static_cast<float>(entry.weight->Number(scope)) : 1.0f;
            if (entry.name.rfind("ctrl:", 0) == 0) {
                if (const auto c = def.controllers.find(entry.name.substr(5)); c != def.controllers.end())
                    runId(c->second, w, 0, "");
            } else {
                run(entry.name, w, 0, "");
            }
        }
        for (auto it = v.animStart.begin(); it != v.animStart.end();)
            it = it->second.seen == epoch ? std::next(it) : v.animStart.erase(it);
        (void)e;
    }

    bool EntityRenderer::Build(const Entity& e, View& v, const FrameIn& in, std::vector<Layer>& layers, float& outScale) {
        struct FpPoseGuard { FpPoseGuard(bool on) { attachable::gFirstPersonPose = on; } ~FpPoseGuard() { attachable::gFirstPersonPose = false; } } fpPoseGuard(v.fp);
        const ClientEntityDef* def = assets.Entity(e.type);
        if (!def)
            return false;
        EntityHost host;
        host.e = &e;
        host.dt = v.dt;
        host.now = in.time;
        host.limbSwing = v.limbSwing;
        host.limbAmount = v.limbAmount;
        host.groundSpeed = v.groundSpeed;
        host.verticalSpeed = v.verticalSpeed;
        host.capeFlap = v.capeFlap;
        host.bodyYaw = v.yaw;
        host.headYaw = v.headYaw;
        host.pitch = v.pitch;
        host.life = static_cast<float>(in.time - v.born);
        host.dx = static_cast<float>(v.pos.x - v.lastPos.x) * 20.0f;
        host.dy = static_cast<float>(v.pos.y - v.lastPos.y) * 20.0f;
        host.dz = static_cast<float>(v.pos.z - v.lastPos.z) * 20.0f;
        host.yawSpeed = v.yawSpeed;
        host.standAmount = v.standAmount;
        host.sitAmount = v.sitAmount;
        host.swimAmount = v.swimAmount;
        host.lieAmount = v.lieAmount;
        host.swellAmount = v.swellAmount;
        host.hurtTime = v.hurtTimer * 20.0f;
        host.deathTicks = v.deathTime * 20.0f;
        host.pos = v.pos;
        host.camera = in.camera;
        host.cameraYaw = in.cameraYaw;
        host.cameraPitch = in.cameraPitch;
        host.firstPerson = v.fp;
        {
            const std::string& usedItem = e.mainHand;
            float maxUse = 72000.0f;
            if (usedItem == "minecraft:crossbow") maxUse = 25.0f;
            else if (HandRenderer::IsFood(usedItem)) maxUse = 32.0f;
            else if (usedItem == "minecraft:spyglass") maxUse = 1200.0f;
            host.maxUseTicks = maxUse;
            if (v.fp && fp) {
                host.using_ = fp->using_;
                host.usedTicks = fp->usedTicks;
                host.blocking = fp->blocking;
                host.offArmHeight = fp->offArmHeight;
            } else {
                host.using_ = e.Has(flag::UsingItem) || e.AimingBow();
                host.usedTicks = host.using_ ? static_cast<float>((v.now - v.usingSince) * 20.0) : 0.0f;
                host.blocking = host.using_ && (usedItem == "minecraft:shield" || e.offHand == "minecraft:shield");
            }
        }
        host.timeOfDay = std::fmod(in.worldTicks, 24000.0) / 24000.0;
        host.day = std::floor(in.worldTicks / 24000.0);
        v.scope.host = &host;
        v.scope.queryCache.clear();
        v.scope.cacheQueries = true;
        struct CacheGuard { molang::Scope& sc; ~CacheGuard() { sc.cacheQueries = false; sc.queryCache.clear(); } } cacheGuard{v.scope};
        v.hasArmR = v.hasArmL = false;
        auto& vars = v.scope.variables;
        vars["attack_time"] = Value::Num(v.attackTime);
        vars["gliding_speed_value"] = Value::Num(1.0);
        vars["is_first_person"] = Value::Num(v.fp ? 1 : 0);
        if (v.fp && fp) {
            vars["attack_time"] = Value::Num(fp->attackTime);
            vars["player_arm_height"] = Value::Num(fp->armHeight);
            vars["bob_animation"] = Value::Num(fp->bob ? 1 : 0);
            vars["short_arm_offset_right"] = Value::Num(0);
            vars["short_arm_offset_left"] = Value::Num(0);
            vars["is_using_vr"] = Value::Num(0);
            vars["is_vertical_splitscreen"] = Value::Num(0);
            vars["is_horizontal_splitscreen"] = Value::Num(0);
        }
        vars["map_face_icon"] = Value::Num(0);
        vars["player_x_rotation"] = Value::Num(v.pitch);
        vars["is_paperdoll"] = Value::Num(0);
        vars["is_brandishing_spear"] = Value::Num(0);
        if (!v.initialised) {
            v.initialised = true;
            for (const auto& s : def->initialize)
                s->Eval(v.scope);
        }
        for (const auto& s : def->preAnimation)
            s->Eval(v.scope);
        if (e.isPlayer) {
            vars["swim_amount"] = Value::Num(v.swimAmount);
            vars["is_holding_right"] = Value::Num(e.mainHand.empty() ? 0.0 : 1.0);
            vars["is_holding_left"] = Value::Num(e.offHand.empty() ? 0.0 : 1.0);
            vars["is_holding_spyglass"] = Value::Num(e.Has(flag::UsingItem) && e.mainHand == "minecraft:spyglass" ? 1.0 : 0.0);
            vars["is_tooting_goat_horn"] = Value::Num(e.Has(flag::UsingItem) && e.mainHand == "minecraft:goat_horn" ? 1.0 : 0.0);
            vars["is_using_brush"] = Value::Num(e.Has(flag::UsingItem) && e.mainHand == "minecraft:brush" ? 1.0 : 0.0);
        }

        const attachable::Geometry* bindGeometry = nullptr;
        for (const auto& rcRef : def->renderControllers) {
            if (rcRef.condition && !rcRef.condition->Eval(v.scope).Truthy())
                continue;
            const RenderControllerDef* rc = assets.RenderController(rcRef.id);
            if (!rc || !rc->geometry)
                continue;
            v.scope.arrays.clear();
            for (const auto& [name, values] : rc->arrays)
                v.scope.arrays[name] = values;
            const Value g = rc->geometry->Eval(v.scope);
            const auto id = def->geometries.find(AfterDot(g.isString ? g.s : "geometry.default"));
            if (id != def->geometries.end())
                bindGeometry = assets.geometries.Find(id->second);
            if (bindGeometry)
                break;
        }
        Pose pose;
        host.bonePivot = [bindGeometry](const std::string& bone, int axis, double& out) {
            if (!bindGeometry || axis < 0 || axis > 2)
                return false;
            std::string lower = bone;
            for (char& ch : lower)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            for (const auto& b : bindGeometry->bones)
                if (b.name == lower) {
                    out = b.pivot[axis];
                    return true;
                }
            return false;
        };
        RunAnimations(e, v, *def, pose, in, bindGeometry);
        if (PERLIN_ENV("PERLIN_POSE_DEBUG"))
            for (const auto& [bn, xf] : pose)
                std::cout << "[pose] " << e.type << " " << bn << " rot " << xf.rotation.x << "," << xf.rotation.y << "," << xf.rotation.z
                          << " pos " << xf.position.x << "," << xf.position.y << "," << xf.position.z << std::endl;
        if (e.isPlayer && e.Has(flag::Riding) && v.swimAmount < 0.01f && v.lieAmount < 0.01f) {
            pose["rightleg"].rotation = {-81.0f, -18.0f, -4.5f};
            pose["leftleg"].rotation = {-81.0f, 18.0f, 4.5f};
            pose["rightarm"].rotation.x += -36.0f;
            pose["leftarm"].rotation.x += -36.0f;
        }
        if (e.isPlayer && !v.fp && e.Has(flag::Sneaking) && v.swimAmount < 0.01f && v.lieAmount < 0.01f && !e.Has(flag::Riding))
            ApplyJavaCrouch(pose, bindGeometry);
        if (e.isPlayer && e.isSelf) {
            const SkinParts& parts = in.ownParts;
            const auto hide = [&](bool shown, const char* bone) {
                if (!shown)
                    pose[bone].scale = glm::vec3(0.0f);
            };
            hide(parts.hat, "hat");
            hide(parts.jacket, "jacket");
            hide(parts.leftSleeve, "leftsleeve");
            hide(parts.rightSleeve, "rightsleeve");
            hide(parts.leftPants, "leftpants");
            hide(parts.rightPants, "rightpants");
        }
        if (e.isPlayer) {
            auto& r = pose["cape"].rotation;
            float simulated = 0.0f;
            for (const float a : v.cape.pitch)
                simulated += a;
            simulated /= static_cast<float>(CapeChain::kSegments);
            r.x = -std::max(v.capeX, simulated);
            r.z = -v.capeZ;
            r.y = v.capeZ;
        }
        if (e.isPlayer && in.capePhysics > 0) {
            pose["cape"].rotation = glm::vec3(0.0f);
            float previousPitch = 0.0f, previousRoll = 0.0f;
            for (int i = 0; i < CapeChain::kSegments; i++) {
                auto& part = pose["part" + std::to_string(i + 1)].rotation;
                part = glm::vec3(-(v.cape.pitch[i] - previousPitch), 0.0f, -(v.cape.roll[i] - previousRoll));
                previousPitch = v.cape.pitch[i];
                previousRoll = v.cape.roll[i];
            }
            const float turned = std::sin(WrapDegrees(v.yaw - v.headYaw) * 0.017453292f);
            pose["shoulders"].rotation = glm::vec3(0.0f, std::clamp(v.limbAmount, 0.0f, 1.0f) * turned * turned * turned * 60.0f, 0.0f);
        }
        if (v.fp) {
            static const float px = PERLIN_ENV("PERLIN_FPX") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_FPX"))) : 1.0f;
            for (auto& [boneName, xf] : pose)
                xf.position.x *= px;
        }

        float scale = def->scale ? static_cast<float>(def->scale->Number(v.scope)) : 1.0f;
        if (scale <= 0.0f)
            scale = 1.0f;
        if (e.scale > 0.0f && !e.isPlayer)
            scale *= e.scale;
        outScale = scale;

        const attachable::GeometryLibrary* library = &assets.geometries;
        std::string playerGeometry;
        if (e.isPlayer && e.skin) {
            const SkinData& s = *e.skin;
            skinOwners[s.serial] = e.skin;
            if (!s.geometryJson.empty()) {
                auto& g = skinGeometry[s.serial];
                if (!g) {
                    g = std::make_unique<attachable::GeometryLibrary>();
                    g->Load(s.geometryJson);
                }
                if (g->Size() > 0) {
                    library = g.get();
                    playerGeometry = s.geometryName;
                }
            }
            if (playerGeometry.empty() && s.slim)
                playerGeometry = "geometry.humanoid.customSlim";
        }

        const auto addLayer = [&](const std::string& controllerId, bool allowCapeOnly) {
            (void)allowCapeOnly;
            const RenderControllerDef* rc = assets.RenderController(controllerId);
            if (!rc) {
                return;
            }
            v.scope.arrays.clear();
            for (const auto& [name, values] : rc->arrays)
                v.scope.arrays[name] = values;
            std::string geometryRef = "geometry.default", textureRef = "texture.default";
            if (rc->geometry) {
                const Value g = rc->geometry->Eval(v.scope);
                if (g.isString)
                    geometryRef = g.s;
            }
            if (!rc->textures.empty()) {
                const Value t = rc->textures.front()->Eval(v.scope);
                if (t.isString)
                    textureRef = t.s;
            }
            const std::string geoKey = AfterDot(geometryRef), texKey = AfterDot(textureRef);
            const auto geoId = def->geometries.find(geoKey);
            if (geoId == def->geometries.end())
                return;
            std::string geometryId = geoId->second;
            const attachable::GeometryLibrary* lib = &assets.geometries;
            if (e.isPlayer && geoKey == "default" && !playerGeometry.empty()) {
                geometryId = playerGeometry;
                lib = library;
            }
            const attachable::Geometry* geometry = lib->Find(geometryId);
            if (!geometry && lib != &assets.geometries)
                geometry = assets.geometries.Find(def->geometries.at(geoKey));
            if (!geometry)
                geometry = assets.geometries.Find(geoId->second);
            if (!geometry) {
                if (!warned[geometryId]) {
                    warned[geometryId] = true;
                    std::cout << "[entity] geometry " << geometryId << " not found for " << e.type << std::endl;
                }
                return;
            }
            if (in.skins3d && e.isPlayer && e.skin && texKey == "default" && !e.skin->skinRgba.empty() && geometry &&
                glm::distance(e.pos, in.playerPos) < 16.0) {
                auto& voxel = voxelGeometry[e.skin->serial];
                if (!voxel)
                    voxel = std::make_unique<attachable::Geometry>(
                        attachable::VoxelizeSkinLayers(*geometry, e.skin->skinRgba.data(), e.skin->skinWidth, e.skin->skinHeight));
                geometry = voxel.get();
            }
            bool physicsCape = false;
            if (controllerId == "controller.render.player.cape" && e.isPlayer && in.capePhysics > 0) {
                if (capeGeometries.Size() == 0) {
                    capeGeometries.Load(capegeo::kNoShoulder);
                    capeGeometries.Load(capegeo::kShoulder);
                }
                if (const attachable::Geometry* physics = capeGeometries.Find(in.capePhysics == 2 ? "geometry.cape_physics_shoulder" : "geometry.cape_physics_plain")) {
                    geometry = physics;
                    geometryId = in.capePhysics == 2 ? "geometry.cape_physics_shoulder" : "geometry.cape_physics_plain";
                    physicsCape = true;
                }
            }
            if (!allowCapeOnly && !v.hasArmR && !v.hasArmL) {
                const glm::mat4 toJava = glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, -16.0f, 16.0f));
                glm::mat4 w;
                glm::vec3 pv;
                if (attachable::BoneWorld(*geometry, pose, "rightarm", w, pv)) {
                    v.armR = w * glm::translate(glm::mat4(1.0f), pv) * toJava;
                    v.hasArmR = true;
                }
                if (attachable::BoneWorld(*geometry, pose, "leftarm", w, pv)) {
                    v.armL = w * glm::translate(glm::mat4(1.0f), pv) * toJava;
                    v.hasArmL = true;
                }
                v.hasItemR = attachable::BoneWorld(*geometry, pose, "rightitem", v.itemR, pv);
                if (v.hasItemR)
                    v.itemR = v.itemR * glm::translate(glm::mat4(1.0f), pv);
                v.hasItemL = attachable::BoneWorld(*geometry, pose, "leftitem", v.itemL, pv);
                if (v.hasItemL)
                    v.itemL = v.itemL * glm::translate(glm::mat4(1.0f), pv);
            }
            Layer layer;
            if (e.isPlayer && e.skin && (texKey == "default" || texKey == "cape")) {
                if (texKey == "cape" && e.skin->capeWidth <= 0)
                    return;
                layer.texture = texKey == "cape" ? (physicsCape ? "#cape" : "#capemodel") : "#skin";
            } else {
                const auto tp = def->textures.find(texKey);
                if (tp == def->textures.end())
                    return;
                layer.texture = tp->second;
            }
            std::vector<std::pair<std::string, bool>> explicitVisibility;
            bool defaultVisible = true;
            std::vector<std::pair<std::string, bool>> rules;
            for (const auto& part : rc->parts)
                rules.emplace_back(part.pattern, part.visible ? part.visible->Eval(v.scope).Truthy() : true);
            (void)explicitVisibility;
            (void)defaultVisible;
            const attachable::BoneFilter filter = rules.empty() ? attachable::BoneFilter() : attachable::BoneFilter([&](const std::string& bone) {
                bool visible = true;
                for (const auto& [pattern, on] : rules)
                    if (Matches(pattern, bone))
                        visible = on;
                return visible;
            });
            std::vector<std::pair<std::string, std::string>> materialRules;
            for (const auto& r : rc->materials) {
                if (!r.value)
                    continue;
                const Value mv = r.value->Eval(v.scope);
                materialRules.emplace_back(r.pattern, LowerCase(AfterDot(mv.isString ? mv.s : "default")));
            }
            const auto materialOf = [&](const std::string& bone) {
                std::string key = "default";
                for (const auto& [pattern, material] : materialRules)
                    if (Matches(pattern, bone))
                        key = material;
                return key;
            };
            std::vector<std::string> groups;
            if (materialRules.empty())
                groups.push_back("default");
            else
                for (const auto& bone : geometry->bones) {
                    const std::string key = materialOf(bone.name);
                    if (std::find(groups.begin(), groups.end(), key) == groups.end())
                        groups.push_back(key);
                }
            if (groups.size() > 6)
                groups.resize(6);
            if (rc->hasColor) {
                for (int i = 0; i < 4; i++) {
                    v.scope.thisValue = 1.0f;
                    layer.tint[i] = rc->color[i] ? static_cast<float>(rc->color[i]->Number(v.scope)) : 1.0f;
                }
            }
            layer.translucent = rc->translucent;
            for (const std::string& group : groups) {
                const bool single = groups.size() == 1;
                const attachable::BoneFilter groupFilter = single ? filter : attachable::BoneFilter([&](const std::string& bone) {
                    return materialOf(bone) == group && (!filter || filter(bone));
                });
                attachable::BuiltMesh mesh;
                attachable::BuildMesh(*geometry, pose, nullptr, mesh, groupFilter, nullptr, true);
                if (mesh.body.empty())
                    continue;
                static const bool debug = PERLIN_ENV("PERLIN_ENTITY_DEBUG") != nullptr;
                if (debug && !warned["dbg:" + e.type + controllerId + group]) {
                    warned["dbg:" + e.type + controllerId + group] = true;
                    std::cout << "[entity] " << e.type << " controller " << controllerId << " geometry " << geometryId << " texture "
                              << layer.texture << " material " << group << " quads " << mesh.body.size() << " (bones "
                              << geometry->bones.size() << ")" << std::endl;
                }
                Layer copy = layer;
                const auto named = def->materials.find(group);
                copy.material = assets.materialLibrary.Find(named != def->materials.end() ? named->second : group);
                if (copy.material.colorMask)
                    copy.maskRgb = DyeColorRgb(e.color);
                copy.quads = std::move(mesh.body);
                layers.push_back(std::move(copy));
            }
        };

        for (const auto& rcRef : def->renderControllers) {
            if (rcRef.condition && !rcRef.condition->Eval(v.scope).Truthy())
                continue;
            addLayer(rcRef.id, false);
        }
        if (e.isPlayer && e.skin && e.skin->capeWidth > 0 && !e.Has(flag::Invisible) && !v.fp && e.armor[1] != "minecraft:elytra" && (!e.isSelf || in.ownParts.cape))
            addLayer("controller.render.player.cape", true);
        if (itemRenderer && e.isPlayer) {
            const auto bind = [&](const std::string& name, const glm::mat4& boneWorld, bool left) {
                if (name.empty() || (v.fp && name != "minecraft:bow"))
                    return;
                static const bool bedrockAttach = PERLIN_ENV("PERLIN_BEDROCK_ATTACH") != nullptr;
                if (!v.fp && !bedrockAttach && (name == "minecraft:bow" || name == "minecraft:crossbow"))
                    return;
                attachable::Frame frame;
                if (!itemRenderer->ThirdPersonAttachable(name, left, v.fp, e.Has(flag::Sneaking), host.using_ && !left, host.usedTicks,
                                                         host.blocking, static_cast<double>(in.time - v.born), e.mainHand, e.offHand,
                                                         frame))
                    return;
                for (const attachable::Part& part : frame.parts) {
                    Layer layer;
                    layer.texture = part.texture;
                    static const glm::vec3 fpScale = [] {
                        glm::vec3 f(1.0f, 1.0f, 1.0f);
                        if (const char* e = PERLIN_ENV("PERLIN_FPF"))
                            std::sscanf(e, "%f,%f,%f", &f.x, &f.y, &f.z);
                        return f;
                    }();
                    const glm::mat4 fpFix = v.fp ? glm::scale(glm::mat4(1.0f), fpScale) : glm::mat4(1.0f);
                    const glm::mat4 bone = (part.texture.rfind("textures/entity/", 0) == 0
                                               ? boneWorld * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, kAttachDrop, 0.0f))
                                               : boneWorld) * fpFix;
                    layer.material = assets.materialLibrary.Find("entity_alphatest");
                    layer.quads = part.quads;
                    for (attachable::Quad& q : layer.quads) {
                        for (glm::vec3& p : q.p)
                            p = glm::vec3(bone * glm::vec4(p, 1.0f));
                        q.n = glm::normalize(glm::mat3(bone) * q.n);
                    }
                    layer.body = false;
                    layers.push_back(std::move(layer));
                }
                v.attachedItem[left ? 1 : 0] = true;
            };
            v.attachedItem[0] = v.attachedItem[1] = false;
            if (v.hasItemR)
                bind(e.mainHand, v.itemR, false);
            if (v.hasItemL)
                bind(e.offHand, v.itemL, true);
        }
        if (itemRenderer && !layers.empty() && !v.fp) {
            static const int kOrder[4] = {0, 1, 2, 3};
            for (int slot : kOrder) {
                const std::string& item = e.armor[slot];
                if (item.empty())
                    continue;
                std::string geometryId, texture;
                bool found = false;
                if (e.isPlayer)
                    found = itemRenderer->ArmorAttachable(item + ".player", geometryId, texture);
                if (!found)
                    found = itemRenderer->ArmorAttachable(item, geometryId, texture);
                if (PERLIN_ENV("PERLIN_ARMOR_DEBUG"))
                    std::cout << "[armor] " << item << " found=" << found << " geo=" << geometryId << " tex=" << texture << std::endl;
                if (!found)
                    continue;
                const attachable::Geometry* geometry = assets.geometries.Find(geometryId);
                if (!geometry && library)
                    geometry = library->Find(geometryId);
                if (!geometry) {
                    if (!warned["armor:" + geometryId]) {
                        warned["armor:" + geometryId] = true;
                        std::cout << "[entity] armour geometry " << geometryId << " not found" << std::endl;
                    }
                    continue;
                }
                attachable::Pose elytraPose;
                const attachable::Pose* meshPose = &pose;
                if (item == "minecraft:elytra") {
                    elytraPose = pose;
                    elytraPose["body"].scale *= 1.067f;
                    glm::vec3 pl{4.5f, 4.0f, -2.0f}, rl{15.0f, 0.0f, -13.0f};
                    if (e.Has(flag::Gliding)) {
                        float ratio = 1.0f;
                        const float vy = v.verticalSpeed;
                        const float len = std::sqrt(v.groundSpeed * v.groundSpeed + vy * vy);
                        if (vy < 0.0f && len > 1e-4f)
                            ratio = 1.0f - std::pow(std::min(1.0f, -vy / len), 1.5f);
                        pl = {2.0f, 2.0f, -2.0f};
                        rl = {15.0f + 5.0f * ratio, 0.0f, -15.0f - 75.0f * ratio};
                    } else if (e.Has(flag::Swimming)) {
                        pl = {5.0f, 2.0f, -2.0f};
                        rl = {v.swimAmount * 15.0f, 0.0f, -15.0f};
                    } else if (e.Has(flag::Sneaking)) {
                        pl = {2.0f, 3.0f, -2.0f};
                        rl = {20.0f, -15.0f, -45.0f};
                    }
                    const glm::vec3 pr{-pl.x, pl.y, pl.z};
                    const glm::vec3 rr{rl.x, -rl.y, -rl.z};
                    elytraPose["left_wing"] = {pl, rl, glm::vec3(1.0f, 1.0f, 2.0f)};
                    elytraPose["right_wing"] = {pr, rr, glm::vec3(1.0f, 1.0f, 2.0f)};
                    meshPose = &elytraPose;
                }
                attachable::BuiltMesh mesh;
                glm::mat4 rootWorld(1.0f);
                glm::vec3 rootPivot(0.0f);
                bool haveRoot = false;
                if (bindGeometry && e.isPlayer)
                    haveRoot = attachable::BoneWorld(*bindGeometry, pose, "root", rootWorld, rootPivot);
                attachable::BuildMesh(*geometry, *meshPose, nullptr, mesh, attachable::BoneFilter(), haveRoot ? &rootWorld : nullptr, true);
                if (PERLIN_ENV("PERLIN_ARMOR_DEBUG"))
                {
                    glm::vec3 lo(1e9f), hi(-1e9f);
                    for (const auto& q : mesh.body)
                        for (const auto& pt : q.p) { lo = glm::min(lo, pt); hi = glm::max(hi, pt); }
                    std::cout << "[armor]   quads=" << mesh.body.size() << " bones=" << geometry->bones.size() << " lo=" << lo.x << "," << lo.y << "," << lo.z << " hi=" << hi.x << "," << hi.y << "," << hi.z << std::endl;
                }
                if (mesh.body.empty())
                    continue;
                Layer layer;
                layer.texture = (item == "minecraft:elytra" && e.isPlayer && e.skin && e.skin->capeWidth > 0) ? std::string("#cape") : texture;
                layer.material = assets.materialLibrary.Find("entity_alphatest");
                layer.quads = std::move(mesh.body);
                layer.body = false;
                layers.push_back(std::move(layer));
            }
        }
        v.scope.host = nullptr;
        return !layers.empty();
    }

    namespace { double gEntityProf[3] = {}; int gEntityProfFrames = 0; }

    void EntityRenderer::Draw(const Entity& e, const View& v, const std::vector<Layer>& layers, float scale, const FrameIn& in) {
        static const glm::vec3 l0 = glm::normalize(glm::vec3(0.2f, 1.0f, -0.7f));
        static const glm::vec3 l1 = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.7f));
        bool billboard = false;
        if (const ClientEntityDef* bd = assets.Entity(e.type))
            for (const auto& [shortName, id] : bd->animations)
                if (id == "animation.actor.billboard") {
                    billboard = true;
                    break;
                }
        const bool mirrored = !fpModel && !billboard;
        const float rotation = (fpModel || billboard) ? 180.0f : -v.yaw;
        const glm::mat4 mirrorZ = mirrored ? glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f)) : glm::mat4(1.0f);
        const glm::mat3 rot = glm::mat3(glm::rotate(glm::mat4(1.0f), rotation * kDeg, glm::vec3(0, 1, 0)) * mirrorZ);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(v.pos));
        model = glm::rotate(model, rotation * kDeg, glm::vec3(0, 1, 0)) * mirrorZ;
        if (v.deathTime > 0.0f) {
            const float t = v.deathTime * 20.0f;
            const float f = std::min(std::sqrt(std::max(0.0f, (t - 1.0f) / 20.0f * 1.6f)), 1.0f);
            model = glm::translate(model, glm::vec3(0, 0.0f, 0));
            model = glm::rotate(model, f * 90.0f * kDeg, glm::vec3(0, 0, 1));
        }
        if (e.sleeping && e.isPlayer)
            model = glm::rotate(model, -90.0f * kDeg, glm::vec3(1, 0, 0));
        if (v.glideTime > 0.0f && e.isPlayer) {
            const float t = std::min(v.glideTime * 20.0f, 10.0f);
            const float amount = std::clamp(t * t / 100.0f, 0.0f, 1.0f);
            model = glm::rotate(model, -amount * (90.0f + v.pitch) * kDeg, glm::vec3(1, 0, 0));
        }
        if (e.isSelf && (in.ownPose.x != 0.0f || in.ownPose.y != 0.0f || in.ownPose.z != 0.0f)) {
            model = glm::translate(model, glm::vec3(0.0f, 0.9f, 0.0f));
            model = glm::rotate(model, in.ownPose.y * kDeg, glm::vec3(0, 1, 0));
            model = glm::rotate(model, in.ownPose.x * kDeg, glm::vec3(1, 0, 0));
            model = glm::rotate(model, in.ownPose.z * kDeg, glm::vec3(0, 0, 1));
            model = glm::translate(model, glm::vec3(0.0f, -0.9f, 0.0f));
        }
        if (e.type == "minecraft:arrow")
            model = glm::rotate(model, 180.0f * kDeg, glm::vec3(0, 1, 0));
        if (e.type == "minecraft:thrown_trident") {
            model = glm::rotate(model, (90.0f + v.pitch) * kDeg, glm::vec3(1, 0, 0));
            model = glm::translate(model, glm::vec3(0.0f, -12.0f / 16.0f * scale, 0.0f));
        }
        // lobbed items tumble around x/z instead of holding a fixed pose
        if (e.type == "minecraft:xp_bottle" || e.type == "minecraft:splash_potion" ||
            e.type == "minecraft:lingering_potion" || e.type == "minecraft:snowball" || e.type == "minecraft:egg" ||
            e.type == "minecraft:ender_pearl") {
            const float spin = std::fmod(static_cast<float>((in.time - v.born) * 10800.0), 360.0f);
            model = glm::rotate(model, spin * kDeg, glm::vec3(1, 0, 0));
        }
        model = glm::scale(model, glm::vec3(scale / 16.0f) * modScale);
        glm::vec3 light(1.0f);
        if (in.light)
            light = in.light(v.pos + glm::dvec3(0.0, 1.0, 0.0));
        if (fpModel) {
            model = *fpModel;
            if (fp)
                light = fp->light;
        }
        if (dollModel) {
            model = *dollModel * glm::rotate(glm::mat4(1.0f), rotation * kDeg, glm::vec3(0, 1, 0)) * mirrorZ * glm::scale(glm::mat4(1.0f), glm::vec3(scale / 16.0f));
            light = glm::vec3(1.0f);
        }
        const float hurt = v.hurtTimer > 0.0f ? std::min(1.0f, v.hurtTimer / 0.5f) : 0.0f;

        std::vector<const Layer*> ordered;
        for (const Layer& l : layers)
            ordered.push_back(&l);
        std::stable_sort(ordered.begin(), ordered.end(), [](const Layer* a, const Layer* b) {
            return static_cast<int>(a->material.blend) < static_cast<int>(b->material.blend);
        });
        // invisibility hides the body (players and mobs); armor and held items stay, as in both editions
        const bool hideBody = e.Has(flag::Invisible) && !v.fp && !dollModel;
        for (const Layer* layerPtr : ordered) {
            const Layer& layer = *layerPtr;
            if (hideBody && layer.body)
                continue;
            uint32_t texture = 0;
            if (layer.texture == "#skin")
                texture = TextureFor(e, "default");
            else if (layer.texture == "#cape")
                texture = TextureFor(e, "cape");
            else if (layer.texture == "#capemodel")
                texture = TextureFor(e, "capemodel");
            else {
                char maskKey[16] = "";
                if (layer.material.colorMask)
                    std::snprintf(maskKey, sizeof(maskKey), "#mask%06x", layer.maskRgb & 0xFFFFFFu);
                const std::string key = layer.texture + maskKey;
                auto it = textures.find(key);
                if (it == textures.end()) {
                    Texture t;
                    uint32_t id = 0;
                    if (ui::UiAssets::Instance().Image(layer.texture, t) && t.width > 0) {
                        if (layer.material.colorMask)
                            BakeColorMask(t, layer.maskRgb);
                        id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
                    }
                    else if (!warned[layer.texture]) {
                        warned[layer.texture] = true;
                        std::cout << "[entity] texture " << layer.texture << " not found" << std::endl;
                    }
                    it = textures.emplace(key, id).first;
                }
                texture = it->second;
            }
            if (PERLIN_ENV("PERLIN_ENTITY_DEBUG") && !warned["draw:" + e.type + layer.texture]) {
                warned["draw:" + e.type + layer.texture] = true;
                std::cout << "[entity]   draw " << e.type << " tex " << layer.texture << " id " << texture << " scale " << scale << " pos "
                          << v.pos.x << "," << v.pos.y << "," << v.pos.z << " quads " << layer.quads.size() << std::endl;
            }
            if (!texture)
                continue;
            std::vector<UiVertex> vertices;
            std::vector<uint32_t> indices;
            vertices.reserve(layer.quads.size() * 4);
            indices.reserve(layer.quads.size() * 6);
            const float layerHurt = layer.material.hurtTint ? hurt : 0.0f;
            const bool fullbright = layer.material.emissive == 2;
            const glm::vec3 tint(layer.tint[0], layer.tint[1], layer.tint[2]);
            for (const attachable::Quad& q : layer.quads) {
                const glm::vec3 n = rot * q.n;
                const float lit = std::min(1.0f, (std::max(0.0f, glm::dot(n, l0)) + std::max(0.0f, glm::dot(n, l1))) * 0.6f + 0.4f);
                const uint32_t color = fullbright ? PackColor(tint, layer.tint[3]) : PackColor(light * lit * tint, layer.tint[3]);
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                for (int i = 0; i < 4; i++)
                    vertices.push_back({q.p[i].x, q.p[i].y, q.p[i].z, color, q.uv[i].x, q.uv[i].y});
                for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                    indices.push_back(base + i);
            }
            const uint32_t mesh = renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(),
                                                         static_cast<uint32_t>(indices.size()));
            if (PERLIN_ENV("PERLIN_ENTITY_DEBUG") && !warned["mesh:" + e.type]) {
                warned["mesh:" + e.type] = true;
                std::cout << "[entity]   mesh " << mesh << " verts " << vertices.size() << " v0 " << vertices[0].x << "," << vertices[0].y << ","
                          << vertices[0].z << " uv " << vertices[0].u << "," << vertices[0].v << " color " << std::hex << vertices[0].abgr
                          << std::dec << std::endl;
            }
            if (!mesh)
                continue;
            pendingMeshes.push_back(mesh);
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = texture;
            std::copy_n(&model[0][0], 16, draw.model);
            draw.premultiplied = false;
            using Blend = MaterialInfo::Blend;
            draw.alphaCutoff = layer.material.blend == Blend::AlphaTest ? 0.1f
                               : layer.material.blend == Blend::Opaque && layer.material.emissive == 0 ? 0.0f
                               : layer.material.blend == Blend::Opaque ? 0.0f : 0.01f;
            if (layer.material.blend == Blend::AlphaTest || layer.material.blend == Blend::Opaque)
                draw.blend = 0;
            else
                draw.blend = layer.material.blend == Blend::Additive ? 2 : 1;
            draw.material = DrawMaterial::Entity;
            draw.colorMask = layer.material.colorMask;
            draw.emissive = layer.material.emissive;
            if (layerHurt > 0.0f) {
                draw.overlay[0] = 1.0f; draw.overlay[1] = 0.0f; draw.overlay[2] = 0.0f; draw.overlay[3] = 0.3f;
            }
            draw.depthWrite = layer.material.depthWrite && draw.blend != 2;
            draw.depth = layer.material.depthTest;
            draw.depthFunc = layer.material.depthFunc;
            draw.colorWrite = layer.material.colorWrite;
            draw.alphaWrite = layer.material.alphaWrite;
            if (layer.material.customBlend && layer.material.blend != Blend::AlphaTest && layer.material.blend != Blend::Opaque) {
                draw.customBlend = true;
                draw.blendSrc = layer.material.blendSrc;
                draw.blendDst = layer.material.blendDst;
            }
            if (fpModel && PERLIN_ENV("PERLIN_FP_DEBUG"))
                std::cout << "[fp] submit mesh=" << mesh << " tex=" << texture << " blend=" << draw.blend << " cutoff=" << draw.alphaCutoff << " verts=" << vertices.size() << " c=" << std::hex << vertices[0].abgr << std::dec << std::endl;
            if (dollModel && dollClip) {
                draw.scissor = true;
                std::copy_n(dollClip, 4, draw.scissorRect);
            }
            draw.shadowOnly = shadowOnlyPass;
            renderer->UiSubmit(dollModel ? UiLayer::Doll : fpModel ? UiLayer::Hand : UiLayer::World, draw);
        }
        if (itemRenderer && !fpModel && (!dollModel || dollClip)) {
            const auto held = [&](const std::string& name, const glm::mat4& arm, bool left) {
                if (name.empty())
                    return;
                ui::HudItem item;
                item.name = name;
                item.count = 1;
                item.maxDamage = ui::MaxDurability(name);
                const float bowTicks = !left && e.AimingBow() ? static_cast<float>((v.now - v.usingSince) * 20.0) : -1.0f;
                itemRenderer->DrawHeldItem(item, model * arm, left, light, bowTicks);
            };
            itemRenderer->SetWorldShadowOnly(shadowOnlyPass);
            itemRenderer->SetDollClip(dollModel ? dollClip : nullptr);
            if (v.hasArmR && !v.attachedItem[0])
                held(e.mainHand, v.armR, false);
            if (v.hasArmL && !v.attachedItem[1])
                held(e.offHand, v.armL, true);
            itemRenderer->SetDollClip(nullptr);
            itemRenderer->SetWorldShadowOnly(false);
        }
    }

    void EntityRenderer::RenderFirstPerson(const Entity& e, const FrameIn& in, const FpState& state) {
        if (!renderer)
            return;
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (root.empty())
            return;
        assets.Configure(root);
        auto& slot = views[e.runtimeId];
        if (!slot)
            slot = std::make_unique<View>();
        View& v = *slot;
        v.lastSeen = in.time;
        v.fp = true;
        fp = &state;
        Track(e, v, in);
        std::vector<Layer> layers;
        float scale = 1.0f;
        const bool built = Build(e, v, in, layers, scale);
        if (PERLIN_ENV("PERLIN_FP_DEBUG"))
            std::cout << "[fp] built=" << built << " layers=" << layers.size() << " main='" << e.mainHand << "'" << std::endl;
        if (built) {
            const float eyeBlocks = e.Has(flag::Sneaking) ? 1.27f : 1.62f;
            static const float eyeOverride = PERLIN_ENV("PERLIN_FPEYE") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_FPEYE"))) : 20.0f;
            static const bool mirror = PERLIN_ENV("PERLIN_FPNOMIR") == nullptr;
            const float eyePx = eyeOverride > 0 ? eyeOverride : eyeBlocks * 16.0f / scale;
            const glm::mat4 flip = mirror ? glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f))
                                          : glm::rotate(glm::mat4(1.0f), 3.14159265f, glm::vec3(0, 1, 0));
            glm::mat4 m = state.viewEffect * flip *
                          glm::scale(glm::mat4(1.0f), glm::vec3(scale / 16.0f)) * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -eyePx, 0.0f));
            if (PERLIN_ENV("PERLIN_FP_DEBUG"))
                for (const Layer& l : layers) {
                    glm::vec3 lo(1e9f), hi(-1e9f), mlo(1e9f), mhi(-1e9f);
                    for (const auto& q : l.quads)
                        for (const auto& pt : q.p) {
                            lo = glm::min(lo, pt); hi = glm::max(hi, pt);
                            const glm::vec3 w = glm::vec3(m * glm::vec4(pt, 1.0f));
                            mlo = glm::min(mlo, w); mhi = glm::max(mhi, w);
                        }
                    std::cout << "[fp] layer " << l.texture << " quads=" << l.quads.size() << " model " << lo.x << "," << lo.y << "," << lo.z << " .. " << hi.x << "," << hi.y << "," << hi.z
                              << " view " << mlo.x << "," << mlo.y << "," << mlo.z << " .. " << mhi.x << "," << mhi.y << "," << mhi.z << std::endl;
                }
            fpModel = &m;
            Draw(e, v, layers, scale, in);
            fpModel = nullptr;
        }
        v.fp = false;
        fp = nullptr;
    }

    void EntityRenderer::RenderDoll(const Entity& e, const FrameIn& in, float cx, float feetY, float pxPerBlock, const int clip[4]) {
        if (!renderer)
            return;
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (root.empty())
            return;
        assets.Configure(root);
        auto& slot = views[e.runtimeId];
        if (!slot)
            slot = std::make_unique<View>();
        View& v = *slot;
        v.lastSeen = in.time;
        Track(e, v, in);
        std::vector<Layer> layers;
        float scale = 1.0f;
        if (Build(e, v, in, layers, scale)) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(cx, feetY, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(pxPerBlock, -pxPerBlock, pxPerBlock));
            dollModel = &m;
            dollClip = clip;
            Draw(e, v, layers, scale, in);
            dollModel = nullptr;
            dollClip = nullptr;
        }
    }

    void EntityRenderer::DrawShadow(const glm::dvec3& pos, float radius, float strength, const FrameIn& in) {
        if (PERLIN_ENV("PERLIN_SHADOW_DEBUG"))
            std::cout << "[shadow-0] " << (in.groundY ? 1 : 0) << " r " << radius << std::endl;
        if (!in.groundY || radius <= 0.0f)
            return;
        const double ground = in.groundY(pos.x, pos.y + 0.5, pos.z);
        if (PERLIN_ENV("PERLIN_SHADOW_DEBUG"))
            std::cout << "[shadow-g] y " << pos.y << " ground " << ground << std::endl;
        if (ground < -500.0)
            return;
        const float height = static_cast<float>(pos.y - ground);
        if (PERLIN_ENV("PERLIN_SHADOW_DEBUG"))
            std::cout << "[shadow-in] y " << pos.y << " ground " << ground << " r " << radius << std::endl;
        if (height < -0.5f || height > 2.0f * radius * 2.0f + 8.0f)
            return;
        const double camDist2 = glm::dot(pos - in.camera, pos - in.camera);
        const float fade = static_cast<float>(1.0 - camDist2 / 256.0);
        if (fade <= 0.0f)
            return;
        float alpha = (strength * fade - std::max(0.0f, height) * 0.5f) * 0.5f;
        if (in.light) {
            const glm::vec3 l = in.light(pos + glm::dvec3(0.0, 0.5, 0.0));
            alpha *= std::clamp((l.x + l.y + l.z) / 3.0f, 0.0f, 1.0f);
        }
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        if (PERLIN_ENV("PERLIN_SHADOW_DEBUG"))
            std::cout << "[shadow] pos " << pos.y << " ground " << ground << " alpha " << alpha << " r " << radius << std::endl;
        if (alpha <= 0.004f)
            return;
        if (!shadowTexture) {
            std::vector<uint8_t> px(32 * 32 * 4, 0);
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    const float dx = (x + 0.5f - 16.0f) / 16.0f, dy = (y + 0.5f - 16.0f) / 16.0f;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    const float a = std::clamp((0.94f - d) / 0.12f, 0.0f, 1.0f);
                    px[(static_cast<size_t>(y) * 32 + x) * 4 + 3] = static_cast<uint8_t>(a * 255.0f);
                }
            shadowTexture = renderer->UiCreateTexture(32, 32, px.data(), false);
        }
        if (!shadowTexture)
            return;
        const uint32_t color = PackColor(glm::vec3(0.0f), std::min(alpha, 1.0f));
        const float r = radius;
        const UiVertex v[4] = {{-r, 0, -r, color, 0, 0}, {r, 0, -r, color, 1, 0}, {r, 0, r, color, 1, 1}, {-r, 0, r, color, 0, 1}};
        const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
        const uint32_t mesh = renderer->UiCreateFrameMesh(v, 4, idx, 6);
        if (!mesh)
            return;
        pendingMeshes.push_back(mesh);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.x, ground + 0.015, pos.z));
        UiDraw draw;
        draw.mesh = mesh;
        draw.texture = shadowTexture;
        std::copy_n(&model[0][0], 16, draw.model);
        draw.premultiplied = false;
        draw.alphaCutoff = 0.004f;
        draw.blend = 1;
        draw.depthWrite = false;
        draw.depth = true;
        draw.emissive = 2;
        draw.shadowOnly = shadowOnlyPass;
        renderer->UiSubmit(UiLayer::World, draw);
    }

    void EntityRenderer::DrawParticles(const FrameIn& in) {
        const bool haveCustom = in.dataParticles && in.dataParticles->Count() > 0;
        if (!renderer || ((!in.particles || in.particles->empty()) && !haveCustom))
            return;
        if (!particleSheet && !particleSheetTried) {
            particleSheetTried = true;
            Texture t;
            if (ui::UiAssets::Instance().Image("textures/particle/particles", t) && t.width > 0)
                particleSheet = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
        }
        const float yawR = in.cameraYaw * kDeg, pitchR = in.cameraPitch * kDeg;
        const glm::vec3 forward(-std::sin(yawR) * std::cos(pitchR), -std::sin(pitchR), std::cos(yawR) * std::cos(pitchR));
        const glm::vec3 right(-std::cos(yawR), 0.0f, -std::sin(yawR));
        const glm::vec3 up = glm::cross(right, forward);
        const glm::dvec3 base(std::floor(in.camera.x), std::floor(in.camera.y), std::floor(in.camera.z));
        struct Batch {
            std::vector<UiVertex> vertices;
            std::vector<uint32_t> indices;
        };
        std::unordered_map<int, Batch> batches;
        static const std::vector<particle::Quad> noQuads;
        for (const particle::Quad& q : in.particles ? *in.particles : noQuads) {
            Batch& b = batches[q.terrainTile];
            const glm::vec3 c = glm::vec3(q.centre - base);
            const glm::vec3 r = right * q.half, u = up * q.half;
            const uint32_t i0 = static_cast<uint32_t>(b.vertices.size());
            const glm::vec3 p0 = c - r - u, p1 = c + r - u, p2 = c + r + u, p3 = c - r + u;
            b.vertices.push_back({p0.x, p0.y, p0.z, q.abgr, q.u0, q.v1});
            b.vertices.push_back({p1.x, p1.y, p1.z, q.abgr, q.u1, q.v1});
            b.vertices.push_back({p2.x, p2.y, p2.z, q.abgr, q.u1, q.v0});
            b.vertices.push_back({p3.x, p3.y, p3.z, q.abgr, q.u0, q.v0});
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u})
                b.indices.push_back(i0 + k);
        }
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(base));
        if (haveCustom) {
            particle::CameraView view;
            view.position = in.camera;
            view.right = right;
            view.up = up;
            view.forward = forward;
            static std::vector<particle::CustomQuad> custom;
            custom.clear();
            in.dataParticles->Collect(view, in.dataWorld, custom);
            struct CustomBatch {
                std::vector<UiVertex> vertices;
                std::vector<uint32_t> indices;
            };
            std::map<std::pair<std::string, int>, CustomBatch> grouped;
            for (const particle::CustomQuad& q : custom) {
                CustomBatch& b = grouped[{q.texture, q.blend}];
                const glm::vec3 c = glm::vec3(q.centre - base);
                const uint32_t i0 = static_cast<uint32_t>(b.vertices.size());
                const glm::vec3 p0 = c - q.right - q.up, p1 = c + q.right - q.up, p2 = c + q.right + q.up, p3 = c - q.right + q.up;
                b.vertices.push_back({p0.x, p0.y, p0.z, q.abgr, q.u0, q.v1});
                b.vertices.push_back({p1.x, p1.y, p1.z, q.abgr, q.u1, q.v1});
                b.vertices.push_back({p2.x, p2.y, p2.z, q.abgr, q.u1, q.v0});
                b.vertices.push_back({p3.x, p3.y, p3.z, q.abgr, q.u0, q.v0});
                for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u})
                    b.indices.push_back(i0 + k);
            }
            for (auto& [keyPair, b] : grouped) {
                auto cached = particleTextures.find(keyPair.first);
                if (cached == particleTextures.end()) {
                    uint32_t id = 0;
                    Texture t;
                    if (ui::UiAssets::Instance().Image(keyPair.first, t) && t.width > 0)
                        id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
                    cached = particleTextures.emplace(keyPair.first, id).first;
                }
                if (!cached->second || b.vertices.empty())
                    continue;
                const uint32_t mesh = renderer->UiCreateFrameMesh(b.vertices.data(), static_cast<uint32_t>(b.vertices.size()), b.indices.data(),
                                                             static_cast<uint32_t>(b.indices.size()));
                if (!mesh)
                    continue;
                pendingMeshes.push_back(mesh);
                UiDraw draw;
                draw.mesh = mesh;
                draw.texture = cached->second;
                std::copy_n(&model[0][0], 16, draw.model);
                draw.premultiplied = false;
                StyleFromMaterial(draw, keyPair.second == 1 ? "particles_add" : (keyPair.second == 2 ? "particles_blend" : "particles_alpha"));
                draw.alphaCutoff = keyPair.second == 0 ? 0.5f : 0.02f;
                draw.emissive = 2;
                draw.shadowOnly = shadowOnlyPass;
                renderer->UiSubmit(UiLayer::World, draw);
            }
        }
        for (auto& [tile, b] : batches) {
            uint32_t texture = 0;
            if (tile < 0) {
                texture = particleSheet;
            } else {
                auto it = particleTiles.find(tile);
                if (it == particleTiles.end()) {
                    uint32_t id = 0;
                    std::vector<uint8_t> rgba;
                    if (in.tilePixels && in.tilePixels(tile, rgba) && !rgba.empty()) {
                        const int side = static_cast<int>(std::lround(std::sqrt(static_cast<double>(rgba.size() / 4))));
                        id = renderer->UiCreateTexture(side, side, rgba.data(), false);
                    }
                    it = particleTiles.emplace(tile, id).first;
                }
                texture = it->second;
            }
            if (!texture || b.vertices.empty())
                continue;
            const uint32_t mesh = renderer->UiCreateFrameMesh(b.vertices.data(), static_cast<uint32_t>(b.vertices.size()), b.indices.data(),
                                                         static_cast<uint32_t>(b.indices.size()));
            if (!mesh)
                continue;
            pendingMeshes.push_back(mesh);
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = texture;
            std::copy_n(&model[0][0], 16, draw.model);
            draw.premultiplied = false;
            StyleFromMaterial(draw, tile < 0 ? "particles_blend" : "particles_alpha");
            draw.alphaCutoff = tile < 0 ? 0.02f : 0.5f;
            draw.emissive = 2;
            draw.shadowOnly = shadowOnlyPass;
            renderer->UiSubmit(UiLayer::World, draw);
        }
    }

    void EntityRenderer::DrawNametags(const FrameIn& in) {
        if (tags.empty() || !renderer)
            return;
        ui::McFont& font = ui::McFont::Instance();
        if (!fontTexture || fontTextureVersion != font.Version()) {
            if (fontTexture)
                renderer->UiDestroyTexture(fontTexture);
            fontTexture = renderer->UiCreateTexture(font.AtlasWidth(), font.AtlasHeight(), font.Atlas().data(), false);
            fontTextureVersion = font.Version();
        }
        if (!fontTexture)
            return;
        const float yawR = in.cameraYaw * kDeg, pitchR = in.cameraPitch * kDeg;
        const glm::vec3 forward(-std::sin(yawR) * std::cos(pitchR), -std::sin(pitchR), std::cos(yawR) * std::cos(pitchR));
        const glm::vec3 right(-std::cos(yawR), 0.0f, -std::sin(yawR));
        const glm::vec3 up = glm::cross(right, forward);
        constexpr float kScale = 0.025f;
        for (const Tag& tag : tags) {
            std::vector<std::string> rows;
            for (size_t start = 0;;) {
                const size_t nl = tag.text.find('\n', start);
                rows.push_back(tag.text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
                if (nl == std::string::npos)
                    break;
                start = nl + 1;
            }
            static thread_local std::vector<UiVertex> bgV, textV;
            static thread_local std::vector<uint32_t> bgI, textI;
            bgV.clear();
            textV.clear();
            bgI.clear();
            textI.clear();
            const auto corner = [&](float x, float y) { return right * (x * kScale) + up * (-y * kScale); };
            const auto quad = [&](std::vector<UiVertex>& vv, std::vector<uint32_t>& ii, float x0, float y0, float x1, float y1, float u0,
                                  float v0, float u1, float v1, uint32_t color) {
                const uint32_t base = static_cast<uint32_t>(vv.size());
                const glm::vec3 p0 = corner(x0, y1), p1 = corner(x1, y1), p2 = corner(x1, y0), p3 = corner(x0, y0);
                vv.push_back({p0.x, p0.y, p0.z, color, u0, v1});
                vv.push_back({p1.x, p1.y, p1.z, color, u1, v1});
                vv.push_back({p2.x, p2.y, p2.z, color, u1, v0});
                vv.push_back({p3.x, p3.y, p3.z, color, u0, v0});
                for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u})
                    ii.push_back(base + i);
            };
            glm::vec3 col(1.0f);
            bool bold = false;
            for (size_t row = 0; row < rows.size(); row++) {
            const std::string& text = rows[row];
            const float rowY = (static_cast<float>(row) - static_cast<float>(rows.size() - 1)) * 10.0f;
            const float width = font.Width(text);
            const float left = -width * 0.5f;
            if (!text.empty())
                quad(bgV, bgI, left - 1.0f, rowY - 1.0f, left + width + 1.0f, rowY + 8.0f, 0, 0, 0, 0, PackColor(glm::vec3(0.0f), 0.25f));
            float pen = left;
            for (size_t i = 0; i < text.size();) {
                uint32_t cp = 0;
                if (!ui::McFont::Next(text, i, cp))
                    break;
                if (cp == 0xA7 && i < text.size()) {
                    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i++])));
                    uint32_t rgb = 0;
                    if (ui::FormatCodeColor(c, rgb)) {
                        col = glm::vec3(static_cast<float>((rgb >> 16) & 0xFF), static_cast<float>((rgb >> 8) & 0xFF), static_cast<float>(rgb & 0xFF)) / 255.0f;
                        bold = false;
                    } else if (c == 'l') {
                        bold = true;
                    } else if (c == 'r') {
                        col = glm::vec3(1.0f);
                        bold = false;
                    }
                    continue;
                }
                if (cp < 0x20)
                    continue;
                const ui::McFont::Glyph& g = font.Get(cp);
                if (g.width > 0)
                    quad(textV, textI, pen + g.left, rowY + g.top, pen + g.left + g.width, rowY + g.top + g.height, g.u0, g.v0, g.u1, g.v1, PackColor(g.colored ? glm::vec3(1.0f) : col));
                pen += static_cast<float>(g.advance) + (bold ? 1.0f : 0.0f);
            }
            }
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(tag.at.x, tag.at.y, tag.at.z));
            const auto submit = [&](std::vector<UiVertex> vv, const std::vector<uint32_t>& ii, uint32_t texture, bool seeThrough, float alpha) {
                if (vv.empty())
                    return;
                if (alpha != 1.0f)
                    for (UiVertex& v : vv) {
                        const uint32_t a = static_cast<uint32_t>(static_cast<float>(v.abgr >> 24) * alpha);
                        v.abgr = (v.abgr & 0x00FFFFFFu) | (a << 24);
                    }
                const uint32_t mesh = renderer->UiCreateFrameMesh(vv.data(), static_cast<uint32_t>(vv.size()), ii.data(), static_cast<uint32_t>(ii.size()));
                if (!mesh)
                    return;
                pendingMeshes.push_back(mesh);
                UiDraw draw;
                draw.mesh = mesh;
                draw.texture = texture;
                std::copy_n(&model[0][0], 16, draw.model);
                draw.premultiplied = false;
                StyleFromMaterial(draw, texture ? (seeThrough ? "name_tag_text" : "name_text_depth_tested")
                                                : (seeThrough ? "name_tag" : "name_tag_depth_tested"));
                draw.blend = 1;
                draw.customBlend = false;
                draw.alphaCutoff = 0.004f;
                draw.depthWrite = false;
                draw.emissive = 2;
                renderer->UiSubmit(UiLayer::WorldLate, draw);
            };
            submit(bgV, bgI, 0, true, 1.0f);
            submit(textV, textI, fontTexture, true, tag.sneaking ? 1.0f : 32.0f / 255.0f);
            if (!tag.sneaking)
                submit(textV, textI, fontTexture, false, 1.0f);
            if (in.nametagDepth && !bgV.empty()) {
                const uint32_t mesh = renderer->UiCreateFrameMesh(bgV.data(), static_cast<uint32_t>(bgV.size()), bgI.data(), static_cast<uint32_t>(bgI.size()));
                if (mesh) {
                    pendingMeshes.push_back(mesh);
                    UiDraw draw;
                    draw.mesh = mesh;
                    draw.material = DrawMaterial::Nametag;
                    std::copy_n(&model[0][0], 16, draw.model);
                    draw.premultiplied = false;
                    draw.blend = 0;
                    draw.colorWrite = false;
                    draw.alphaWrite = false;
                    draw.depth = true;
                    draw.depthWrite = true;
                    draw.alphaCutoff = 0.004f;
                    renderer->UiSubmit(UiLayer::WorldLate, draw);
                }
            }
        }
        tags.clear();
    }

    void EntityRenderer::DrawCracks(const FrameIn& in) {
        for (const auto& c : in.cracks) {
            const std::string path = "textures/environment/destroy_stage_" + std::to_string(std::clamp(c.stage, 0, 9));
            auto it = textures.find(path);
            if (it == textures.end()) {
                Texture t;
                uint32_t id = 0;
                if (ui::UiAssets::Instance().Image(path, t) && t.width > 0)
                    id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
                it = textures.emplace(path, id).first;
            }
            if (!it->second)
                continue;
            const glm::dvec3 d = glm::dvec3(c.x + 0.5, c.y + 0.5, c.z + 0.5) - in.camera;
            if (glm::dot(d, d) > 48.0 * 48.0)
                continue;
            const uint32_t white = 0xFFFFFFFFu;
            std::vector<UiVertex> v;
            std::vector<uint32_t> idx;
            const auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc, glm::vec3 dd) {
                const glm::vec3 n = glm::abs(glm::cross(b - a, dd - a));
                const auto uv = [&](glm::vec3 p) {
                    if (n.y >= n.x && n.y >= n.z)
                        return glm::vec2(p.x, p.z);
                    if (n.z >= n.x)
                        return glm::vec2(p.x, 1.0f - p.y);
                    return glm::vec2(p.z, 1.0f - p.y);
                };
                const uint32_t base = static_cast<uint32_t>(v.size());
                for (const glm::vec3& p : {a, b, cc, dd}) {
                    const glm::vec2 t = uv(p);
                    v.push_back({p.x, p.y, p.z, white, t.x, t.y});
                }
                for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                    idx.push_back(base + i);
            };
            const std::vector<std::array<float, 6>> fullCube{{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f}};
            for (const std::array<float, 6>& b : c.boxes.empty() ? fullCube : c.boxes) {
                const float x0 = b[0] - 0.002f, y0 = b[1] - 0.002f, z0 = b[2] - 0.002f;
                const float x1 = b[3] + 0.002f, y1 = b[4] + 0.002f, z1 = b[5] + 0.002f;
                face({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});
                face({x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0});
                face({x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0});
                face({x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1});
                face({x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0});
                face({x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1});
            }
            const uint32_t mesh = renderer->UiCreateFrameMesh(v.data(), static_cast<uint32_t>(v.size()), idx.data(), static_cast<uint32_t>(idx.size()));
            if (!mesh)
                continue;
            pendingMeshes.push_back(mesh);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(c.x, c.y, c.z));
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = it->second;
            std::copy_n(&model[0][0], 16, draw.model);
            draw.premultiplied = false;
            StyleFromMaterial(draw, "cracks_overlay");
            draw.alphaCutoff = 0.5f;
            draw.emissive = 2;
            draw.shadowOnly = shadowOnlyPass;
            renderer->UiSubmit(UiLayer::World, draw);
        }
    }

    void EntityRenderer::Render(const std::vector<Entity>& entities, const FrameIn& in) {
        if (!renderer)
            return;
        PruneSkinCaches();
        for (uint32_t mesh : pendingMeshes)
            renderer->UiDestroyMesh(mesh);
        pendingMeshes.clear();
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (root.empty())
            return;
        assets.Configure(root);
        drawn = considered = unrenderable = culled = 0;
        if (itemRenderer)
            itemRenderer->BeginWorldItems();
        tags.clear();
        for (const Entity& e : entities) {
            const bool shadowOnlySelf = e.isSelf && in.firstPerson;
            if (shadowOnlySelf && !in.selfShadow)
                continue;
            shadowOnlyPass = shadowOnlySelf;
            considered++;
            if (e.type == "minecraft:painting") {
                DrawPainting(e, in);
                continue;
            }
            auto& slot = views[e.runtimeId];
            if (!slot)
                slot = std::make_unique<View>();
            View& v = *slot;
            v.lastSeen = in.time;
            Track(e, v, in);
            const FrameIn::ModStyle* style = in.modStyle ? in.modStyle(e) : nullptr;
            if (style && style->hidden)
                continue;
            const glm::dvec3 delta = v.pos - in.camera;
            if (glm::dot(delta, delta) > static_cast<double>(in.maxDistance) * in.maxDistance)
                continue;
            // server holograms hide the model, keep the name: entity scaled to zero, or invisible mob/armor stand with no gear
            // (with armor or held items only those show; Draw hides the body)
            const bool gearless = e.mainHand.empty() && e.offHand.empty() &&
                                  std::all_of(std::begin(e.armor), std::end(e.armor), [](const std::string& a) { return a.empty(); });
            const bool modelHidden = !e.isPlayer && (e.scale == 0.0f || (e.Has(flag::Invisible) && gearless));
            if (e.dying && v.deathTime >= 1.0f && !e.isSelf)
                continue;
            if (in.frustum && !shadowOnlySelf) {
                float w = 0.5f, h = 0.5f;
                if (!e.isItem) {
                    const auto hb = HitBox(e);
                    w = hb.first;
                    h = hb.second;
                }
                const float r = std::max(w, h) * 0.5f + 0.75f + (e.isItem ? 0.0f : 0.5f * h);
                const float x = static_cast<float>(v.pos.x), y = static_cast<float>(v.pos.y), z = static_cast<float>(v.pos.z);
                if (!in.frustum->Visible(x - r, y - 0.5f, z - r, x + r, y + std::max(h, 0.5f) + r, z + r)) {
                    culled++;
                    continue;
                }
                // behind walls: no model; names still show through walls
                if (in.sectionReached && !e.isSelf && !e.isPlayer && e.nameTag.empty() &&
                    !in.sectionReached(static_cast<int>(std::floor(x)) >> 4, static_cast<int>(std::floor(y)) >> 4,
                                       static_cast<int>(std::floor(z)) >> 4)) {
                    culled++;
                    continue;
                }
            }
            if (in.blobShadows && !modelHidden && !e.Has(flag::Invisible) && e.type != "minecraft:cushion" && !(e.isSelf && in.firstPerson)) {
                if (e.isItem) {
                    DrawShadow(v.pos, 0.15f, 0.75f, in);
                } else if (!e.dying || v.deathTime < 0.9f) {
                    const auto hb = HitBox(e);
                    DrawShadow(v.pos, std::min(hb.first, 1.4f) * 0.8f * (e.scale > 0 ? e.scale : 1.0f), 1.0f, in);
                }
            }
            // invisible players hide their name; invisible holograms (armor stands, mobs) keep an always-shown name
            const bool nameHidden = e.Has(flag::Invisible) && (e.isPlayer || !e.Has(flag::AlwaysShowName));
            // name shown only when the server allows it (can-show / always-show flags); fake-player npcs leave both off
            // text: name tag, falling back to a player's username
            const bool nameAllowed = e.isSelf || e.Has(flag::CanShowName) || e.Has(flag::AlwaysShowName) || !e.isPlayer;
            if (in.nametags && !e.isItem && !nameHidden && nameAllowed && !(e.isSelf && (in.firstPerson || !in.ownNametag))) {
                const std::string& label = style && style->nameTag == 1 ? std::string() : style && style->nameTag == 2 ? style->nameText : !e.nameTag.empty() ? e.nameTag : e.isPlayer ? e.name : e.nameTag;
                const double d2 = glm::dot(delta, delta);
                if (!label.empty() && d2 < (e.isPlayer ? 64.0 * 64.0 : 16.0 * 16.0)) {
                    if (std::getenv("PERLIN_DEBUG_NAMETAGS") != nullptr) {
                        static std::unordered_set<std::string> seen;
                        if (seen.size() > 20000)
                            seen.clear();
                        std::string key = std::to_string(e.runtimeId) + "|" + label;
                        if (seen.insert(key).second) {
                            std::string flat = label;
                            for (char& c : flat)
                                if (c == '\n')
                                    c = '|';
                            std::cout << "[nametag] draw rid=" << e.runtimeId << " type=" << e.type
                                      << (e.isPlayer ? " player" : "") << " taglen=" << e.nameTag.size()
                                      << " text=" << flat << std::endl;
                        }
                    }
                    const auto hb = HitBox(e);
                    Tag t;
                    t.text = label;
                    t.at = v.pos + glm::dvec3(0.0, static_cast<double>(hb.second * (e.scale > 0 ? e.scale : 1.0f)) + 0.5, 0.0);
                    t.sneaking = e.Has(flag::Sneaking);
                    tags.push_back(std::move(t));
                }
            }
            if (modelHidden)
                continue;
            if (e.isItem) {
                if (itemRenderer) {
                    ui::HudItem item;
                    item.name = e.itemName;
                    item.count = e.itemCount;
                    item.aux = e.itemAux;
                    item.blockRuntimeId = e.itemBlock;
                    item.maxDamage = ui::MaxDurability(e.itemName);
                    item.enchanted = e.itemEnchanted;
                    glm::vec3 light(1.0f);
                    if (in.light)
                        light = in.light(v.pos + glm::dvec3(0.0, 0.5, 0.0));
                    itemRenderer->DrawWorldItem(item, v.pos, static_cast<float>((in.time - v.born) * 20.0), e.runtimeId, light);
                    drawn++;
                }
                continue;
            }
            if (e.type == "minecraft:xp_orb") {
                const double age = (in.time - v.born) * 20.0;
                View facing = v;
                facing.yaw = 180.0f;
                Layer layer;
                layer.texture = "textures/entity/experience_orb";
                const int icon = std::clamp(e.variant, 0, 10);
                auto& geometry = orbGeometry[icon];
                if (geometry.empty()) {
                    Texture atlas;
                    if (ui::UiAssets::Instance().Image(layer.texture, atlas))
                        geometry = attachable::ExtrudeSprite(atlas, (icon % 4) * atlas.width / 4,
                            (icon / 4) * atlas.height / 4, atlas.width / 4, atlas.height / 4, 4.8f, 4.8f, 1.2f);
                }
                layer.quads = geometry;
                facing.yaw = static_cast<float>(age * 2.0 + (e.runtimeId % 360) * 137.0);
                const float t = static_cast<float>(age) * 0.5f;
                layer.tint[0] = (std::sin(t) + 1.0f) * 0.5f;
                layer.tint[1] = 1.0f;
                layer.tint[2] = (std::sin(t + 4.1887903f) + 1.0f) * 0.1f;
                layer.tint[3] = 128.0f / 255.0f;
                layer.translucent = true;
                layer.material.blend = MaterialInfo::Blend::Alpha;
                layer.material.emissive = 2;
                layer.material.hurtTint = false;
                Draw(e, facing, {layer}, 1.0f, in);
                drawn++;
                continue;
            }
            const double dist2 = glm::dot(delta, delta);
            const bool pinned = e.dying || e.Has(flag::Riding);
            const double reuseFor = pinned ? 0.0 : dist2 > 80.0 * 80.0 ? 0.066 : dist2 > 48.0 * 48.0 ? 0.033 : dist2 > 24.0 * 24.0 ? 0.016 : dist2 > 16.0 * 16.0 ? 0.008 : 0.0035;
            const auto drawStyled = [&](const std::vector<Layer>& layers, float scale) {
                if (!style) {
                    Draw(e, v, layers, scale, in);
                    return;
                }
                std::vector<Layer> styled = layers;
                for (Layer& l : styled)
                    for (int i = 0; i < 4; i++)
                        l.tint[i] *= style->tint[i];
                if (!style->texture.empty() && !styled.empty())
                    styled.front().texture = style->texture;
                modScale = style->scale;
                Draw(e, v, styled, scale, in);
                modScale = glm::vec3(1.0f);
            };
            if (reuseFor > 0.0 && in.time - v.cacheTime < reuseFor && !v.cachedLayers.empty()) {
                drawStyled(v.cachedLayers, v.cachedScale);
                drawn++;
                continue;
            }
            std::vector<Layer> layers;
            float scale = 1.0f;
            const bool built = Build(e, v, in, layers, scale);
            if (!built) {
                unrenderable++;
                if (!warned["build:" + e.type]) {
                    warned["build:" + e.type] = true;
                    std::cout << "[entity] cannot render " << e.type << (assets.Entity(e.type) ? " (no usable layer)" : " (no definition)") << std::endl;
                }
                continue;
            }
            drawStyled(layers, scale);
            if (reuseFor > 0.0) {
                v.cachedLayers = std::move(layers);
                v.cachedScale = scale;
                v.cacheTime = in.time;
            }
            drawn++;
        }
        shadowOnlyPass = false;
        if (PERLIN_ENV("PERLIN_PROFILE") && ++gEntityProfFrames >= 120) {
            std::cout << "[profile] entity build=" << gEntityProf[0] / gEntityProfFrames << " draw=" << gEntityProf[1] / gEntityProfFrames << " shadow+rest=" << gEntityProf[2] / gEntityProfFrames << std::endl;
            gEntityProf[0] = gEntityProf[1] = gEntityProf[2] = 0;
            gEntityProfFrames = 0;
        }
        DrawParticles(in);
        DrawNametags(in);
        for (auto it = views.begin(); it != views.end();)
            it = in.time - it->second->lastSeen > 5.0 ? views.erase(it) : std::next(it);
        if (in.hitboxes) {
            const uint32_t white = 0xFFFFFFFFu, red = 0xFF0000FFu, blue = 0xFFFF0000u;
            for (const Entity& e : entities) {
                if (e.isSelf && in.firstPerson)
                    continue;
                const auto v = views.find(e.runtimeId);
                const glm::dvec3 pos = v != views.end() ? v->second->pos : e.pos;
                const double yawDeg = v != views.end() ? v->second->yaw : e.yaw;
                const auto [w, h] = HitBox(e);
                const double half = w * 0.5;
                AddBox({pos.x - half, pos.y, pos.z - half}, {pos.x + half, pos.y + h, pos.z + half}, white, 1.0f);
                if (e.isPlayer) {
                    const double eye = pos.y + (e.Has(flag::Sneaking) ? 1.27 : 1.62);
                    AddBox({pos.x - half, eye - 0.01, pos.z - half}, {pos.x + half, eye + 0.01, pos.z + half}, red, 1.0f);
                    const double yr = yawDeg * kDeg;
                    lines.push_back({{pos.x, eye, pos.z}, {pos.x - std::sin(yr) * 2.0, eye, pos.z + std::cos(yr) * 2.0}, blue, 1.5f});
                }
            }
        }
        if (in.modLines)
            for (const FrameIn::ModLine& l : *in.modLines)
                lines.push_back({l.a, l.b, l.colour, l.width});
        DrawModMeshes(in);
        if (in.chunkBorders) {
            const int pcx = static_cast<int>(std::floor(in.playerPos.x)) >> 4, pcz = static_cast<int>(std::floor(in.playerPos.z)) >> 4;
            const int sectionY = static_cast<int>(std::floor(in.playerPos.y)) >> 4;
            const double minY = -64.0, maxY = 320.0;
            const double x0 = pcx * 16.0, z0 = pcz * 16.0, x1 = x0 + 16.0, z1 = z0 + 16.0;
            const float thin = 1.0f / 2.5f, thick = 4.0f / 2.5f;
            const auto argb = [](int r, int g, int b) { return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r); };
            const uint32_t red = 0x800000FFu;
            const uint32_t cell = argb(0, 155, 155);
            const uint32_t yellow = argb(255, 255, 0);
            const uint32_t major = argb(64, 64, 255);
            for (int j = -16; j <= 32; j += 16)
                for (int i = -16; i <= 32; i += 16)
                    lines.push_back({{x0 + i, minY, z0 + j}, {x0 + i, maxY, z0 + j}, red, thick});
            for (int k = 2; k < 16; k += 2) {
                const uint32_t c = k % 4 == 0 ? cell : yellow;
                lines.push_back({{x0 + k, minY, z0}, {x0 + k, maxY, z0}, c, thin});
                lines.push_back({{x0 + k, minY, z1}, {x0 + k, maxY, z1}, c, thin});
            }
            for (int k = 2; k < 16; k += 2) {
                const uint32_t c = k % 4 == 0 ? cell : yellow;
                lines.push_back({{x0, minY, z0 + k}, {x0, maxY, z0 + k}, c, thin});
                lines.push_back({{x1, minY, z0 + k}, {x1, maxY, z0 + k}, c, thin});
            }
            for (int y = static_cast<int>(minY); y <= static_cast<int>(maxY); y += 2) {
                const uint32_t c = y % 8 == 0 ? cell : yellow;
                lines.push_back({{x0, double(y), z0}, {x0, double(y), z1}, c, thin});
                lines.push_back({{x0, double(y), z1}, {x1, double(y), z1}, c, thin});
                lines.push_back({{x1, double(y), z1}, {x1, double(y), z0}, c, thin});
                lines.push_back({{x1, double(y), z0}, {x0, double(y), z0}, c, thin});
            }
            for (int j = 0; j <= 16; j += 16)
                for (int i = 0; i <= 16; i += 16)
                    lines.push_back({{x0 + i, minY, z0 + j}, {x0 + i, maxY, z0 + j}, major, thick});
            AddBox({x0, sectionY * 16.0, z0}, {x1, sectionY * 16.0 + 16.0, z1}, major, thin);
            for (int y = static_cast<int>(minY); y <= static_cast<int>(maxY); y += 16) {
                lines.push_back({{x0, double(y), z0}, {x0, double(y), z1}, major, thick});
                lines.push_back({{x0, double(y), z1}, {x1, double(y), z1}, major, thick});
                lines.push_back({{x1, double(y), z1}, {x1, double(y), z0}, major, thick});
                lines.push_back({{x1, double(y), z0}, {x0, double(y), z0}, major, thick});
            }
        }
        for (const Entity& hook : entities) {
            if (hook.type != "minecraft:fishing_hook" || hook.ownerUid == 0)
                continue;
            const Entity* owner = nullptr;
            for (const Entity& o : entities)
                if (o.uniqueId == hook.ownerUid && !o.isItem) {
                    owner = &o;
                    break;
                }
            const auto hv = views.find(hook.runtimeId);
            if (!owner || hv == views.end())
                continue;
            const auto ov = views.find(owner->runtimeId);
            const double ownerYaw = ov != views.end() ? ov->second->yaw : owner->yaw;
            const glm::dvec3 ownerPos = ov != views.end() ? ov->second->pos : owner->pos;
            glm::dvec3 hand;
            if (owner->isSelf && in.firstPerson) {
                const double yr = in.cameraYaw * kDeg, pr = in.cameraPitch * kDeg;
                const glm::dvec3 fwd(-std::sin(yr) * std::cos(pr), -std::sin(pr), std::cos(yr) * std::cos(pr));
                const glm::dvec3 rgt(-std::cos(yr), 0.0, -std::sin(yr));
                const glm::dvec3 upv = glm::cross(rgt, fwd);
                hand = in.camera + fwd * 0.65 + rgt * 0.38 - upv * 0.32;
            } else {
                const double f = ownerYaw * kDeg, s = std::sin(f), c = std::cos(f);
                hand = ownerPos + glm::dvec3(-c * 0.35 - s * 0.8, 1.62 * 0.8 + (owner->Has(flag::Sneaking) ? -0.1875 : 0.0), -s * 0.35 + c * 0.8);
            }
            const glm::dvec3 hookPos = hv->second->pos;
            const glm::dvec3 rel = hand - (hookPos + glm::dvec3(0.0, 0.25, 0.0));
            glm::dvec3 previous = hookPos + glm::dvec3(0.0, 0.25, 0.0);
            for (int i = 1; i <= 16; i++) {
                const double p = i / 16.0;
                const glm::dvec3 point = hookPos + glm::dvec3(rel.x * p, rel.y * (p * p + p) * 0.5 + 0.25, rel.z * p);
                lines.push_back({previous, point, 0xFF000000u, 0.4f});
                previous = point;
            }
        }
        DrawCracks(in);
        outlineLineStart = lines.size();
        if (in.outline) {
            if (!in.outlineShape.empty())
                AddShapeOutline(in.outlineShape, 0x66000000u);
            else
                AddBox(in.outlineLo, in.outlineHi, 0x66000000u);
        }
        DrawLines(in);
    }

    void EntityRenderer::AddBox(const glm::dvec3& lo, const glm::dvec3& hi, uint32_t colour, float width) {
        const glm::dvec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
                                 {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges)
            lines.push_back({c[e[0]], c[e[1]], colour, width});
    }

    void EntityRenderer::AddShapeOutline(const std::vector<std::pair<glm::dvec3, glm::dvec3>>& boxes, uint32_t colour) {
        if (boxes.empty())
            return;
        std::vector<double> c[3];
        for (const auto& [lo, hi] : boxes)
            for (int a = 0; a < 3; a++) {
                c[a].push_back(lo[a]);
                c[a].push_back(hi[a]);
            }
        for (auto& v : c) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end(), [](double x, double y) { return std::fabs(x - y) < 1e-6; }), v.end());
        }
        const int n[3] = {static_cast<int>(c[0].size()) - 1, static_cast<int>(c[1].size()) - 1, static_cast<int>(c[2].size()) - 1};
        if (n[0] < 1 || n[1] < 1 || n[2] < 1)
            return;
        const auto filled = [&](int i, int j, int k) {
            if (i < 0 || j < 0 || k < 0 || i >= n[0] || j >= n[1] || k >= n[2])
                return false;
            const glm::dvec3 centre((c[0][i] + c[0][i + 1]) * 0.5, (c[1][j] + c[1][j + 1]) * 0.5, (c[2][k] + c[2][k + 1]) * 0.5);
            for (const auto& [lo, hi] : boxes)
                if (centre.x > lo.x && centre.x < hi.x && centre.y > lo.y && centre.y < hi.y && centre.z > lo.z && centre.z < hi.z)
                    return true;
            return false;
        };
        for (int axis = 0; axis < 3; axis++) {
            const int u = (axis + 1) % 3, w = (axis + 2) % 3;
            for (int pu = 0; pu <= n[u]; pu++)
                for (int pw = 0; pw <= n[w]; pw++) {
                    int runStart = -1;
                    for (int t = 0; t <= n[axis]; t++) {
                        bool edge = false;
                        if (t < n[axis]) {
                            bool f[4];
                            for (int du = 0; du < 2; du++)
                                for (int dw = 0; dw < 2; dw++) {
                                    int idx[3];
                                    idx[axis] = t;
                                    idx[u] = pu - 1 + du;
                                    idx[w] = pw - 1 + dw;
                                    f[du * 2 + dw] = filled(idx[0], idx[1], idx[2]);
                                }
                            const int count = f[0] + f[1] + f[2] + f[3];
                            edge = count == 1 || count == 3 || (count == 2 && f[0] == f[3]);
                        }
                        if (edge && runStart < 0)
                            runStart = t;
                        if (!edge && runStart >= 0) {
                            glm::dvec3 a, b;
                            a[axis] = c[axis][runStart];
                            b[axis] = c[axis][t];
                            a[u] = b[u] = c[u][pu];
                            a[w] = b[w] = c[w][pw];
                            lines.push_back({a, b, colour});
                            runStart = -1;
                        }
                    }
                }
        }
    }

    void EntityRenderer::DrawLines(const FrameIn& in) {
        if (lines.empty())
            return;
        std::vector<UiVertex> vertices;
        std::vector<uint32_t> indices;
        const double pixel = in.pixelScale > 0.0f ? in.pixelScale : 0.002;
        size_t lineIndex = 0;
        std::vector<Line> pieces;
        pieces.reserve(lines.size() * 2);
        const auto lerpColour = [](uint32_t c0, uint32_t c1, double t) {
            uint32_t out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                const double v0 = (c0 >> sh) & 0xFF, v1 = (c1 >> sh) & 0xFF;
                out |= static_cast<uint32_t>(std::lround(v0 + (v1 - v0) * t)) << sh;
            }
            return out;
        };
        size_t outlineFirstPiece = static_cast<size_t>(-1);
        for (size_t li = 0; li < lines.size(); li++) {
            const Line& l = lines[li];
            if (li == outlineLineStart)
                outlineFirstPiece = pieces.size();
            const double len = glm::length(l.b - l.a);
            const int n = len > 8.0 ? static_cast<int>(std::ceil(len / 8.0)) : 1;
            const uint32_t endColour = l.colourEnd ? l.colourEnd : l.colour;
            for (int i = 0; i < n; i++) {
                const double t0 = static_cast<double>(i) / n, t1 = static_cast<double>(i + 1) / n;
                pieces.push_back({l.a + (l.b - l.a) * t0, l.a + (l.b - l.a) * t1, lerpColour(l.colour, endColour, t0), l.width,
                                  lerpColour(l.colour, endColour, t1)});
            }
        }
        if (outlineFirstPiece == static_cast<size_t>(-1))
            outlineFirstPiece = pieces.size();
        for (const Line& l : pieces) {
            const float layer = lineIndex++ >= outlineFirstPiece ? 0.99975586f : 1.0f;
            const glm::vec3 a = glm::vec3(l.a - in.camera) * layer, b = glm::vec3(l.b - in.camera) * layer;
            const glm::vec3 dir = b - a;
            const float length = glm::length(dir);
            if (length < 1e-5f)
                continue;
            const glm::vec3 mid = (a + b) * 0.5f;
            glm::vec3 side = glm::cross(dir, mid);
            if (glm::length(side) < 1e-8f)
                continue;
            const float halfA = static_cast<float>(in.lineWidthPx * l.width * 0.5 * pixel * glm::length(a));
            const float halfB = static_cast<float>(in.lineWidthPx * l.width * 0.5 * pixel * glm::length(b));
            const glm::vec3 sideDir = glm::normalize(side);
            const glm::vec3 along = dir / length;
            const glm::vec3 p0 = a - along * halfA, p1 = b + along * halfB;
            const glm::vec3 sideA = sideDir * halfA, sideB = sideDir * halfB;
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const uint32_t endColour = l.colourEnd ? l.colourEnd : l.colour;
            vertices.push_back({(p0 - sideA).x, (p0 - sideA).y, (p0 - sideA).z, l.colour, 0.0f, 0.0f});
            vertices.push_back({(p0 + sideA).x, (p0 + sideA).y, (p0 + sideA).z, l.colour, 0.0f, 0.0f});
            vertices.push_back({(p1 + sideB).x, (p1 + sideB).y, (p1 + sideB).z, endColour, 0.0f, 0.0f});
            vertices.push_back({(p1 - sideB).x, (p1 - sideB).y, (p1 - sideB).z, endColour, 0.0f, 0.0f});
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                indices.push_back(base + i);
        }
        lines.clear();
        if (vertices.empty())
            return;
        const uint32_t mesh = renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(),
                                                     static_cast<uint32_t>(indices.size()));
        if (!mesh)
            return;
        pendingMeshes.push_back(mesh);
        UiDraw draw;
        draw.mesh = mesh;
        draw.premultiplied = false;
        draw.blend = 1;
        draw.depth = true;
        draw.depthWrite = false;
        glm::mat4 model(1.0f);
        model[3] = glm::vec4(static_cast<float>(in.camera.x), static_cast<float>(in.camera.y), static_cast<float>(in.camera.z), 1.0f);
        std::copy_n(&model[0][0], 16, draw.model);
        draw.shadowOnly = shadowOnlyPass;
        renderer->UiSubmit(UiLayer::World, draw);
    }

    void EntityRenderer::DrawModMeshes(const FrameIn& in) {
        if (!in.modMeshes)
            return;
        for (const FrameIn::ModMesh& m : *in.modMeshes) {
            if (m.vertices.size() < 3)
                continue;
            std::vector<uint32_t> indices(m.vertices.size() - m.vertices.size() % 3);
            for (uint32_t i = 0; i < indices.size(); i++)
                indices[i] = i;
            const uint32_t mesh = renderer->UiCreateFrameMesh(m.vertices.data(), static_cast<uint32_t>(m.vertices.size()), indices.data(),
                                                             static_cast<uint32_t>(indices.size()));
            if (!mesh)
                continue;
            pendingMeshes.push_back(mesh);
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = m.texture;
            draw.premultiplied = false;
            draw.blend = 1;
            draw.depth = m.depthTest;
            draw.depthWrite = false;
            glm::mat4 model(1.0f);
            model[3] = glm::vec4(glm::vec3(m.origin), 1.0f);
            std::copy_n(&model[0][0], 16, draw.model);
            renderer->UiSubmit(UiLayer::World, draw);
        }
    }

    uint32_t EntityRenderer::TextureByPath(const std::string& path) {
        auto it = textures.find(path);
        if (it == textures.end()) {
            Texture t;
            uint32_t id = 0;
            bool loaded = ui::UiAssets::Instance().Image(path, t);
            if (!loaded && path.starts_with("bed_") && path.find('/') == std::string::npos)
                loaded = LoadTexture(paths::DataDir() + "/assets/" + path, t);
            if (loaded && t.width > 0)
                id = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false);
            else if (!warned[path]) {
                warned[path] = true;
                std::cout << "[entity] texture " << path << " not found" << std::endl;
            }
            it = textures.emplace(path, id).first;
        }
        return it->second;
    }

    namespace {
        struct BedCube {
            float o[3], s[3], uv[2];
        };

        const BedCube kBedCubes[] = {
            {{0, 0, 0}, {16, 32, 6}, {0, 0}},
            {{13, 29, 6}, {3, 3, 3}, {12, 38}},
            {{0, 29, 6}, {3, 3, 3}, {0, 38}},   {{13, 0, 6}, {3, 3, 3}, {12, 44}}, {{0, 0, 6}, {3, 3, 3}, {0, 44}},
        };

        const char* const kDyeNames[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                         "silver", "cyan", "purple", "blue", "brown", "green", "red", "black"};

        struct WrappedLine {
            std::string text;
        };

        std::vector<std::string> WrapSignText(const std::string& text, float maxWidth, int maxLines) {
            ui::McFont& font = ui::McFont::Instance();
            std::vector<std::string> lines;
            for (size_t start = 0;;) {
                const size_t nl = text.find('\n', start);
                std::string row = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
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
    }

    namespace {
        struct PaintingMotive {
            const char* title;
            int w, h, ox, oy;
        };

        const PaintingMotive kPaintings[] = {
            {"Kebab", 16, 16, 0, 0},     {"Aztec", 16, 16, 16, 0},     {"Alban", 16, 16, 32, 0},     {"Aztec2", 16, 16, 48, 0},
            {"Bomb", 16, 16, 64, 0},     {"Plant", 16, 16, 80, 0},     {"Wasteland", 16, 16, 96, 0}, {"Pool", 32, 16, 0, 32},
            {"Courbet", 32, 16, 32, 32}, {"Sea", 32, 16, 64, 32},      {"Sunset", 32, 16, 96, 32},   {"Creebet", 32, 16, 128, 32},
            {"Wanderer", 16, 32, 0, 64}, {"Graham", 16, 32, 16, 64},   {"Match", 32, 32, 0, 128},    {"Bust", 32, 32, 32, 128},
            {"Stage", 32, 32, 64, 128},  {"Void", 32, 32, 96, 128},    {"SkullAndRoses", 32, 32, 128, 128}, {"Wither", 32, 32, 160, 128},
            {"Fighters", 64, 32, 0, 96}, {"Pointer", 64, 64, 0, 192},  {"Pigscene", 64, 64, 64, 192}, {"BurningSkull", 64, 64, 128, 192},
            {"Skeleton", 64, 48, 192, 64}, {"DonkeyKong", 64, 48, 192, 112}, {"Earth", 32, 32, 0, 160}, {"Wind", 32, 32, 32, 160},
            {"Fire", 32, 32, 64, 160},   {"Water", 32, 32, 96, 160},   {"baroque", 32, 32, -1, 0},   {"humble", 32, 32, -1, 0},
            {"meditative", 16, 16, -1, 0}, {"prairie_ride", 16, 32, -1, 0}, {"unpacked", 64, 64, -1, 0}, {"backyard", 48, 64, -1, 0},
            {"bouquet", 48, 48, -1, 0},  {"cavebird", 48, 48, -1, 0},  {"changing", 64, 32, -1, 0},  {"cotan", 48, 48, -1, 0},
            {"endboss", 48, 48, -1, 0},  {"fern", 48, 48, -1, 0},      {"finding", 64, 32, -1, 0},   {"lowmist", 64, 32, -1, 0},
            {"orb", 64, 64, -1, 0},      {"owlemons", 48, 48, -1, 0},  {"passage", 64, 32, -1, 0},   {"pond", 48, 64, -1, 0},
            {"sunflowers", 48, 48, -1, 0}, {"tides", 48, 48, -1, 0},   {"dennis", 48, 48, -1, 0},
        };
    }

    void EntityRenderer::DrawPainting(const Entity& e, const FrameIn& in) {
        const auto title = e.propText.find("painting_motive");
        const auto direction = e.props.find("painting_direction");
        if (title == e.propText.end() || direction == e.props.end() || !renderer)
            return;
        const glm::dvec3 delta = e.pos - in.camera;
        if (glm::dot(delta, delta) > 96.0 * 96.0)
            return;
        const PaintingMotive* motive = &kPaintings[0];
        for (const PaintingMotive& m : kPaintings)
            if (title->second == m.title) {
                motive = &m;
                break;
            }
        static const float kYaw[4] = {180.0f, 90.0f, 0.0f, 270.0f};
        const int dir = std::clamp(static_cast<int>(direction->second), 0, 3);
        const float yaw = kYaw[dir] * kDeg;
        const float sn = std::sin(yaw), cs = std::cos(yaw);
        const float hw = static_cast<float>(motive->w) / 32.0f, hh = static_cast<float>(motive->h) / 32.0f;
        const float depth = 1.0f / 32.0f;
        const bool atlas = motive->ox >= 0;
        const uint32_t texture = TextureByPath(atlas ? "textures/painting/kz" : std::string("textures/painting/") + motive->title);
        const uint32_t borderTexture = atlas ? texture : TextureByPath("textures/painting/kz");
        if (!texture)
            return;
        const glm::dvec3 base(std::floor(in.camera.x), std::floor(in.camera.y), std::floor(in.camera.z));
        const glm::vec3 centre = glm::vec3(e.pos - base);
        const auto world = [&](float x, float y, float z) {
            return centre + glm::vec3(cs * x + sn * z, y, -sn * x + cs * z);
        };
        glm::vec3 lit(1.0f);
        if (in.light) {
            const glm::vec3 front(sn * -1.0f, 0.0f, cs * -1.0f);
            lit = in.light(e.pos + glm::dvec3(front.x * 0.6, 0.0, front.z * 0.6));
        }
        const uint32_t color = PackColor(lit);
        float fu0 = 0, fv0 = 0, fu1 = 1, fv1 = 1;
        float bu0 = 192.0f / 256.0f, bv0 = 0.0f, bu1 = 208.0f / 256.0f, bv1 = 16.0f / 256.0f;
        if (atlas) {
            fu0 = static_cast<float>(motive->ox) / 256.0f;
            fv0 = static_cast<float>(motive->oy) / 256.0f;
            fu1 = static_cast<float>(motive->ox + motive->w) / 256.0f;
            fv1 = static_cast<float>(motive->oy + motive->h) / 256.0f;
        }
        std::vector<UiVertex> front, border;
        std::vector<uint32_t> fi, bi;
        const auto quad = [](std::vector<UiVertex>& vv, std::vector<uint32_t>& ii, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                             uint32_t c, float u0, float v0, float u1, float v1) {
            const uint32_t i0 = static_cast<uint32_t>(vv.size());
            vv.push_back({p0.x, p0.y, p0.z, c, u0, v1});
            vv.push_back({p1.x, p1.y, p1.z, c, u1, v1});
            vv.push_back({p2.x, p2.y, p2.z, c, u1, v0});
            vv.push_back({p3.x, p3.y, p3.z, c, u0, v0});
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u})
                ii.push_back(i0 + k);
        };
        quad(front, fi, world(hw, -hh, -depth), world(-hw, -hh, -depth), world(-hw, hh, -depth), world(hw, hh, -depth), color, fu0, fv0, fu1, fv1);
        quad(border, bi, world(-hw, -hh, depth), world(hw, -hh, depth), world(hw, hh, depth), world(-hw, hh, depth), color, bu0, bv0, bu1, bv1);
        quad(border, bi, world(hw, -hh, depth), world(hw, -hh, -depth), world(hw, hh, -depth), world(hw, hh, depth), color, bu0, bv0, bu1, bv1);
        quad(border, bi, world(-hw, -hh, -depth), world(-hw, -hh, depth), world(-hw, hh, depth), world(-hw, hh, -depth), color, bu0, bv0, bu1, bv1);
        quad(border, bi, world(-hw, hh, depth), world(hw, hh, depth), world(hw, hh, -depth), world(-hw, hh, -depth), color, bu0, bv0, bu1, bv1);
        quad(border, bi, world(-hw, -hh, -depth), world(hw, -hh, -depth), world(hw, -hh, depth), world(-hw, -hh, depth), color, bu0, bv0, bu1, bv1);
        glm::mat4 model(1.0f);
        model[3] = glm::vec4(static_cast<float>(base.x), static_cast<float>(base.y), static_cast<float>(base.z), 1.0f);
        const auto submit = [&](std::vector<UiVertex>& vv, std::vector<uint32_t>& ii, uint32_t tex) {
            if (!tex || vv.empty())
                return;
            const uint32_t mesh = renderer->UiCreateFrameMesh(vv.data(), static_cast<uint32_t>(vv.size()), ii.data(), static_cast<uint32_t>(ii.size()));
            if (!mesh)
                return;
            pendingMeshes.push_back(mesh);
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = tex;
            std::copy_n(&model[0][0], 16, draw.model);
            draw.premultiplied = false;
            draw.alphaCutoff = 0.1f;
            draw.blend = 0;
            draw.depth = true;
            draw.depthWrite = true;
            draw.emissive = 0;
            draw.shadowOnly = shadowOnlyPass;
            renderer->UiSubmit(UiLayer::World, draw);
        };
        submit(front, fi, texture);
        submit(border, bi, borderTexture);
        drawn++;
    }

    namespace {
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
                    SkullModelDef d{tex, tw, th, {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f}}, {{0, -4, -8, -4, 8, 8, 8, 0, 0, 0, false}}};
                    if (hat)
                        d.boxes.push_back({0, -4, -8, -4, 8, 8, 8, 32, 0, 0.25f, false});
                    return d;
                };
                m[0] = plain("textures/entity/skeleton/skeleton", 64, 32, false);
                m[1] = plain("textures/entity/skeleton/wither_skeleton", 64, 32, false);
                m[2] = plain("textures/entity/zombie/zombie", 64, 32, true);
                m[3] = plain("textures/entity/steve", 64, 64, true);
                m[4] = plain("textures/entity/creeper/creeper", 64, 32, false);
                m[5] = SkullModelDef{"textures/entity/dragon/dragon", 256, 256,
                                     {{-1, glm::vec3(0.0f, -7.986666f, 0.0f), glm::vec3(0.0f), 0.75f},
                                      {0, glm::vec3(0.0f, 4.0f, -8.0f), glm::vec3(0.2f, 0.0f, 0.0f), 1.0f}},
                                     {{0, -6, -1, -24, 12, 5, 16, 176, 44, 0, false},
                                      {0, -8, -8, -10, 16, 16, 16, 112, 30, 0, false},
                                      {0, -5, -12, -4, 2, 4, 6, 0, 0, 0, true},
                                      {0, -5, -3, -22, 2, 2, 4, 112, 0, 0, true},
                                      {0, 3, -12, -4, 2, 4, 6, 0, 0, 0, false},
                                      {0, 3, -3, -22, 2, 2, 4, 112, 0, 0, false},
                                      {1, -6, 0, -16, 12, 4, 16, 176, 65, 0, false}}};
                m[6] = SkullModelDef{"textures/entity/piglin/piglin", 64, 64,
                                     {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f},
                                      {0, glm::vec3(4.5f, -6.0f, 0.0f), glm::vec3(0.0f, 0.0f, -0.7f), 1.0f},
                                      {0, glm::vec3(-4.5f, -6.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.7f), 1.0f}},
                                     {{0, -5, -8, -4, 10, 8, 8, 0, 0, 0, false},
                                      {0, -2, -4, -5, 4, 4, 1, 31, 1, 0, false},
                                      {0, 2, -2, -5, 1, 2, 1, 2, 4, 0, false},
                                      {0, -3, -2, -5, 1, 2, 1, 2, 0, 0, false},
                                      {1, 0, 0, -2, 1, 5, 4, 51, 6, 0, false},
                                      {2, -1, 0, -2, 1, 5, 4, 39, 6, 0, false}}};
                static const char* const kCopper[] = {"textures/entity/copper_golem/copper_golem", "textures/entity/copper_golem/copper_golem_exposed",
                                                      "textures/entity/copper_golem/copper_golem_weathered",
                                                      "textures/entity/copper_golem/copper_golem_oxidized"};
                for (const char* tex : kCopper)
                    m.push_back(SkullModelDef{tex, 64, 64,
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
                m.push_back(SkullModelDef{"textures/blocks/conduit_base", 32, 16,
                                          {{-1, glm::vec3(0.0f, -8.0f, 0.0f), glm::vec3(0.0f), 1.0f}},
                                          {{0, -3, -3, -3, 6, 6, 6, 0, 0, 0, false}}});
                m.push_back(SkullModelDef{"textures/entity/banner/banner_base", 64, 64,
                                          {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 2.0f / 3.0f},
                                           {0, glm::vec3(0.0f, -44.0f, 0.0f), glm::vec3(0.0f), 1.0f}},
                                          {{0, -1, -42, -1, 2, 42, 2, 44, 0, 0, false},
                                           {0, -10, -44, -1, 20, 2, 2, 0, 42, 0, false},
                                           {1, -10, 0, -2, 20, 40, 1, 0, 0, 0, false}},
                                          1});
                m.push_back(SkullModelDef{"textures/entity/banner/banner_base", 64, 64,
                                          {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 2.0f / 3.0f},
                                           {0, glm::vec3(0.0f, -20.5f, 10.5f), glm::vec3(0.0f), 1.0f}},
                                          {{0, -10, -20.5f, 9.5f, 20, 2, 2, 0, 42, 0, false},
                                           {1, -10, 0, -2, 20, 40, 1, 0, 0, 0, false}},
                                          1});
                const auto chest = [](const char* tex, bool large) {
                    const float w = large ? 30.0f : 14.0f;
                    SkullModelDef d{tex, large ? 128.0f : 64.0f, 64.0f,
                                    {{-1, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f}, {0, glm::vec3(-7.0f, -9.0f, 7.0f), glm::vec3(0.0f), 1.0f}},
                                    {{0, -7, -10, -7, w, 10, 14, 0, 19, 0, false},
                                     {1, 0, -5, -14, w, 5, 14, 0, 0, 0, false},
                                     {1, large ? 14.0f : 6.0f, -2, -15, 2, 4, 1, 0, 0, 0, false}}};
                    d.lidPart = 1;
                    return d;
                };
                for (const char* tex : {"textures/entity/chest/normal", "textures/entity/chest/trapped", "textures/entity/chest/ender",
                                        "textures/entity/chest/copper_default", "textures/entity/chest/copper_exposed",
                                        "textures/entity/chest/copper_weathered", "textures/entity/chest/copper_oxidized"})
                    m.push_back(chest(tex, false));
                for (const char* tex : {"textures/entity/chest/double_normal", "textures/entity/chest/trapped_double",
                                        "textures/entity/chest/copper_default_double", "textures/entity/chest/copper_exposed_double",
                                        "textures/entity/chest/copper_weathered_double", "textures/entity/chest/copper_oxidized_double"})
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
            m = glm::rotate(m, p.rotation.x + (part == def.flagPart || part == def.lidPart ? flagXRot : 0.0f), glm::vec3(1, 0, 0));
            return glm::scale(m, glm::vec3(p.scale));
        }
    }

    void EntityRenderer::RenderBlockEntities(const std::vector<BlockEntityDraw>& draws, const FrameIn& in) {
        if (!renderer || draws.empty())
            return;
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (root.empty())
            return;
        ui::McFont& font = ui::McFont::Instance();
        if (!fontTexture || fontTextureVersion != font.Version()) {
            if (fontTexture)
                renderer->UiDestroyTexture(fontTexture);
            fontTexture = renderer->UiCreateTexture(font.AtlasWidth(), font.AtlasHeight(), font.Atlas().data(), false);
            fontTextureVersion = font.Version();
        }
        const auto submit = [&](std::vector<UiVertex>& vv, std::vector<uint32_t>& ii, uint32_t texture, float cutoff) {
            if (vv.empty() || !texture)
                return;
            const uint32_t mesh = renderer->UiCreateFrameMesh(vv.data(), static_cast<uint32_t>(vv.size()), ii.data(), static_cast<uint32_t>(ii.size()));
            if (!mesh)
                return;
            pendingMeshes.push_back(mesh);
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = texture;
            draw.premultiplied = false;
            const bool overlay = cutoff < 0.0f;
            draw.alphaCutoff = overlay ? 0.004f : cutoff;
            draw.blend = overlay ? 1 : 0;
            draw.depth = true;
            draw.depthWrite = !overlay;
            draw.emissive = 0;
            draw.shadowOnly = shadowOnlyPass;
            renderer->UiSubmit(UiLayer::World, draw);
        };
        std::unordered_map<int, std::pair<std::vector<UiVertex>, std::vector<uint32_t>>> beds;
        struct SkullBatch {
            std::vector<UiVertex> v;
            std::vector<uint32_t> i;
            bool overlay = false;
        };
        std::map<std::string, SkullBatch> skulls;
        std::pair<std::vector<UiVertex>, std::vector<uint32_t>> text;
        const double maxDist = 64.0;
        for (const BlockEntityDraw& d : draws) {
            const glm::dvec3 centre = d.origin + glm::dvec3(0.5, 0.5, 0.5);
            const glm::dvec3 delta = centre - in.camera;
            const double dist2 = glm::dot(delta, delta);
            if (dist2 > maxDist * maxDist)
                continue;
            if (in.frustum && !in.frustum->Visible(static_cast<float>(d.origin.x) - 1.0f, static_cast<float>(d.origin.y) - 0.5f, static_cast<float>(d.origin.z) - 1.0f,
                                                   static_cast<float>(d.origin.x) + 2.0f, static_cast<float>(d.origin.y) + 1.5f, static_cast<float>(d.origin.z) + 2.0f))
                continue;
            if (in.sectionReached && !in.sectionReached(static_cast<int>(std::floor(d.origin.x)) >> 4,
                                                        static_cast<int>(std::floor(d.origin.y)) >> 4,
                                                        static_cast<int>(std::floor(d.origin.z)) >> 4))
                continue;
            glm::vec3 light(1.0f);
            if (in.light)
                light = in.light(centre);
            const float ryRad = d.yaw * kDeg;
            const float cs = std::cos(ryRad), sn = std::sin(ryRad);
            const auto rotate = [&](float lx, float lz, float& ox, float& oz) {
                const float dx = lx - 0.5f, dz = lz - 0.5f;
                ox = 0.5f + dx * cs - dz * sn;
                oz = 0.5f + dx * sn + dz * cs;
            };
            const glm::vec3 base(static_cast<float>(d.origin.x), static_cast<float>(d.origin.y), static_cast<float>(d.origin.z));
            if (d.kind == BlockEntityDraw::Kind::Bed) {
                auto& batch = beds[std::clamp(d.color, 0, 15)];
                struct BFace { int dir; float u0, v0, u1, v1; int tile; };
                struct BBox { float from[3], to[3]; std::vector<BFace> faces; };
                static const BBox kFoot[] = {
                    {{0, 3, 0}, {16, 9, 16}, {{0, 0, 0, 16, 16, 8}, {1, 0, 0, 16, 16, 0}, {3, 0, 7, 16, 13, 1}, {4, 0, 7, 16, 13, 3}, {5, 0, 7, 16, 13, 2}}},
                    {{0, 0, 13}, {3, 3, 16}, {{0, 7, 13, 10, 16, 3}, {2, 10, 13, 13, 16, 3}, {3, 0, 13, 3, 16, 1}, {4, 13, 13, 16, 16, 3}, {5, 3, 13, 6, 16, 1}}},
                    {{13, 0, 13}, {16, 3, 16}, {{0, 6, 13, 9, 16, 2}, {2, 3, 13, 6, 16, 2}, {3, 13, 13, 16, 16, 1}, {4, 10, 13, 13, 16, 1}, {5, 0, 13, 3, 16, 2}}},
                };
                static const BBox kHead[] = {
                    {{0, 3, 0}, {16, 9, 16}, {{0, 0, 0, 16, 16, 8}, {1, 0, 0, 16, 16, 4}, {2, 0, 7, 16, 13, 5}, {4, 0, 7, 16, 13, 7}, {5, 0, 7, 16, 13, 6}}},
                    {{0, 0, 0}, {3, 3, 3}, {{0, 6, 13, 9, 16, 7}, {2, 13, 13, 16, 16, 5}, {3, 3, 13, 6, 16, 7}, {4, 0, 13, 3, 16, 7}, {5, 10, 13, 13, 16, 5}}},
                    {{13, 0, 0}, {16, 3, 3}, {{0, 7, 13, 10, 16, 6}, {2, 0, 13, 3, 16, 5}, {3, 10, 13, 13, 16, 6}, {4, 3, 13, 6, 16, 5}, {5, 13, 13, 16, 16, 6}}},
                };
                const float kShade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
                const auto emitPiece = [&](const BBox* boxes, int count, float zOff) {
                    for (int bi = 0; bi < count; bi++) {
                        const BBox& bx = boxes[bi];
                        const float x0 = bx.from[0], y0 = bx.from[1], z0 = bx.from[2], x1 = bx.to[0], y1 = bx.to[1], z1 = bx.to[2];
                        const auto P = [&](float x, float y, float z) {
                            float ox, oz;
                            rotate(x / 16.0f, z / 16.0f + zOff, ox, oz);
                            return base + glm::vec3(ox, y / 16.0f, oz);
                        };
                        for (const BFace& f : bx.faces) {
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
                            const float uu0 = (tu + f.u0) / 64.0f, vv0 = (tv + f.v0) / 48.0f, uu1 = (tu + f.u1) / 64.0f, vv1 = (tv + f.v1) / 48.0f;
                            const uint32_t color = PackColor(light * kShade[f.dir]);
                            const uint32_t i0 = static_cast<uint32_t>(batch.first.size());
                            batch.first.push_back({tl.x, tl.y, tl.z, color, uu0, vv0});
                            batch.first.push_back({tr.x, tr.y, tr.z, color, uu1, vv0});
                            batch.first.push_back({br.x, br.y, br.z, color, uu1, vv1});
                            batch.first.push_back({bl.x, bl.y, bl.z, color, uu0, vv1});
                            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                                batch.second.push_back(i0 + k);
                        }
                    }
                };
                emitPiece(kFoot, 3, 0.0f);
                emitPiece(kHead, 3, -1.0f);
                continue;
            }
            if (d.kind == BlockEntityDraw::Kind::Skull) {
                const SkullModelDef& def = SkullModelFor(d.skull);
                const bool banner = def.flagPart >= 0;
                glm::mat4 place = glm::translate(glm::mat4(1.0f), base + glm::vec3(0.5f, d.onWall ? 0.25f : 0.0f, 0.5f));
                place = glm::rotate(place, -(d.yaw + 180.0f) * kDeg, glm::vec3(0, 1, 0));
                if (d.onWall)
                    place = glm::translate(place, glm::vec3(0.0f, 0.0f, 0.25f));
                place = glm::scale(place, glm::vec3(-1.0f / 16.0f, -1.0f / 16.0f, 1.0f / 16.0f));
                float flagXRot = 0.0f;
                if (banner) {
                    const double t = in.worldTicks + static_cast<double>(d.origin.x * 7.0 + d.origin.y * 9.0 + d.origin.z * 13.0);
                    const float phase = static_cast<float>(t - std::floor(t / 100.0) * 100.0) / 100.0f;
                    flagXRot = (-0.0125f + 0.01f * std::cos(6.2831853f * phase)) * 3.14159265f;
                }
                if (def.lidPart >= 0)
                    flagXRot = -d.lid * 1.5707963f;
                const auto emitBox = [&](SkullBatch& batch, const SkullBox& b, float extraGrow, const glm::vec3& tint) {
                    const glm::mat4 m = place * SkullPartMatrix(def, b.part, flagXRot);
                    const float g = b.grow + extraGrow;
                    float x0 = b.x - g, x1 = b.x + b.w + g;
                    const float y0 = b.y - g, y1 = b.y + b.h + g, z0 = b.z - g, z1 = b.z + b.d + g;
                    if (b.mirror)
                        std::swap(x0, x1);
                    const auto P = [&](float x, float y, float z) { return glm::vec3(m * glm::vec4(x, y, z, 1.0f)); };
                    const float u = b.u, v = b.v, w = b.w, h = b.h, dd = b.d;
                    const auto face = [&](glm::vec3 tl, glm::vec3 tr, glm::vec3 br, glm::vec3 bl, float fu, float fv, float fw, float fh, float shade) {
                        const uint32_t color = PackColor(light * shade * tint);
                        const uint32_t i0 = static_cast<uint32_t>(batch.v.size());
                        const float uu0 = fu / def.texW, vv0 = fv / def.texH, uu1 = (fu + fw) / def.texW, vv1 = (fv + fh) / def.texH;
                        batch.v.push_back({tl.x, tl.y, tl.z, color, uu0, vv0});
                        batch.v.push_back({tr.x, tr.y, tr.z, color, uu1, vv0});
                        batch.v.push_back({br.x, br.y, br.z, color, uu1, vv1});
                        batch.v.push_back({bl.x, bl.y, bl.z, color, uu0, vv1});
                        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                            batch.i.push_back(i0 + k);
                    };
                    face(P(x0, y0, z0), P(x1, y0, z0), P(x1, y1, z0), P(x0, y1, z0), u + dd, v + dd, w, h, 0.8f);
                    face(P(x0, y0, z1), P(x0, y0, z0), P(x0, y1, z0), P(x0, y1, z1), u, v + dd, dd, h, 0.6f);
                    face(P(x1, y0, z0), P(x1, y0, z1), P(x1, y1, z1), P(x1, y1, z0), u + dd + w, v + dd, dd, h, 0.6f);
                    face(P(x1, y0, z1), P(x0, y0, z1), P(x0, y1, z1), P(x1, y1, z1), u + 2 * dd + w, v + dd, w, h, 0.8f);
                    face(P(x0, y0, z1), P(x1, y0, z1), P(x1, y0, z0), P(x0, y0, z0), u + dd, v, w, dd, 1.0f);
                    face(P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1), u + dd + w, v, w, dd, 0.5f);
                };
                const auto dyeTint = [](int dye) {
                    const uint32_t rgb = DyeColorRgb(dye);
                    return glm::vec3(static_cast<float>((rgb >> 16) & 0xFF), static_cast<float>((rgb >> 8) & 0xFF), static_cast<float>(rgb & 0xFF)) / 255.0f;
                };
                SkullBatch& batch = skulls[def.texture];
                for (const SkullBox& b : def.boxes)
                    emitBox(batch, b, 0.0f, banner && b.part == def.flagPart ? dyeTint(d.color) : glm::vec3(1.0f));
                if (banner) {
                    int layer = 0;
                    for (const auto& [texture, dye] : d.patterns) {
                        layer++;
                        SkullBatch& over = skulls[texture];
                        over.overlay = true;
                        for (const SkullBox& b : def.boxes)
                            if (b.part == def.flagPart)
                                emitBox(over, b, 0.004f * static_cast<float>(layer), dyeTint(dye));
                    }
                }
                continue;
            }
            const bool hasFront = !d.front.empty(), hasBack = !d.back.empty();
            if (!hasFront && !hasBack)
                continue;
            if (dist2 > 32.0 * 32.0 || !fontTexture)
                continue;
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
            const auto emit = [&](const std::string& body, uint32_t argb, bool glow, bool frontSide, float planeZ) {
                if (body.empty() || planeZ < -0.5f)
                    return;
                const std::vector<std::string> lines = WrapSignText(body, maxWidth, 4);
                const float dir = frontSide ? 1.0f : -1.0f;
                const float rx = frontSide ? 1.0f : -1.0f;
                const float baseR = static_cast<float>(argb & 0xFF) / 255.0f, baseG = static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
                            baseB = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
                const glm::vec3 ink(baseB, baseG, baseR);
                const glm::vec3 lit = glow ? glm::vec3(1.0f) : light;
                const float top = -static_cast<float>(lines.size()) * lineHeight * 0.5f;
                for (size_t row = 0; row < lines.size(); row++) {
                    const std::string& line = lines[row];
                    const float width = font.Width(line);
                    float pen = -width * 0.5f;
                    glm::vec3 col = ink;
                    bool bold = false;
                    const float y0 = top + static_cast<float>(row) * lineHeight;
                    for (size_t i = 0; i < line.size();) {
                        uint32_t cp = 0;
                        if (!ui::McFont::Next(line, i, cp))
                            break;
                        if (cp == 0xA7 && i < line.size()) {
                            const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i++])));
                            uint32_t rgb = 0;
                            if (ui::FormatCodeColor(code, rgb)) {
                                col = glm::vec3(static_cast<float>((rgb >> 16) & 0xFF), static_cast<float>((rgb >> 8) & 0xFF), static_cast<float>(rgb & 0xFF)) / 255.0f;
                                bold = false;
                            } else if (code == 'l')
                                bold = true;
                            else if (code == 'r') {
                                col = ink;
                                bold = false;
                            }
                            continue;
                        }
                        if (cp < 0x20)
                            continue;
                        const ui::McFont::Glyph& g = font.Get(cp);
                        if (g.width > 0) {
                            const uint32_t color = PackColor(g.colored ? glm::vec3(1.0f) : col * lit);
                            for (int pass = 0; pass < (bold ? 2 : 1); pass++) {
                                const float gx0 = pen + g.left + static_cast<float>(pass), gx1 = gx0 + static_cast<float>(g.width);
                                const auto corner = [&](float gx, float gy) {
                                    float ox, oz;
                                    const float lx = 0.5f + rx * gx * scale, lz = planeZ + dir * 0.0f;
                                    rotate(lx, lz, ox, oz);
                                    return base + glm::vec3(ox, centerY - gy * scale, oz);
                                };
                                const uint32_t i0 = static_cast<uint32_t>(text.first.size());
                                const glm::vec3 p0 = corner(gx0, y0 + g.top + g.height), p1 = corner(gx1, y0 + g.top + g.height), p2 = corner(gx1, y0 + g.top), p3 = corner(gx0, y0 + g.top);
                                text.first.push_back({p0.x, p0.y, p0.z, color, g.u0, g.v1});
                                text.first.push_back({p1.x, p1.y, p1.z, color, g.u1, g.v1});
                                text.first.push_back({p2.x, p2.y, p2.z, color, g.u1, g.v0});
                                text.first.push_back({p3.x, p3.y, p3.z, color, g.u0, g.v0});
                                for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                                    text.second.push_back(i0 + k);
                            }
                        }
                        pen += static_cast<float>(g.advance) + (bold ? 1.0f : 0.0f);
                    }
                }
            };
            emit(d.front, d.frontColor, d.frontGlow, true, frontZ);
            emit(d.back, d.backColor, d.backGlow, false, backZ);
        }
        for (auto& [texture, batch] : skulls)
            if (!batch.overlay)
                submit(batch.v, batch.i, TextureByPath(texture), 0.1f);
        for (auto& [texture, batch] : skulls)
            if (batch.overlay)
                submit(batch.v, batch.i, TextureByPath(texture), -1.0f);
        for (auto& [color, batch] : beds)
            submit(batch.first, batch.second, TextureByPath(std::string("bed_") + kDyeNames[color] + ".png"), 0.0f);
        submit(text.first, text.second, fontTexture, 0.1f);
    }
}
