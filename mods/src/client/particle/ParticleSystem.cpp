#include "client/particle/ParticleSystem.hpp"

#include <algorithm>
#include <cmath>

#include "world/level/block/BlockPalette.hpp"

namespace perlin::particle {

    namespace {
        enum Ambient : int8_t {
            kNone = -1,
            kUnknown = 0,
            kTorch = 1,
            kSoulTorch = 2,
            kRedstoneTorch = 3,
            kLava = 4,
            kFire = 5,
            kCandle = 6,
            kBubbleUp = 7,
            kBubbleDown = 8
        };

        uint32_t Pack(const glm::vec3& c, float a) {
            const auto b = [](float v) {
                return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return (b(a) << 24) | (b(c.z) << 16) | (b(c.y) << 8) | b(c.x);
        }

        int GenericColumn(int age, int lifetime) {
            const int index = std::clamp(age * 7 / std::max(lifetime, 1), 0, 7);
            return 7 - index;
        }
    }

    void ParticleSystem::Base(Particle& p, const glm::dvec3& at, const glm::dvec3& velArg) {
        p.pos = p.prev = at;
        glm::dvec3 v = velArg + glm::dvec3(Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1) * 0.4;
        const double speed = (Rand() + Rand() + 1.0) * 0.15;
        double dist = glm::length(v);
        if (dist < 1e-9)
            dist = 1.0;
        v = v / dist * speed * 0.4;
        v.y += 0.1;
        p.vel = v;
        p.quadSize = 0.1f * (Rand() * 0.5f + 0.5f) * 2.0f;
        p.lifetime = static_cast<int>(4.0f / (Rand() * 0.9f + 0.1f));
    }

    void ParticleSystem::Destroy(int x, int y, int z, uint16_t id) {
        TileChoice choice;
        if (!tileFor || !tileFor(id, x, y, z, choice))
            return;
        constexpr int kN = 4;
        for (int i = 0; i < kN; i++)
            for (int j = 0; j < kN; j++)
                for (int k = 0; k < kN; k++) {
                    const double px = x + (i + 0.5) / kN, py = y + (j + 0.5) / kN, pz = z + (k + 0.5) / kN;
                    Particle p;
                    p.kind = Kind::Terrain;
                    Base(p, {px, py, pz}, {px - x - 0.5, py - y - 0.5, pz - z - 0.5});
                    p.gravity = 1.0f;
                    p.colour = glm::vec3(0.6f) * choice.tint;
                    p.quadSize /= 2.0f;
                    p.tile = choice.tile;
                    p.u = Rand() * 3.0f;
                    p.v = Rand() * 3.0f;
                    particles.push_back(p);
                }
    }

    void ParticleSystem::Crack(int x, int y, int z, int face, uint16_t id) {
        TileChoice choice;
        if (!tileFor || !tileFor(id, x, y, z, choice))
            return;
        const float r = 0.1f;
        double px = x + Rand() * (1.0 - 2 * r) + r, py = y + Rand() * (1.0 - 2 * r) + r,
               pz = z + Rand() * (1.0 - 2 * r) + r;
        switch (face) {
        case 0:
            py = y - r;
            break;
        case 1:
            py = y + 1.0 + r;
            break;
        case 2:
            pz = z - r;
            break;
        case 3:
            pz = z + 1.0 + r;
            break;
        case 4:
            px = x - r;
            break;
        case 5:
            px = x + 1.0 + r;
            break;
        default:
            break;
        }
        Particle p;
        p.kind = Kind::Terrain;
        Base(p, {px, py, pz}, glm::dvec3(0.0));
        p.gravity = 1.0f;
        p.colour = glm::vec3(0.6f) * choice.tint;
        p.quadSize /= 2.0f;
        p.vel.x *= 0.2;
        p.vel.y = (p.vel.y - 0.1) * 0.2 + 0.1;
        p.vel.z *= 0.2;
        p.quadSize *= 0.6f;
        p.tile = choice.tile;
        p.u = Rand() * 3.0f;
        p.v = Rand() * 3.0f;
        particles.push_back(p);
    }

