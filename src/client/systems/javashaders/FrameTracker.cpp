#include "client/systems/javashaders/FrameTracker.hpp"

#include "client/systems/javashaders/Biomes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

namespace perlin::jshader {
    namespace {
        constexpr double kPi = 3.14159265358979323846;
        constexpr int kFogLinear = 0x2601;
        constexpr int kFogExp2 = 0x0801;

        glm::mat4 ToMat(const float* m) {
            glm::mat4 out;
            std::memcpy(&out[0][0], m, sizeof(float) * 16);
            return out;
        }

        void FromMat(const glm::mat4& m, float* out) {
            std::memcpy(out, &m[0][0], sizeof(float) * 16);
        }

        void ToGlDepth(const float* in, float* out) {
            std::memcpy(out, in, sizeof(float) * 16);
            for (int c = 0; c < 4; c++)
                out[c * 4 + 2] = 2.0f * in[c * 4 + 2] - in[c * 4 + 3];
        }

        glm::dvec3 CameraOf(const FrameParams& params) {
            return {params.cameraPosition[0], params.cameraPosition[1], params.cameraPosition[2]};
        }

        float RainFactor(const FrameParams& params) {
            return std::clamp(1.0f - params.sky.rainBrightness, 0.0f, 1.0f);
        }

        float WaterVision(float ticks) {
            const float early = std::clamp(ticks / 100.0f, 0.0f, 1.0f);
            const float late = ticks < 100.0f ? 0.0f : std::clamp((ticks - 100.0f) / 500.0f, 0.0f, 1.0f);
            return ticks >= 600.0f ? 1.0f : early * 0.6f + late * 0.39999998f;
        }

        void Approach(float& value, float target, float halflife, float dt, bool first) {
            if (first || halflife <= 0.0f) {
                value = target;
                return;
            }
            value += (target - value) * (1.0f - std::exp(-dt * 0.69314718f / halflife));
        }
    }

    void FrameTracker::Update(const FrameParams& params, const PipelineConfig& config, const Scene& scene,
                              FrameState& s) {
        s = FrameState();
        TrackCamera(params, s);
        TrackSky(params, config, scene, s);
        TrackFog(params, scene, s);
        s.rainStrength = RainFactor(params);
        s.thunderStrength = std::clamp(params.thunder, 0.0f, 1.0f);
        if (params.worldTick >= 0) {
            s.worldTime = params.worldTick % 24000;
            s.worldDay = std::max(0, params.worldDay);
        }
        TrackSmoothing(params, config, scene, s);
        TrackBiome(params, s);
        TrackView(params, scene, s);
    }

    void FrameTracker::TrackCamera(const FrameParams& params, FrameState& s) {
        const glm::dmat4 relative =
            glm::dmat4(ToMat(params.view)) * glm::translate(glm::dmat4(1.0), glm::dvec3(CameraOf(params)));
        FromMat(glm::mat4(relative), s.modelView);
        ToGlDepth(params.projection, s.projection);
        if (havePrev) {
            std::memcpy(s.prevModelView, prevModelView, sizeof(prevModelView));
            std::memcpy(s.prevProjection, prevProjection, sizeof(prevProjection));
            std::memcpy(s.previousCameraPosition, prevCamera, sizeof(prevCamera));
        } else {
            std::memcpy(s.prevModelView, s.modelView, sizeof(s.modelView));
            std::memcpy(s.prevProjection, s.projection, sizeof(s.projection));
            for (int i = 0; i < 3; i++)
                s.previousCameraPosition[i] = params.cameraPosition[i];
        }
        std::memcpy(prevModelView, s.modelView, sizeof(prevModelView));
        std::memcpy(prevProjection, s.projection, sizeof(prevProjection));
        for (int i = 0; i < 3; i++) {
            s.cameraPosition[i] = params.cameraPosition[i];
            prevCamera[i] = params.cameraPosition[i];
        }
        havePrev = true;
    }

