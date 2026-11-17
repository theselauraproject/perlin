#include <perlin/perlin.hpp>

#include <cmath>

using namespace perlin;

namespace {
    constexpr float kDuration = 1.0f;
    float elapsed = 0.0f;
    bool rolling = false;

    float ease(float p) {
        return p * p * (3.0f - 2.0f * p);
    }
}

struct BarrelRoll : perlin::Plugin {
    void onEnable(perlin::Context& ctx) override {
        ctx.bind<"roll">("Barrel roll", Key::R, [] {
            auto p = game::player();
            if (p && p->gliding && !rolling) {
                rolling = true;
                elapsed = 0.0f;
            }
        });

        ctx.on_frame([](const abi::EventFrame& f) {
            if (!rolling)
                return;
            elapsed += f.dt;
            const float p = elapsed / kDuration;
            if (p >= 1.0f) {
                rolling = false;
                return;
            }
            const float angle = 360.0f * ease(p);
            const float kick = 10.0f * std::sin(p * 3.14159265f);
            (void)game::camera({.roll = angle, .fovAdd = kick, .ticks = 2});
            (void)game::model_pose({.yaw = -angle, .ticks = 2});
        });
    }
};
PERLIN_REGISTER(BarrelRoll)