    void ParticleSystem::Spawn(Kind kind, const glm::dvec3& at, const glm::dvec3& velocity, float scale,
                               const glm::vec3& colour) {
        Particle p;
        p.kind = kind;
        switch (kind) {
        case Kind::Smoke:
        case Kind::LargeSmoke: {
            Base(p, at, glm::dvec3(0.0));
            const float s = kind == Kind::LargeSmoke ? 2.5f * scale : scale;
            p.friction = 0.96f;
            p.vel = p.vel * 0.1 + velocity;
            const float f = Rand() * 0.3f;
            p.colour = colour.x >= 0 ? colour : glm::vec3(f);
            p.quadSize *= 0.75f * s;
            p.lifetime = std::max(1, static_cast<int>(8.0f / (Rand() * 0.8f + 0.2f) * s));
            p.gravity = -0.1f;
            p.byAge = true;
            break;
        }
        case Kind::Flame: {
            Base(p, at, glm::dvec3(0.0));
            p.friction = 0.96f;
            p.vel = p.vel * 0.01 + velocity;
            p.pos += glm::dvec3(Rand() - Rand(), Rand() - Rand(), Rand() - Rand()) * 0.05;
            p.prev = p.pos;
            p.lifetime = static_cast<int>(8.0f / (Rand() * 0.8f + 0.2f)) + 4;
            p.quadSize *= scale;
            p.cellX = 0;
            p.cellY = 3;
            p.fullbright = true;
            if (colour.x >= 0)
                p.colour = colour;
            break;
        }
        case Kind::Lava: {
            Base(p, at, glm::dvec3(0.0));
            p.vel = p.vel * 0.8;
            p.vel.y = Rand() * 0.4 + 0.05;
            p.quadSize *= Rand() * 2.0f + 0.2f;
            p.lifetime = static_cast<int>(16.0f / (Rand() * 0.8f + 0.2f));
            p.gravity = 0.75f;
            p.friction = 0.999f;
            p.cellX = 1;
            p.cellY = 3;
            p.fullbright = true;
            break;
        }
        case Kind::Heart:
        case Kind::Angry: {
            Base(p, at, glm::dvec3(0.0));
            p.friction = 0.86f;
            p.vel = p.vel * 0.01 + glm::dvec3(velocity.x, velocity.y + 0.1, velocity.z);
            p.quadSize *= 1.5f * scale;
            p.lifetime = 16;
            p.physics = false;
            p.cellX = kind == Kind::Heart ? 0 : 1;
            p.cellY = 5;
            p.fullbright = true;
            break;
        }
        case Kind::Happy: {
            Base(p, at, glm::dvec3(0.0));
            p.vel = glm::dvec3(Rand() - 0.5, Rand() * 0.2, Rand() - 0.5) * 0.02 + velocity;
            p.friction = 0.9f;
            p.quadSize = 0.1f * (Rand() * 0.5f + 0.5f) * 1.2f;
            p.lifetime = 20 + RandInt(10);
            p.physics = false;
            p.cellX = 2;
            p.cellY = 5;
            p.fullbright = true;
            break;
        }
        case Kind::Note: {
            Base(p, at, glm::dvec3(0.0));
            p.friction = 0.66f;
            p.vel = glm::dvec3(p.vel.x * 0.01, p.vel.y * 0.01 + 0.2, p.vel.z * 0.01) + velocity;
            p.quadSize *= 1.5f;
            p.lifetime = 6;
            p.physics = false;
            p.cellX = 0;
            p.cellY = 4;
            p.fullbright = true;
            const float h = static_cast<float>(velocity.x);
            p.colour = glm::vec3(std::max(0.0f, std::sin((h + 0.0f) * 6.2831853f) * 0.65f + 0.35f),
                                 std::max(0.0f, std::sin((h + 0.33333334f) * 6.2831853f) * 0.65f + 0.35f),
                                 std::max(0.0f, std::sin((h + 0.6666667f) * 6.2831853f) * 0.65f + 0.35f));
            p.vel = glm::dvec3(0, 0.2, 0);
            break;
        }
        case Kind::Bubble: {
            Base(p, at, glm::dvec3(0.0));
            p.quadSize *= Rand() * 0.6f + 0.2f;
            p.vel = p.vel * 0.2 + velocity;
            p.lifetime = static_cast<int>(8.0f / (Rand() * 0.8f + 0.2f));
            p.gravity = -0.05f;
            p.friction = 0.85f;
            p.physics = false;
            p.cellX = 0;
            p.cellY = 2;
            break;
        }
        case Kind::Splash: {
            Base(p, at, glm::dvec3(0.0));
            p.vel = p.vel * 0.3 + velocity;
            p.vel.y = Rand() * 0.2 + 0.1;
            p.gravity = 0.04f;
            p.lifetime = static_cast<int>(8.0f / (Rand() * 0.8f + 0.2f));
            p.cellX = static_cast<int>(rng() % 4);
            p.cellY = 1;
            p.colour = colour.x >= 0 ? colour : glm::vec3(1.0f);
            break;
        }
        case Kind::Poof: {
            Base(p, at, glm::dvec3(0.0));
            p.vel = glm::dvec3(Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1) * 0.05 + velocity;
            const float g = Rand() * 0.3f + 0.7f;
            p.colour = colour.x >= 0 ? colour : glm::vec3(g);
            p.quadSize = 0.1f * (Rand() * 0.5f + 0.5f) * 2.0f * scale * 2.0f;
            p.lifetime = static_cast<int>(16.0f / (Rand() * 0.8f + 0.2f)) + 2;
            p.friction = 0.9f;
            p.physics = false;
            p.byAge = true;
            break;
        }
        case Kind::Crit: {
            Base(p, at, glm::dvec3(0.0));
            p.vel = glm::dvec3(Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1) * 0.4 * 0.4 + velocity;
            const float b = Rand() * 0.3f + 0.6f;
            p.colour = glm::vec3(b, b * 0.9f, b * 0.8f);
            p.quadSize *= 0.75f;
            p.lifetime = std::max(1, static_cast<int>(6.0f / (Rand() * 0.8f + 0.6f)));
            p.friction = 0.7f;
            p.gravity = 0.5f;
            p.byAge = true;
            p.fullbright = true;
            break;
        }
        case Kind::Dust: {
            Base(p, at, glm::dvec3(0.0));
            p.vel = glm::dvec3(0.0);
            const float shade = 0.75f + Rand() * 0.25f;
            p.colour = colour.x >= 0 ? colour * shade : glm::vec3(1.0f * shade, 0.0f, 0.0f);
            p.quadSize *= 0.75f * scale;
            p.lifetime = std::max(1, static_cast<int>(8.0f / (Rand() * 0.8f + 0.2f) * scale));
            p.physics = false;
            p.byAge = true;
            break;
        }
        case Kind::Totem: {
            Base(p, at, glm::dvec3(0.0));
            p.vel = velocity;
            p.friction = 0.6f;
            p.gravity = 0.6f;
            p.quadSize *= 0.75f;
            p.lifetime = 60 + RandInt(12);
            p.byAge = true;
            p.colour = RandInt(4) == 0 ? glm::vec3(0.6f + Rand() * 0.2f, 0.6f + Rand() * 0.3f, Rand() * 0.2f)
                                       : glm::vec3(0.1f + Rand() * 0.2f, 0.4f + Rand() * 0.3f, Rand() * 0.2f);
            break;
        }
        case Kind::Drip: {
            Base(p, at, glm::dvec3(0.0));
            p.vel = glm::dvec3(0.0);
            p.quadSize = 0.06f;
            p.lifetime = 40;
            p.gravity = 0.06f;
            p.cellX = 1;
            p.cellY = 7;
            p.colour = colour.x >= 0 ? colour : glm::vec3(0.2f, 0.4f, 1.0f);
            break;
        }
        default:
            return;
        }
        particles.push_back(p);
    }

