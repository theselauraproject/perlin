#include "client/ui/Glint.hpp"
#include "util/Env.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/ui/UiAssets.hpp"

#include <cstdio>
#include <cstdlib>

namespace perlin::ui {
    uint32_t CreateGlintTexture(RenderContext& renderer, int* width) {
        Texture t;
        if (!UiAssets::Instance().Image("textures/misc/enchanted_item_glint", t) || t.pixels.empty())
            return 0;
        if (PERLIN_ENV("PERLIN_GLINT_DEBUG"))
            std::fprintf(stderr, "[glint] texture %dx%d\n", t.width, t.height);
        if (width)
            *width = t.width;
        return renderer.UiCreateTexture(t.width, t.height, t.pixels.data(), true, true);
    }
}
