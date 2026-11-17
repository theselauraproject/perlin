#include "client/systems/renderer/gpu/PostEffects.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::gpu {

    PostEffects ChoosePostEffects(const PostEffectInputs& in) {
        PostEffects fx;
        const float scale = in.disableUpscale ? 1.0f : std::clamp(in.renderScale, 0.25f, 1.0f);
        fx.scaled = scale < 0.999f;
        fx.w = fx.scaled ? std::max(32, static_cast<int>(std::lround(static_cast<float>(in.width) * scale)))
                         : in.width;
        fx.h = fx.scaled ? std::max(32, static_cast<int>(std::lround(static_cast<float>(in.height) * scale)))
                         : in.height;
        fx.temporal = !in.disableUpscale && in.upscaler == 3 && in.hasTemporalProgram;
        fx.bloom = in.bloom > 0.001f && !in.disableBloom && in.hasBloomProgram;
        fx.blur = (in.motionBlur > 0.001f || in.menuBlur > 0.01f) && !in.disableBlur && in.hasPostProgram;
        fx.sceneCopy = in.packSceneCopyWanted && !in.disableSsr && !in.packRunning;
        fx.active = !in.postFailed && (fx.blur || fx.sceneCopy || fx.bloom || fx.scaled || fx.temporal) &&
                    in.hasPostProgram;
        if (in.javaActive && !in.postFailed && in.hasPostProgram)
            fx.active = true;
        if (in.javaChainReady) {
            fx.scaled = fx.temporal = fx.bloom = fx.blur = false;
            fx.sceneCopy = true;
            fx.w = in.width;
            fx.h = in.height;
        }
        return fx;
    }

    float Halton(uint32_t index, uint32_t base) {
        float f = 1.0f, r = 0.0f;
        while (index > 0) {
            f /= static_cast<float>(base);
            r += f * static_cast<float>(index % base);
            index /= base;
        }
        return r;
    }

    TemporalJitter JitterFor(uint32_t frameIndex, int w, int h, bool originBottom) {
        const uint32_t index = (frameIndex % 16) + 1;
        TemporalJitter j;
        j.x = Halton(index, 2) - 0.5f;
        j.y = Halton(index, 3) - 0.5f;
        const float ySign = originBottom ? 1.0f : -1.0f;
        j.clip[0] = 2.0f * j.x / static_cast<float>(w);
        j.clip[1] = 2.0f * j.y / static_cast<float>(h);
        j.uv[0] = j.x / static_cast<float>(w);
        j.uv[1] = ySign * j.y / static_cast<float>(h);
        return j;
    }

    void ApplyJitter(float m[16], float ox, float oy) {
        for (int c = 0; c < 4; c++) {
            m[c * 4 + 0] += ox * m[c * 4 + 3];
            m[c * 4 + 1] += oy * m[c * 4 + 3];
        }
    }

}