    bool ParticleSystem::SpawnNamed(const std::string& rawName, const glm::dvec3& at) {
        std::string name = rawName;
        if (name.starts_with("minecraft:"))
            name = name.substr(10);
        const auto has = [&](const char* s) { return name.contains(s); };
        const auto burst = [&](Kind k, int n, double spread, float scale = 1.0f) {
            for (int i = 0; i < n; i++)
                Spawn(k,
                      at +
                          glm::dvec3((Rand() - 0.5) * spread, Rand() * spread * 0.5, (Rand() - 0.5) * spread),
                      glm::dvec3(0.0), scale);
        };
        if (has("heart")) {
            burst(Kind::Heart, 1, 0.6);
            return true;
        }
        if (has("villager_angry") || has("angry")) {
            burst(Kind::Angry, 1, 0.6);
            return true;
        }
        if (has("villager_happy") || has("crop_growth") || has("happy")) {
            burst(Kind::Happy, 6, 1.2);
            return true;
        }
        if (has("critical_hit") || has("crit")) {
            burst(Kind::Crit, 8, 0.6);
            return true;
        }
        if (has("large_smoke") || has("campfire")) {
            burst(Kind::LargeSmoke, 1, 0.3);
            return true;
        }
        if (has("explosion") || has("death_smoke") || has("evaporation") || has("poof")) {
            burst(Kind::Poof, 8, 1.2);
            return true;
        }
        if (has("smoke")) {
            burst(Kind::Smoke, 1, 0.2);
            return true;
        }
        if (has("lava")) {
            burst(Kind::Lava, 1, 0.3);
            return true;
        }
        if (has("flame") || has("fire")) {
            burst(Kind::Flame, 1, 0.2);
            return true;
        }
        if (has("splash")) {
            burst(Kind::Splash, 8, 0.8);
            return true;
        }
        if (has("bubble")) {
            burst(Kind::Bubble, 2, 0.5);
            return true;
        }
        if (has("note")) {
            burst(Kind::Note, 1, 0.2);
            return true;
        }
        if (has("redstone") || has("dust")) {
            burst(Kind::Dust, 1, 0.2);
            return true;
        }
        if (has("drip")) {
            burst(Kind::Drip, 1, 0.1);
            return true;
        }
        if (has("totem")) {
            AddTotemEmitter(at);
            return true;
        }
        return false;
    }

