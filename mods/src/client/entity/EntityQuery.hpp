#pragma once
#include "client/attachable/Molang.hpp"
#include "client/entity/EntityTracker.hpp"

#include <functional>
#include <string>
#include <vector>

namespace perlin::entity {
    struct QueryContext {
        const Entity* e = nullptr;
        float dt = 0, partial = 0;
        double now = 0;
        float limbSwing = 0, limbAmount = 0, groundSpeed = 0, verticalSpeed = 0, capeFlap = 0, animTime = 0;
        float bodyYaw = 0, headYaw = 0, pitch = 0, life = 0, yawSpeed = 0;
        float dx = 0, dy = 0, dz = 0;
        float standAmount = 0, sitAmount = 0, swimAmount = 0, lieAmount = 0, swellAmount = 0, hurtTime = 0;
        glm::dvec3 pos{0.0}, camera{0.0};
        float cameraYaw = 0, cameraPitch = 0;
        bool firstPerson = false;
        bool onScreen = true;
        double timeOfDay = 0.0;
        double day = 0.0;
        bool inWater = false, inRain = false;
        bool allFinished = false, anyFinished = false;
        float usedTicks = 0, maxUseTicks = 1;
        bool using_ = false, blocking = false;
        float offArmHeight = 1;
        float deathTicks = 0;
        std::function<bool(const std::string&, int, double&)> bonePivot;
    };

    bool EvalEntityQuery(const QueryContext& c, const std::string& name, const molang::Args& args,
                         molang::Value& out);
}
