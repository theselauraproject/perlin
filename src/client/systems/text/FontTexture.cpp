#include "client/systems/text/FontTexture.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/text/McFont.hpp"

#include <algorithm>
#include <vector>

namespace perlin::ui {
    namespace {
        void CopyRect(const McFont& font, int x, int y, int w, int h, bool premultiply, std::vector<uint8_t>& out) {
            const std::vector<uint8_t>& atlas = font.Atlas();
            const size_t stride = static_cast<size_t>(font.AtlasWidth()) * 4;
            out.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            for (int row = 0; row < h; row++) {
                const uint8_t* src = atlas.data() + static_cast<size_t>(y + row) * stride + static_cast<size_t>(x) * 4;
                uint8_t* dst = out.data() + static_cast<size_t>(row) * static_cast<size_t>(w) * 4;
                if (!premultiply) {
                    std::copy_n(src, static_cast<size_t>(w) * 4, dst);
                    continue;
                }
                for (int i = 0; i < w * 4; i += 4) {
                    const unsigned a = src[i + 3];
                    dst[i] = static_cast<uint8_t>((src[i] * a + 127) / 255);
                    dst[i + 1] = static_cast<uint8_t>((src[i + 1] * a + 127) / 255);
                    dst[i + 2] = static_cast<uint8_t>((src[i + 2] * a + 127) / 255);
                    dst[i + 3] = static_cast<uint8_t>(a);
                }
            }
        }
    }

    void SyncFontTexture(RenderContext& renderer, const McFont& font, FontTexture& texture, bool premultiply) {
        static thread_local std::vector<uint8_t> scratch;
        if (texture.handle && texture.version == font.Version() && texture.revision == font.Revision())
            return;
        if (texture.handle && texture.version == font.Version()) {
            int x = 0, y = 0, w = 0, h = 0;
            if (font.DirtySince(texture.revision, x, y, w, h)) {
                bool ok = w == 0 || h == 0;
                if (!ok) {
                    CopyRect(font, x, y, w, h, premultiply, scratch);
                    ok = renderer.UiUpdateTexture(texture.handle, x, y, w, h, scratch.data(), w * 4);
                }
                if (ok) {
                    texture.revision = font.Revision();
                    return;
                }
            }
        }
        CopyRect(font, 0, 0, font.AtlasWidth(), font.AtlasHeight(), premultiply, scratch);
        if (texture.handle && texture.version == font.Version() &&
            renderer.UiUpdateTexture(texture.handle, 0, 0, font.AtlasWidth(), font.AtlasHeight(), scratch.data(),
                                     font.AtlasWidth() * 4)) {
            texture.revision = font.Revision();
            return;
        }
        if (texture.handle)
            renderer.UiDestroyTexture(texture.handle);
        texture.handle = renderer.UiCreateTexture(font.AtlasWidth(), font.AtlasHeight(), scratch.data(), false);
        texture.version = font.Version();
        texture.revision = font.Revision();
    }
}