    void FrameTracker::TrackSky(const FrameParams& params, const PipelineConfig& config, const Scene& scene,
                                FrameState& s) {
        const double angle =
            scene.nether ? kPi : (scene.end ? 0.0 : static_cast<double>(params.sky.sunAngle));
        double fraction = angle / (2.0 * kPi) + 0.25;
        fraction -= std::floor(fraction);
        s.sunAngle = static_cast<float>(fraction);
        s.shadowAngle = s.sunAngle <= 0.5f ? s.sunAngle : s.sunAngle - 0.5f;
        s.worldTime = static_cast<int>(fraction * 24000.0) % 24000;
        s.worldDay = 0;
        s.moonPhase = static_cast<int>(std::floor(params.sky.moonPhase)) & 7;

        const float tilt = glm::radians(config.sunPathRotation);
        const float sinA = std::sin(static_cast<float>(angle)), cosA = std::cos(static_cast<float>(angle));
        const glm::vec3 sunDir(-sinA, cosA * std::cos(tilt), -cosA * std::sin(tilt));
        glm::mat4 rotation = ToMat(params.view);
        rotation[3] = glm::vec4(0, 0, 0, 1);
        const glm::mat3 rot3(rotation);
        const glm::vec3 sunView = rot3 * sunDir * 100.0f;
        const glm::vec3 moonView = -sunView;
        const glm::vec3 upView = rot3 * glm::vec3(0, 1, 0) * 100.0f;
        const bool day = s.sunAngle < 0.5f;
        for (int i = 0; i < 3; i++) {
            s.sunPosition[i] = sunView[i];
            s.moonPosition[i] = moonView[i];
            s.upPosition[i] = upView[i];
            s.shadowLightPosition[i] = day ? sunView[i] : moonView[i];
            s.skyColor[i] = params.sky.skyColor[i];
            s.fogColor[i] = params.fogColor[i];
        }
        shadowLight = day ? sunDir : -sunDir;
    }

    void FrameTracker::TrackFog(const FrameParams& params, const Scene& scene, FrameState& s) {
        const float dt = std::clamp(params.timeSeconds - fogClock, 0.0f, 0.25f);
        fogClock = params.timeSeconds;
        waterVisionTicks = params.cameraMedium == 1 ? std::min(600.0f, waterVisionTicks + dt * 20.0f) : 0.0f;
        const float vision = WaterVision(waterVisionTicks);
        const float rain = RainFactor(params);
        if (params.cameraMedium == 1) {
            s.fogStart = -8.0f;
            s.fogEnd = 96.0f * std::max(0.25f, vision);
            s.fogDensity = 0.05f - vision * vision * 0.03f;
            s.fogMode = kFogExp2;
        } else if (params.cameraMedium == 2) {
            s.fogStart = 0.25f;
            s.fogEnd = 1.0f;
            s.fogDensity = 0.0f;
            s.fogMode = kFogLinear;
        } else if (scene.nether) {
            const float distance = static_cast<float>(std::clamp(params.renderDistance, 2, 64) * 16);
            s.fogStart = distance * 0.05f;
            s.fogEnd = std::min(distance, 192.0f) * 0.5f;
            s.fogDensity = 0.0f;
            s.fogMode = kFogLinear;
        } else {
            s.fogStart = -160.0f * rain;
            s.fogEnd = std::max(96.0f, 1024.0f - 256.0f * rain);
            s.fogDensity = 0.0f;
            s.fogMode = kFogLinear;
        }
        s.fogShape = 1;
    }

    void FrameTracker::TrackSmoothing(const FrameParams& params, const PipelineConfig& config,
                                      const Scene& scene, FrameState& s) {
        const float dt = std::clamp(params.timeSeconds - smoothAt, 0.0f, 1.0f);
        smoothAt = params.timeSeconds;
        const bool first = !smoothStarted;
        smoothStarted = true;
        const float wetnessHalflife =
            (s.rainStrength > wetnessSmooth ? config.wetnessHalflife : config.drynessHalflife) * 0.1f;
        Approach(wetnessSmooth, s.rainStrength, wetnessHalflife, dt, first);
        s.wetness = wetnessSmooth;
        s.eyeBrightness[0] = std::clamp(params.eyeBlockLight, 0, 15) * 16;
        s.eyeBrightness[1] = scene.noSkyLight ? 0 : std::clamp(params.eyeSkyLight, 0, 15) * 16;
        for (int i = 0; i < 2; i++) {
            Approach(eyeSmooth[i], static_cast<float>(s.eyeBrightness[i]),
                     config.eyeBrightnessHalflife * 0.1f, dt, first);
            s.eyeBrightnessSmooth[i] = static_cast<int>(std::lround(eyeSmooth[i]));
        }
    }

    void FrameTracker::TrackBiome(const FrameParams& params, FrameState& s) {
        const int id = JavaBiomeId(params.biome);
        const auto& biome = JavaBiomes()[static_cast<size_t>(id)];
        s.biome = id;
        s.biomeCategory = biome.category;
        s.biomePrecipitation = BiomePrecipitationAt(id, params.cameraPosition[1]);
        s.biomeTemperature = biome.temperature;
        s.biomeRainfall = biome.downfall;
        s.inSwamp = biome.category == 14;
    }

