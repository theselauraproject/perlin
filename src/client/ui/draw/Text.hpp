#pragma once
#include "client/ui/draw/Color.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace perlin::ui::draw {
    struct Face {
        bool ten = false;
        bool shadow = false;
        bool outline = false;
        bool bold = false;
        bool italic = false;
        float size = 8;
    };

    struct FontMetrics {
        float ascent = 7;
        float descent = 2;
        float line = 9;
    };

    class TextSink {
    public:
        virtual ~TextSink() = default;
        virtual int Device() const = 0;
        virtual uint32_t Texture(const std::string& key) = 0;
        virtual void Quad(uint32_t texture, float x, float y, float w, float h, float u0, float v0, float u1,
                          float v1, uint32_t color) = 0;
        virtual void Skewed(uint32_t texture, const float xy[8], const float uv[8], uint32_t color) = 0;
    };

    Face FaceFromFamily(const std::string& family, float size, bool bold, bool italic);
    FontMetrics MetricsFor(const Face& face);
    int StringWidth(const Face& face, const std::string& text);
    float EmitText(const Face& face, const std::string& text, float x, float baseline, Color color,
                   TextSink& sink);
    uint32_t FontStamp();
}