    bool ParticleSystem::SpawnLevelEvent(int event, const glm::dvec3& at, int data) {
        (void)data;
        const auto burst = [&](Kind k, int n, double spread, float scale = 1.0f) {
            for (int i = 0; i < n; i++)
                Spawn(k, at + glm::dvec3((Rand() - 0.5) * spread, Rand() * spread, (Rand() - 0.5) * spread),
                      glm::dvec3(0.0), scale);
        };
        switch (event) {
        case 2005:
            burst(Kind::Happy, 15, 1.5);
            return true;
        case 2007:
            burst(Kind::Poof, 20, 1.2);
            return true;
        case 2012:
            burst(Kind::Crit, 16, 0.8);
            return true;
        case 2015:
            burst(Kind::Bubble, 8, 0.8);
            return true;
        case 2016:
        case 2020:
            burst(Kind::Smoke, 6, 0.6);
            return true;
        case 20065:
            AddTotemEmitter(at);
            return true;
        case 2025:
        case 2026:
            burst(Kind::Poof, 24, 3.0, 1.5f);
            return true;
        case 16385:
        case 16386:
            burst(Kind::Bubble, 1, 0.1);
            return true;
        case 16387:
            burst(Kind::Crit, 1, 0.1);
            return true;
        case 16389:
            burst(Kind::Smoke, 1, 0.1);
            return true;
        case 16390:
            burst(Kind::Poof, 1, 0.1);
            return true;
        case 16391:
            burst(Kind::Smoke, 1, 0.1);
            return true;
        case 16392:
        case 16393:
        case 16402:
            burst(Kind::Flame, 1, 0.1);
            return true;
        case 16394:
            burst(Kind::Lava, 1, 0.1);
            return true;
        case 16395:
            burst(Kind::LargeSmoke, 1, 0.1);
            return true;
        case 16396:
        case 16397:
            burst(Kind::Dust, 1, 0.1);
            return true;
        case 16403:
            burst(Kind::Heart, 1, 0.1);
            return true;
        case 16408:
        case 16409:
        case 16410:
            burst(Kind::Splash, 4, 0.4);
            return true;
        case 16411:
        case 16412:
            burst(Kind::Drip, 1, 0.1);
            return true;
        default:
            return false;
        }
    }

