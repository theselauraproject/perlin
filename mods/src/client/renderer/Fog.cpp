#include "client/renderer/Fog.hpp"

#include <algorithm>
#include <cmath>

namespace perlin {
    namespace {
        float ClampedLerp(float alpha, float from, float to) {
            if (alpha < 0.0f)
                return from;
            if (alpha > 1.0f)
                return to;
            return from + alpha * (to - from);
        }

        glm::vec3 BaseColor(const Environment& env, const FogInput& in) {
            glm::vec3 fog = env.fogColor;
            if (in.renderDistanceChunks >= 4) {
                const float sunX = std::sin(env.sunAngle) > 0.0f ? -1.0f : 1.0f;
                const float facing = in.forward.x * sunX;
                if (facing > 0.0f && env.sunriseColor.w > 0.0f)
                    fog = glm::mix(fog, glm::vec3(env.sunriseColor), facing * env.sunriseColor.w);
            }

            glm::vec3 sky = env.skyColor;
            if (in.rain > 0.0f)
                sky *= glm::vec3(1.0f - in.rain * 0.5f, 1.0f - in.rain * 0.5f, 1.0f - in.rain * 0.4f);
            if (in.thunder > 0.0f)
                sky *= 1.0f - in.thunder * 0.5f;
            const float skyFogEnd =
                std::min(env.skyFogEnd / 16.0f, static_cast<float>(in.renderDistanceChunks));
            const float mix = 1.0f - std::pow(ClampedLerp(skyFogEnd / 32.0f, 0.25f, 1.0f), 0.25f);
            return glm::mix(fog, sky, mix);
        }
    }

    FogState ComputeFog(const Environment& env, const FogInput& in) {
        FogState fog;
        glm::vec3 color = in.inLava       ? glm::vec3(0.6f, 0.09803922f, 0.0f)
                          : in.underwater ? in.waterFogColor
                                          : BaseColor(env, in);

        const float darkness = std::clamp(
            (in.voidDarknessOnsetRange + in.minY - in.cameraY) / in.voidDarknessOnsetRange, 0.0f, 1.0f);
        if (darkness > 0.0f) {
            const float brightness = (1.0f - darkness) * (1.0f - darkness);
            color *= brightness;
        }
        if (in.underwater && color.x != 0.0f && color.y != 0.0f && color.z != 0.0f) {
            const float scale = 1.0f / std::max(color.x, std::max(color.y, color.z));
            color = glm::mix(color, color * scale, in.waterVision);
        }
        fog.color = glm::vec4(color, 1.0f);

        const float renderDistance = static_cast<float>(in.renderDistanceChunks * 16);
        fog.environmentalStart = env.fogStart + -160.0f * in.rainFogMultiplier;
        const float minRainFogEnd = std::min(96.0f, env.fogEnd);
        fog.environmentalEnd = std::max(minRainFogEnd, env.fogEnd + -256.0f * in.rainFogMultiplier);
        // open-sky dimensions: the haze moves out with the render distance so far terrain is not hidden by fog that
        // was tuned for the default range (dense dimensions such as the nether keep their own limit)
        if (env.fogEnd >= 512.0f && renderDistance > env.fogEnd) {
            const float scale = renderDistance / env.fogEnd;
            fog.environmentalStart *= scale;
            fog.environmentalEnd *= scale;
        }
        fog.skyEnd = std::min(renderDistance, env.skyFogEnd);
        fog.cloudEnd = std::min(static_cast<float>(in.cloudRangeChunks * 16), env.cloudFogEnd);

        if (in.underwater) {
            fog.environmentalStart = -8.0f;
            fog.environmentalEnd = 96.0f * in.waterFogEndScale * std::max(0.25f, in.waterVision);
            fog.skyEnd = fog.environmentalEnd;
            fog.cloudEnd = fog.environmentalEnd;
        }

        if (in.inLava) {
            fog.environmentalStart = 0.25f;
            fog.environmentalEnd = 1.0f;
            fog.skyEnd = 1.0f;
            fog.cloudEnd = 1.0f;
        }

        const float span = std::clamp(renderDistance / 10.0f, 4.0f, 256.0f);
        fog.renderDistanceStart = renderDistance - span;
        fog.renderDistanceEnd = renderDistance;
        return fog;
    }
}
