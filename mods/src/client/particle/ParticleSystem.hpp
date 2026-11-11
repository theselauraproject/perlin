#pragma once
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace perlin::particle {

    enum class Kind : uint8_t {
        Terrain,
        Smoke,
        LargeSmoke,
        Flame,
        Lava,
        Heart,
        Angry,
        Happy,
        Note,
        Bubble,
        Splash,
        Poof,
        Crit,
        Dust,
        Drip,
        Totem,
    };

    struct Quad {
        glm::dvec3 centre{0.0};
        float half = 0.1f;
        uint32_t abgr = 0xFFFFFFFFu;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        int terrainTile = -1;
        bool additive = false;
        int8_t blockLight = -1, skyLight = -1; // java packs: lightmap levels (-1: unknown)
    };

    struct WorldAccess {
        std::function<bool(int, int, int)> solid;
        std::function<uint16_t(int, int, int)> block;
        std::function<glm::vec3(const glm::dvec3&)> light;
        std::function<std::string(uint16_t)> blockName;
        // set while a java pack draws: colours stay plain and quads carry the light levels instead
        std::function<glm::ivec2(const glm::dvec3&)> lightLevels;
    };

    struct TileChoice {
        int tile = 0;
        glm::vec3 tint{1.0f};
    };

    class ParticleSystem {
    public:
        std::function<bool(uint16_t, int, int, int, TileChoice&)> tileFor;

        void Destroy(int x, int y, int z, uint16_t id);
        void Crack(int x, int y, int z, int face, uint16_t id);
        void Spawn(Kind kind, const glm::dvec3& at, const glm::dvec3& velocity = glm::dvec3(0.0),
                   float scale = 1.0f, const glm::vec3& colour = glm::vec3(-1.0f));
        bool SpawnNamed(const std::string& name, const glm::dvec3& at);
        bool SpawnLevelEvent(int event, const glm::dvec3& at, int data);

        void AddTotemEmitter(const glm::dvec3& at);
        void Tick(const WorldAccess& world, const glm::dvec3& around);
        void Collect(float partial, const WorldAccess& world, std::vector<Quad>& out) const;

        size_t Count() const { return particles.size(); }
        void Clear() { particles.clear(); }

    private:
        struct Particle {
            Kind kind = Kind::Poof;
            glm::dvec3 pos{0.0}, prev{0.0}, vel{0.0};
            int age = 0, lifetime = 20;
            float quadSize = 0.1f;
            float gravity = 0, friction = 0.98f;
            bool physics = true, onGround = false;
            glm::vec3 colour{1.0f};
            float alpha = 1.0f;
            int tile = -1;
            float u = 0, v = 0;
            float grow = 0;
            bool fullbright = false;
            bool additive = false;
            int cellX = 0, cellY = 0;
            bool byAge = false;
        };
        std::vector<Particle> particles;
        struct Emitter {
            glm::dvec3 at;
            int age = 0, lifetime = 30;
        };
        std::vector<Emitter> emitters;
        std::mt19937 rng{0x5EEDu};
        float Rand() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
        int RandInt(int n) { return n <= 1 ? 0 : static_cast<int>(rng() % static_cast<uint32_t>(n)); }
        void Base(Particle& p, const glm::dvec3& at, const glm::dvec3& velArg);
        void Step(Particle& p, const WorldAccess& world);
        void Ambient(const WorldAccess& world, const glm::dvec3& around);
        std::vector<std::pair<glm::dvec3, glm::dvec3>> pendingSmoke;
        std::vector<int8_t> ambientKind;
    };

}
