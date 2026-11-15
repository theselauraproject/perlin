#pragma once
#include "client/systems/javashaders/PipelineConfig.hpp"
#include "client/systems/javashaders/UniformTable.hpp"
#include "client/systems/gfx/RenderContext.hpp"

#include <cstdint>
#include <glm/glm.hpp>

namespace perlin::jshader {

    class FrameTracker {
    public:
        struct Scene {
            bool nether = false;
            bool end = false;
            bool noSkyLight = false;
            int atlasWidth = 0;
            int atlasHeight = 0;
            int viewWidth = 0;
            int viewHeight = 0;
        };

        struct ShadowInputs {
            bool depthReady = false;
            int interval = 1;
            int frameStamp = 0;
            bool cacheUsable = false;
            int cacheAge = 0;
            bool cacheMatchesMeshes = false;
        };

        struct ShadowFrame {
            float view[16] = {};
            float projection[16] = {};
            float drawView[16] = {};
            bool reuse = false;
            bool partial = false;
        };

        void Update(const FrameParams& params, const PipelineConfig& config, const Scene& scene,
                    FrameState& state);
        ShadowFrame UpdateShadows(const FrameParams& params, const PipelineConfig& config,
                                  const FrameState& state, const ShadowInputs& inputs);
        void ForgetShadow() { shadowKept = false; }
        const glm::vec3& ShadowLightWorld() const { return shadowLight; }

    private:
        void TrackCamera(const FrameParams& params, FrameState& state);
        void TrackSky(const FrameParams& params, const PipelineConfig& config, const Scene& scene,
                      FrameState& state);
        void TrackFog(const FrameParams& params, const Scene& scene, FrameState& state);
        void TrackSmoothing(const FrameParams& params, const PipelineConfig& config, const Scene& scene,
                            FrameState& state);
        static void TrackBiome(const FrameParams& params, FrameState& state);
        void TrackView(const FrameParams& params, const Scene& scene, FrameState& state);

        float prevModelView[16] = {};
        float prevProjection[16] = {};
        double prevCamera[3] = {0, 0, 0};
        bool havePrev = false;
        float lastTime = 0.0f;
        float fogClock = 0.0f;
        float waterVisionTicks = 0.0f;
        float smoothAt = 0.0f;
        float wetnessSmooth = 0.0f;
        float eyeSmooth[2] = {240.0f, 240.0f};
        bool smoothStarted = false;
        glm::vec3 shadowLight{0.0f};
        bool shadowKept = false;
        glm::mat4 keptRotation{1.0f};
        glm::vec3 keptOffset{0.0f};
        glm::dvec3 keptCamera{0.0};
        float keptAngle = 0.0f;
        float keptDrawView[16] = {};
    };

}
