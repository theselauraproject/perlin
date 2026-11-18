#pragma once
#include <cstdint>

namespace perlin {
    class RenderContext;
}

namespace perlin::ui {
    class McFont;
    struct FontTexture {
        uint32_t handle = 0;
        uint32_t version = 0;
        uint32_t revision = 0;
    };

    void SyncFontTexture(RenderContext& renderer, const McFont& font, FontTexture& texture, bool premultiply);
}
