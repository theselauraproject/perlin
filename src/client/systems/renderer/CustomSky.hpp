#pragma once
#include "client/systems/resources/TextureLoader.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin::sky {
    enum class Blend : uint8_t { Alpha, Add, Subtract, Multiply, Dodge, Burn, Screen, Overlay, Replace };

    struct Layer {
        int texture = 0;
        bool cubemap = false;
        bool timed = false;
        int startFadeIn = 0, endFadeIn = 0, startFadeOut = 24000, endFadeOut = 24000;
        Blend blend = Blend::Add;
        bool rotate = true;
        float speed = 1.0f;
        glm::vec3 axis{1.0f, 0.0f, 0.0f};
        std::vector<std::pair<int, int>> days;
        int daysLoop = 8;
        bool clear = true, rain = false, thunder = false;
        std::vector<std::string> biomes;
        std::vector<std::pair<int, int>> heights;
        float transition = 1.0f;
        float positionBrightness = -1.0f;
        std::string source;
    };

    struct Celestial {
        int texture = -1;
        Blend blend = Blend::Add;
    };

    struct Set {
        std::vector<Layer> layers;
        std::vector<Layer> endLayers;
        Celestial sun, moon;
        std::vector<Texture> textures;
        std::string origin;
        bool Empty() const {
            return layers.empty() && endLayers.empty() && sun.texture < 0 && moon.texture < 0;
        }
    };

    Set Load(const std::vector<std::string>& serverPackDirs);

    struct Inputs {
        int64_t worldTime = 0;
        float celestialAngle = 0.0f;
        float rain = 0.0f, thunder = 0.0f;
        std::string biome;
        double eyeY = 64.0;
        float dt = 0.0f;
    };

    struct Draw {
        int texture = 0;
        bool cubemap = false;
        Blend blend = Blend::Add;
        float brightness = 1.0f;
        glm::mat4 rotation{1.0f};
    };

    std::vector<Draw> Evaluate(std::vector<Layer>& layers, const Inputs& in);

    bool ParseLayer(const std::string& properties, const std::string& directory, int index, Layer& out,
                    std::string& error);
    float FadeBrightness(const Layer& layer, int timeOfDay);
}
