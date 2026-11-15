#include "client/systems/renderer/gpu/PostEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

using namespace perlin::gpu;

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    float RefHalton(uint32_t index, uint32_t base) {
        float f = 1.0f, r = 0.0f;
        while (index > 0) {
            f /= static_cast<float>(base);
            r += f * static_cast<float>(index % base);
            index /= base;
        }
        return r;
    }

    PostEffects Reference(const PostEffectInputs& in) {
        PostEffects plan;
        const float scale = in.disableUpscale ? 1.0f : std::clamp(in.renderScale, 0.25f, 1.0f);
        plan.scaled = scale < 0.999f;
        plan.w = plan.scaled
                     ? std::max(32, static_cast<int>(std::lround(static_cast<float>(in.width) * scale)))
                     : in.width;
        plan.h = plan.scaled
                     ? std::max(32, static_cast<int>(std::lround(static_cast<float>(in.height) * scale)))
                     : in.height;
        plan.temporal = !in.disableUpscale && in.upscaler == 3 && in.hasTemporalProgram;
        plan.bloom = in.bloom > 0.001f && !in.disableBloom && in.hasBloomProgram;
        plan.blur = (in.motionBlur > 0.001f || in.menuBlur > 0.01f) && !in.disableBlur && in.hasPostProgram;
        plan.sceneCopy = in.packSceneCopyWanted && !in.disableSsr && !in.packRunning;
        plan.active = !in.postFailed &&
                      (plan.blur || plan.sceneCopy || plan.bloom || plan.scaled || plan.temporal) &&
                      in.hasPostProgram;
        if (in.javaActive && !in.postFailed && in.hasPostProgram)
            plan.active = true;
        if (in.javaChainReady) {
            plan.scaled = plan.temporal = plan.bloom = plan.blur = false;
            plan.sceneCopy = true;
            plan.w = in.width;
            plan.h = in.height;
        }
        return plan;
    }

    bool Same(const PostEffects& a, const PostEffects& b) {
        return a.active == b.active && a.scaled == b.scaled && a.temporal == b.temporal &&
               a.bloom == b.bloom && a.blur == b.blur && a.sceneCopy == b.sceneCopy && a.w == b.w &&
               a.h == b.h;
    }
}

int main() {
    std::mt19937 rng(31337);
    std::uniform_real_distribution<float> unit(0.0f, 1.2f);
    for (int trial = 0; trial < 50000; trial++) {
        PostEffectInputs in;
        in.width = 320 + static_cast<int>(rng() % 3000);
        in.height = 200 + static_cast<int>(rng() % 2000);
        in.renderScale = unit(rng) * 1.2f;
        in.upscaler = static_cast<int>(rng() % 5);
        in.bloom = rng() % 2 ? unit(rng) * 0.01f : 0.0f;
        in.motionBlur = rng() % 2 ? unit(rng) * 0.01f : 0.0f;
        in.menuBlur = rng() % 2 ? unit(rng) * 0.02f : 0.0f;
        in.disableUpscale = rng() % 8 == 0;
        in.disableSsr = rng() % 8 == 0;
        in.disableBloom = rng() % 8 == 0;
        in.disableBlur = rng() % 8 == 0;
        in.hasPostProgram = rng() % 4 != 0;
        in.hasTemporalProgram = rng() % 2;
        in.hasBloomProgram = rng() % 2;
        in.postFailed = rng() % 6 == 0;
        in.packSceneCopyWanted = rng() % 2;
        in.packRunning = rng() % 3 == 0;
        in.javaActive = rng() % 3 == 0;
        in.javaChainReady = rng() % 5 == 0;
        Check(Same(Reference(in), ChoosePostEffects(in)), "post effects match original");
        if (failures > 10)
            break;
    }

    for (uint32_t frame = 0; frame < 200; frame++) {
        const uint32_t index = (frame % 16) + 1;
        const float jx = RefHalton(index, 2) - 0.5f, jy = RefHalton(index, 3) - 0.5f;
        for (bool originBottom : {false, true}) {
            const float ySign = originBottom ? 1.0f : -1.0f;
            const TemporalJitter j = JitterFor(frame, 1280, 720, originBottom);
            Check(j.clip[0] == 2.0f * jx / 1280.0f && j.clip[1] == 2.0f * jy / 720.0f &&
                      j.uv[0] == jx / 1280.0f && j.uv[1] == ySign * jy / 720.0f,
                  "jitter matches original");
        }
    }
    Check(JitterFor(0, 100, 100, true).x == JitterFor(16, 100, 100, true).x,
          "jitter sequence repeats every 16 frames");

    float m[16] = {};
    for (int c = 0; c < 4; c++)
        m[c * 4 + 3] = static_cast<float>(c + 1);
    ApplyJitter(m, 0.5f, -0.25f);
    Check(m[0] == 0.5f && m[4] == 1.0f && m[1] == -0.25f && m[13] == -1.0f && m[5] == -0.5f,
          "projection jitter offsets x and y");

    if (failures == 0)
        std::printf("post_effects_selftest OK\n");
    return failures == 0 ? 0 : 1;
}
