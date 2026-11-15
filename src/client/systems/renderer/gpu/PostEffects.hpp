#pragma once
#include <cstdint>

namespace perlin::gpu {

    struct PostEffectInputs {
        int width = 0;
        int height = 0;
        float renderScale = 1.0f;
        int upscaler = 0;
        float bloom = 0.0f;
        float motionBlur = 0.0f;
        float menuBlur = 0.0f;
        bool disableUpscale = false;
        bool disableSsr = false;
        bool disableBloom = false;
        bool disableBlur = false;
        bool hasPostProgram = false;
        bool hasTemporalProgram = false;
        bool hasBloomProgram = false;
        bool postFailed = false;
        bool packSceneCopyWanted = false;
        bool packRunning = false;
        bool javaActive = false;
        bool javaChainReady = false;
    };

    struct PostEffects {
        bool active = false;
        bool scaled = false;
        bool temporal = false;
        bool bloom = false;
        bool blur = false;
        bool sceneCopy = false;
        int w = 0;
        int h = 0;
    };

    struct TemporalJitter {
        float clip[2] = {0, 0};
        float uv[2] = {0, 0};
        float x = 0;
        float y = 0;
    };

    PostEffects ChoosePostEffects(const PostEffectInputs& in);
    float Halton(uint32_t index, uint32_t base);
    TemporalJitter JitterFor(uint32_t frameIndex, int w, int h, bool originBottom);
    void ApplyJitter(float matrix[16], float ox, float oy);

}
