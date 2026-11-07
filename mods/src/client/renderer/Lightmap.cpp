#include "client/renderer/Lightmap.hpp"

#include <algorithm>
#include <cmath>

namespace perlin {
    namespace {
        float Brightness(float level) {
            return level / (4.0f - 3.0f * level);
        }

        glm::vec3 NotGamma(const glm::vec3& color) {
            const float peak = std::max(std::max(color.x, color.y), color.z);
            const float inverted = 1.0f - peak;
            const float scaled = 1.0f - inverted * inverted * inverted * inverted;
            return peak > 0.0f ? color * (scaled / peak) : color;
        }
    }

    void BlockLightFlicker::Tick() {
        std::uniform_real_distribution<float> next(0.0f, 1.0f);
        const float a = next(random);
        const float b = next(random);
        const float c = next(random);
        const float d = next(random);
        value += (a - b) * c * d * 0.1f;
        value *= 0.9f;
    }

    void BuildLightmap(const LightmapParams& params, uint8_t out[kLightmapBytes]) {
        for (int sky = 0; sky < kLightmapSize; sky++)
            for (int block = 0; block < kLightmapSize; block++) {
                const float blockLevel = static_cast<float>(block) / 15.0f;
                const float skyLevel = static_cast<float>(sky) / 15.0f;
                const float blockBrightness = Brightness(blockLevel) * params.blockFactor;
                const float skyBrightness = Brightness(skyLevel) * params.skyFactor;

                glm::vec3 color =
                    glm::max(params.ambientColor, params.nightVisionColor * params.nightVisionFactor);
                color += params.skyLightColor * skyBrightness;
                const float parabola = (2.0f * blockLevel - 1.0f) * (2.0f * blockLevel - 1.0f);
                color += glm::mix(params.blockLightTint, glm::vec3(1.0f), 0.9f * parabola) * blockBrightness;
                color = glm::mix(color, color * glm::vec3(0.7f, 0.6f, 0.6f), params.bossDarkening);
                color -= glm::vec3(params.darknessScale);
                color = glm::clamp(color, 0.0f, 1.0f);
                color = glm::mix(color, NotGamma(color), params.brightness);

                uint8_t* pixel = out + (static_cast<size_t>(sky) * kLightmapSize + block) * 4;
                for (int i = 0; i < 3; i++)
                    pixel[i] = static_cast<uint8_t>(std::clamp(color[i], 0.0f, 1.0f) * 255.0f + 0.5f);
                pixel[3] = 255;
            }
    }
}
