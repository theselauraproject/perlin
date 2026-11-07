#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace perlin {
    class Level;
    class BiomeTints;
    class RenderContext;

    class WeatherRenderer {
    public:
        struct In {
            RenderContext* renderer = nullptr;
            Level* level = nullptr;
            const BiomeTints* tints = nullptr;
            glm::dvec3 camera{0.0};
            float rain = 0.0f;
            float thunder = 0.0f;
            double seconds = 0.0;
            float light = 1.0f;
            bool enabled = true;
            int radius = 10;
        };
        void Draw(const In& in);
        void Reset();
        bool BlocksRain(Level& level, uint16_t id) { return Blocks(level, id); }

    private:
        struct Column {
            int top = 0;
            int8_t kind = 0;
            double stamp = -1000.0;
        };
        std::unordered_map<int64_t, Column> columns;
        uint32_t rainTexture = 0, snowTexture = 0;
        bool texturesTried = false;
        std::unordered_map<uint16_t, bool> blocking;
        bool Blocks(Level& level, uint16_t id);
        void LoadTextures(RenderContext& renderer);
    };
}
