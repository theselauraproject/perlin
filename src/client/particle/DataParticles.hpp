#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "client/attachable/Molang.hpp"

namespace perlin::particle {
    struct CustomQuad {
        glm::dvec3 centre{0.0};
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        uint32_t abgr = 0xFFFFFFFFu;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        std::string texture;
        int blend = 0;
        int8_t blockLight = -1, skyLight = -1;
    };

    struct CameraView {
        glm::dvec3 position{0.0};
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
    };

    struct DataWorld {
        std::function<bool(int, int, int)> solid;
        std::function<glm::vec3(const glm::dvec3&)> light;
        std::function<glm::ivec2(const glm::dvec3&)> lightLevels;
    };

    class DataParticles {
    public:
        void Configure(const std::vector<std::string>& packDirs);
        bool Has(const std::string& identifier) const;

        bool Spawn(const std::string& identifier, const glm::dvec3& at, const std::string& molangVariables = {},
                   const std::string& preScript = {});
        void Update(float dt, const DataWorld& world);
        void Collect(const CameraView& camera, const DataWorld& world, std::vector<CustomQuad>& out) const;
        size_t Count() const;
        void Clear();

    private:

        std::unordered_map<std::string, std::shared_ptr<molang::Expr>> preScripts;
        using ExprPtr = std::shared_ptr<molang::Expr>;

        struct Curve {
            std::string name;
            std::string type;
            ExprPtr input, range;
            std::vector<ExprPtr> nodes;
        };

        struct Gradient {
            std::vector<std::pair<float, glm::vec4>> stops;
        };

        struct Def {
            std::string identifier;
            std::string texture = "textures/particle/particles";
            int blend = 0;
            std::vector<Curve> curves;

            int rateKind = 0;
            ExprPtr instantCount, steadyRate, steadyMax;
            int lifeKind = 0;
            ExprPtr activeTime, sleepTime, activation, expiration;
            std::vector<ExprPtr> creation, perUpdate;
            bool localPosition = false, localRotation = false, localVelocity = false;

            int shape = 0;
            ExprPtr offset[3], halfDim[3], normal[3], radius, dir[3];
            int dirMode = 0;

            ExprPtr spin, spinRate;
            ExprPtr speed[3];
            ExprPtr particleLife;
            ExprPtr particleExpire;
            ExprPtr accel[3];
            ExprPtr drag;
            ExprPtr rotAccel, rotDrag;
            ExprPtr relPos[3];
            bool parametric = false;
            bool dynamic = false;

            bool collide = false;
            ExprPtr collideRadius;
            float restitution = 0.0f, collideDrag = 0.0f;
            bool expireOnContact = false;

            ExprPtr size[2];
            std::string facing = "rotate_xyz";
            int dirFacing = 0;
            float minSpeed = 0.01f;
            float texW = 16, texH = 16;
            ExprPtr uv[2], uvSize[2];
            bool flipbook = false;
            ExprPtr baseUv[2], sizeUv[2];
            float stepUv[2] = {0, 0};
            ExprPtr fps, maxFrame;
            bool stretch = false, loop = false;

            ExprPtr color[4];
            bool hasColor = false;
            ExprPtr interpolant;
            Gradient gradient;
            bool hasGradient = false;
            bool lit = true;
            bool valid = false;
        };

        struct Emitter {
            const Def* def = nullptr;
            glm::dvec3 origin{0.0};
            double age = 0;
            double lifetime = 0;
            double spawnDebt = 0;
            bool done = false;
            bool fired = false;
            float random[4] = {0, 0, 0, 0};
            std::unordered_map<std::string, molang::Value> variables;
            int alive = 0;
        };

        struct Particle {
            const Def* def = nullptr;
            size_t emitter = 0;
            uint64_t emitterId = 0;
            glm::dvec3 pos{0.0}, vel{0.0};
            double age = 0, lifetime = 1;
            float rotation = 0, rotationRate = 0;
            float random[4] = {0, 0, 0, 0};
            glm::dvec3 origin{0.0};
        };

        struct Live {
            Emitter e;
            uint64_t id = 0;
        };

        void LoadFile(const std::string& path);
        void LoadDirectory(const std::string& dir);
        void Prepare(molang::Scope& scope, const Emitter& e, const Particle* p) const;
        void SpawnOne(Live& live);
        double Rand() const;

        mutable std::mt19937 rng{0x5eed};
        std::string key;
        std::unordered_map<std::string, Def> defs;
        std::vector<Live> emitters;
        std::vector<Particle> particles;
        uint64_t nextEmitter = 1;
    };
}
