#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <random>

namespace perlin {
    constexpr int kLightmapSize = 16;
    constexpr int kLightmapBytes = kLightmapSize * kLightmapSize * 4;

    struct LightmapParams {
        float blockFactor = 1.4f;
        float skyFactor = 1.0f;
        float nightVisionFactor = 0.0f;
        float darknessScale = 0.0f;
        float bossDarkening = 0.0f;
        float brightness = 0.5f;
        glm::vec3 blockLightTint{1.0f};
        glm::vec3 skyLightColor{1.0f};
        glm::vec3 ambientColor{0.0f};
        glm::vec3 nightVisionColor{1.0f};
    };

    class BlockLightFlicker {
    public:
        void Tick();
        float Value() const { return value; }

    private:
        float value = 0.0f;
        std::mt19937 random{std::random_device{}()};
    };

    void BuildLightmap(const LightmapParams& params, uint8_t out[kLightmapBytes]);
}