    void ParticleSystem::Step(Particle& p, const WorldAccess& world) {
        p.prev = p.pos;
        if (p.age++ >= p.lifetime) {
            p.age = p.lifetime + 1;
            return;
        }
        if (p.kind == Kind::Lava) {
            if (Rand() > static_cast<float>(p.age) / static_cast<float>(p.lifetime))
                pendingSmoke.emplace_back(p.pos, p.vel);
        }
        p.vel.y -= 0.04 * p.gravity;
        const auto blocked = [&](double x, double y, double z) {
            return world.solid &&
                   world.solid(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)),
                               static_cast<int>(std::floor(z)));
        };
        const double half = 0.1;
        if (p.physics && world.solid) {
            glm::dvec3 d = p.vel;
            p.onGround = false;
            if (d.y != 0) {
                const double ny = p.pos.y + d.y;
                const double probe = d.y < 0 ? ny : ny + 0.2;
                if (blocked(p.pos.x, probe, p.pos.z)) {
                    if (d.y < 0) {
                        p.pos.y = std::floor(probe) + 1.0;
                        p.onGround = true;
                    } else {
                        p.pos.y = std::floor(probe) - 0.2;
                    }
                    p.vel.y = 0;
                } else {
                    p.pos.y = ny;
                }
            }
            for (int axis : {0, 2}) {
                if (d[axis] == 0)
                    continue;
                const double nv = p.pos[axis] + d[axis];
                glm::dvec3 probe = p.pos;
                probe[axis] = nv + (d[axis] > 0 ? half : -half);
                if (blocked(probe.x, p.pos.y + 0.1, probe.z)) {
                    p.pos[axis] =
                        d[axis] > 0 ? std::floor(probe[axis]) - half : std::floor(probe[axis]) + 1.0 + half;
                    p.vel[axis] = 0;
                } else {
                    p.pos[axis] = nv;
                }
            }
        } else {
            p.pos += p.vel;
        }
        if (p.kind == Kind::Flame || p.kind == Kind::Smoke || p.kind == Kind::LargeSmoke) {
            if (p.pos.y == p.prev.y) {
                p.vel.x *= 1.1;
                p.vel.z *= 1.1;
            }
        }
        p.vel *= p.friction;
        if (p.onGround) {
            p.vel.x *= 0.7;
            p.vel.z *= 0.7;
        }
    }

    void ParticleSystem::AddTotemEmitter(const glm::dvec3& at) {
        emitters.push_back({at, 0, 30});
    }

    void ParticleSystem::Tick(const WorldAccess& world, const glm::dvec3& around) {
        for (Emitter& e : emitters) {
            for (int i = 0; i < 16; i++) {
                const double dx = Rand() * 2.0 - 1.0, dy = Rand() * 2.0 - 1.0, dz = Rand() * 2.0 - 1.0;
                if (dx * dx + dy * dy + dz * dz > 1.0)
                    continue;
                Spawn(Kind::Totem, e.at + glm::dvec3(dx / 4.0, dy / 4.0, dz / 4.0) * 1.8,
                      glm::dvec3(dx, dy + 0.2, dz));
            }
            e.age++;
        }
        emitters.erase(std::remove_if(emitters.begin(), emitters.end(),
                                      [](const Emitter& e) { return e.age >= e.lifetime; }),
                       emitters.end());
        for (Particle& p : particles)
            Step(p, world);
        for (const auto& [at, vel] : pendingSmoke)
            Spawn(Kind::Smoke, at, vel);
        pendingSmoke.clear();
        particles.erase(std::remove_if(particles.begin(), particles.end(),
                                       [](const Particle& p) { return p.age > p.lifetime; }),
                        particles.end());
        if (particles.size() > 6000)
            particles.erase(particles.begin(),
                            particles.begin() + static_cast<long>(particles.size() - 6000));
        Ambient(world, around);
    }

    void ParticleSystem::Ambient(const WorldAccess& world, const glm::dvec3& around) {
        if (!world.block)
            return;
        auto& pal = BlockPalette::Instance();
        const int cx = static_cast<int>(std::floor(around.x)), cy = static_cast<int>(std::floor(around.y)),
                  cz = static_cast<int>(std::floor(around.z));
        for (int i = 0; i < 667; i++) {
            const int x = cx + RandInt(16) - RandInt(16), y = cy + RandInt(16) - RandInt(16),
                      z = cz + RandInt(16) - RandInt(16);
            const uint16_t id = world.block(x, y, z);
            if (id == 0)
                continue;
            if (id >= ambientKind.size())
                ambientKind.resize(static_cast<size_t>(id) + 256, kUnknown);
            int8_t& kind = ambientKind[id];
            if (kind == kUnknown) {
                const std::string name = pal.Name(id);
                kind = kNone;
                if (name == "minecraft:torch" || name == "minecraft:copper_torch")
                    kind = kTorch;
                else if (name == "minecraft:soul_torch")
                    kind = kSoulTorch;
                else if (name == "minecraft:redstone_torch")
                    kind = kRedstoneTorch;
                else if (name == "minecraft:lava" || name == "minecraft:flowing_lava")
                    kind = kLava;
                else if (name == "minecraft:fire")
                    kind = kFire;
                else if (name == "minecraft:bubble_column")
                    kind = pal.StateInt(id, "drag_down", 0) != 0 ? kBubbleDown : kBubbleUp;
            }
            if (kind == kNone)
                continue;
            const double bx = x + 0.5, bz = z + 0.5;
            switch (kind) {
            case kTorch:
            case kSoulTorch: {
                double ox = 0, oz = 0, oy = 0.7;
                const std::string facing = pal.StateString(id, "torch_facing_direction", "top");
                if (facing != "top" && facing != "unknown") {
                    oy = 0.92;
                    ox = facing == "east" ? -0.27 : facing == "west" ? 0.27 : 0;
                    oz = facing == "south" ? -0.27 : facing == "north" ? 0.27 : 0;
                }
                const glm::dvec3 at(bx + ox, y + oy, bz + oz);
                Spawn(Kind::Smoke, at);
                Spawn(Kind::Flame, at);
                if (kind == kSoulTorch && !particles.empty()) {
                    particles.back().cellX = 2;
                    particles.back().cellY = 3;
                }
                break;
            }
            case kRedstoneTorch: {
                Spawn(Kind::Dust,
                      {bx + (Rand() - 0.5) * 0.2, y + 0.7 + (Rand() - 0.5) * 0.2, bz + (Rand() - 0.5) * 0.2});
                break;
            }
            case kLava: {
                if (world.block(x, y + 1, z) == 0 && RandInt(100) == 0)
                    Spawn(Kind::Lava, {x + Rand(), y + 1.0, z + Rand()});
                break;
            }
            case kFire: {
                for (int n = 0; n < 3; n++)
                    Spawn(Kind::LargeSmoke, {x + Rand(), y + Rand() * 0.5 + 0.5, z + Rand()});
                break;
            }
            case kBubbleUp: {
                Spawn(Kind::Bubble, {bx, y + 0.1, bz}, glm::dvec3(0.0, 0.2, 0.0));
                Spawn(Kind::Bubble, {x + Rand(), y + Rand(), z + Rand()}, glm::dvec3(0.0, 0.2, 0.0));
                break;
            }
            case kBubbleDown: {
                Spawn(Kind::Bubble, {bx, y + 0.8, z + Rand()}, glm::dvec3(0.0, -0.2, 0.0));
                if (!particles.empty()) {
                    particles.back().gravity = 0.05f;
                    particles.back().colour = glm::vec3(0.75f, 0.8f, 1.0f);
                }
                break;
            }
            default:
                break;
            }
        }
    }

    void ParticleSystem::Collect(float partial, const WorldAccess& world, std::vector<Quad>& out) const {
        out.reserve(out.size() + particles.size());
        for (const Particle& p : particles) {
            Quad q;
            q.centre = p.prev + (p.pos - p.prev) * static_cast<double>(partial);
            const float t =
                (static_cast<float>(p.age) + partial) / static_cast<float>(std::max(p.lifetime, 1));
            float size = p.quadSize;
            switch (p.kind) {
            case Kind::Flame:
                size *= 1.0f - t * t * 0.5f;
                break;
            case Kind::Lava:
                size *= 1.0f - t * t;
                break;
            case Kind::Poof:
                size *= 0.5f + t * 0.8f;
                break;
            default:
                break;
            }
            q.half = std::max(size, 0.0f);
            glm::vec3 lit(1.0f);
            if (world.lightLevels) {
                const glm::ivec2 l = p.fullbright ? glm::ivec2(15, 15) : world.lightLevels(q.centre);
                q.blockLight = static_cast<int8_t>(l.x);
                q.skyLight = static_cast<int8_t>(l.y);
            } else if (!p.fullbright && world.light) {
                lit = world.light(q.centre);
            }
            q.abgr = Pack(p.colour * lit, p.alpha);
            if (p.kind == Kind::Terrain) {
                q.terrainTile = p.tile;
                const float cu = std::floor(p.u) / 4.0f, cv = std::floor(p.v) / 4.0f;
                q.u0 = cu;
                q.v0 = cv;
                q.u1 = cu + 0.25f;
                q.v1 = cv + 0.25f;
            } else {
                int cx = p.cellX, cy = p.cellY;
                if (p.byAge) {
                    cx = GenericColumn(p.age, p.lifetime);
                    cy = 0;
                }
                const float step = 1.0f / 16.0f, inset = 0.0004f;
                q.u0 = cx * step + inset;
                q.v0 = cy * step + inset;
                q.u1 = (cx + 1) * step - inset;
                q.v1 = (cy + 1) * step - inset;
            }
            out.push_back(q);
        }
    }

}
