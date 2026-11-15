#pragma once
#include <glm/glm.hpp>

#include <optional>

namespace perlin {
    enum class Dimension { Overworld, Nether, End };

    struct EnvironmentInput {
        double ticks = 6000.0;
        Dimension dimension = Dimension::Overworld;
        float rain = 0.0f;
        float thunder = 0.0f;
        std::optional<glm::vec3> biomeSkyColor;
        std::optional<glm::vec3> biomeFogColor;
    };

    struct Environment {
        float sunAngle = 0.0f;
        float moonAngle = 0.0f;
        float starAngle = 0.0f;
        int moonPhase = 0;
        float starBrightness = 0.0f;
        glm::vec3 skyColor{0.0f};
        glm::vec3 fogColor{0.0f};
        glm::vec4 cloudColor{1.0f};
        glm::vec4 sunriseColor{0.0f};
        float cloudHeight = 192.33f;
        glm::vec3 skyLightColor{1.0f};
        float skyLightFactor = 1.0f;
        glm::vec3 ambientColor{0.0f};
        glm::vec3 blockLightTint{1.0f};
        glm::vec3 nightVisionColor{1.0f};
        float fogStart = 0.0f;
        float fogEnd = 1024.0f;
        float skyFogEnd = 512.0f;
        float cloudFogEnd = 2048.0f;
    };

    constexpr int kTicksPerDay = 24000;

    Environment SampleEnvironment(const EnvironmentInput& input);
    float EaseSunCurve(float x);
}
