#pragma once
#include "client/renderer/Environment.hpp"

namespace perlin {
    struct FogInput {
        int renderDistanceChunks = 12;
        int cloudRangeChunks = 128;
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        float cameraY = 64.0f;
        float minY = -64.0f;
        float voidDarknessOnsetRange = 32.0f;
        float rain = 0.0f;
        float thunder = 0.0f;
        float rainFogMultiplier = 0.0f;
        bool underwater = false;
        bool inLava = false;
        float waterVision = 0.0f;
        glm::vec3 waterFogColor{0.0196f, 0.0196f, 0.2f};
        float waterFogEndScale = 1.0f;
    };

    struct FogState {
        glm::vec4 color{0.0f};
        float environmentalStart = 0.0f;
        float environmentalEnd = 0.0f;
        float renderDistanceStart = 0.0f;
        float renderDistanceEnd = 0.0f;
        float skyEnd = 0.0f;
        float cloudEnd = 0.0f;
    };

    FogState ComputeFog(const Environment& env, const FogInput& input);
}