    void FrameTracker::TrackView(const FrameParams& params, const Scene& scene, FrameState& s) {
        s.frameCounter = static_cast<int>(params.frameIndex);
        s.frameTimeCounter = params.timeSeconds;
        s.frameTime = std::clamp(params.timeSeconds - lastTime, 0.0f, 0.25f);
        lastTime = params.timeSeconds;
        const float c = s.projection[10], d = s.projection[14];
        if (std::fabs(c - 1.0f) > 1e-6f && std::fabs(c + 1.0f) > 1e-6f)
            s.nearPlane = d / (c - 1.0f);
        s.farPlane = static_cast<float>(std::clamp(params.renderDistance, 2, 64) * 16);
        s.isEyeInWater = params.cameraMedium == 1 ? 1 : (params.cameraMedium == 2 ? 2 : 0);
        s.eyeAltitude = params.cameraPosition[1];
        s.heldBlockLightValue = static_cast<int>(params.heldLight);
        s.atlasSize[0] = scene.atlasWidth;
        s.atlasSize[1] = scene.atlasHeight;
        s.cloudTime = static_cast<float>(params.cloud.gameTicks);
        s.screenBrightness = 0.5f;
        s.dimensionNether = scene.nether;
        s.dimensionEnd = scene.end;
        s.heavyFog = scene.nether ? 1.0f : 0.0f;
        s.cloudHeight = params.cloud.height;
        s.viewWidth = scene.viewWidth;
        s.viewHeight = scene.viewHeight;
        s.dimensionId = scene.nether ? -1 : (scene.end ? 1 : 0);
    }

    FrameTracker::ShadowFrame FrameTracker::UpdateShadows(const FrameParams& params,
                                                          const PipelineConfig& config, const FrameState& s,
                                                          const ShadowInputs& in) {
        ShadowFrame out;
        const glm::dvec3 camera = CameraOf(params);
        const float skyAngle = s.shadowAngle < 0.25f ? s.shadowAngle + 0.75f : s.shadowAngle - 0.25f;
        glm::mat4 rotation(1.0f);
        rotation = glm::rotate(rotation, glm::radians(90.0f), glm::vec3(1, 0, 0));
        rotation = glm::rotate(rotation, glm::radians(skyAngle * -360.0f), glm::vec3(0, 0, 1));
        rotation = glm::rotate(rotation, glm::radians(config.sunPathRotation), glm::vec3(1, 0, 0));
        glm::vec3 offset(0.0f);
        const float interval = config.shadowIntervalSize;
        if (std::fabs(interval) > 0.0f) {
            const float half = interval / 2.0f;
            offset = glm::vec3(std::fmod(static_cast<float>(camera.x), interval) - half,
                               std::fmod(static_cast<float>(camera.y), interval) - half,
                               std::fmod(static_cast<float>(camera.z), interval) - half);
        }
        const glm::mat4 modelView = glm::translate(rotation, offset);
        FromMat(modelView, out.view);
        const float extent = std::max(1.0f, config.shadowDistance);
        FromMat(
            glm::orthoRH_NO(-extent, extent, -extent, extent, config.shadowNearPlane, config.shadowFarPlane),
            out.projection);
        FromMat(modelView * glm::translate(glm::mat4(1.0f), -glm::vec3(camera)), out.drawView);

        if (config.shadowEnabled && shadowKept && in.depthReady) {
            const double moved = glm::length(camera - keptCamera);
            const float turned = std::fabs(s.shadowAngle - keptAngle);
            if (in.interval > 1)
                out.reuse = in.frameStamp % in.interval != 0 && moved < 4.0 && turned < 0.01f;
            if (!out.reuse && in.cacheUsable)
                out.partial = moved < 1.5 && turned < 0.0002f && in.cacheAge < 300 && in.cacheMatchesMeshes;
        }
        if (out.reuse || out.partial) {
            FromMat(glm::translate(keptRotation, keptOffset + glm::vec3(camera - keptCamera)), out.view);
            std::memcpy(out.drawView, keptDrawView, sizeof(out.drawView));
            return out;
        }
        keptRotation = rotation;
        keptOffset = offset;
        keptCamera = camera;
        keptAngle = s.shadowAngle;
        std::memcpy(keptDrawView, out.drawView, sizeof(keptDrawView));
        shadowKept = config.shadowEnabled;
        return out;
    }

}
