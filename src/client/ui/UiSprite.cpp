#include "client/ui/UiSprite.hpp"

#include "client/resources/TextureLoader.hpp"
#include "client/resources/TexturePack.hpp"
#include "client/ui/UiAssets.hpp"
#include "util/Resources.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <vector>

namespace perlin::ui {
    bool PackSpriteNative(const std::string& resource, Texture& out) {
        if (!TexturePack::Instance().Active() || !resource.starts_with("ui_") || resource.size() < 8)
            return false;
        std::string stem = resource.substr(3);
        if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".png") == 0)
            stem.resize(stem.size() - 4);
        return TexturePack::Instance().Load("textures/ui/screen_" + stem + ".png", out) && out.width > 0 &&
               out.height > 0;
    }

    bool PackSpriteFor(const std::string& resource, int baseWidth, int baseHeight, Texture& out) {
        Texture pack;
        if (baseWidth <= 0 || baseHeight <= 0 || !PackSpriteNative(resource, pack))
            return false;
        std::vector<uint8_t> scaled(static_cast<size_t>(baseWidth) * static_cast<size_t>(baseHeight) * 4, 0);
        for (int y = 0; y < baseHeight; y++)
            for (int x = 0; x < baseWidth; x++) {
                const int x0 = x * pack.width / baseWidth, x1 = std::max(x0 + 1, (x + 1) * pack.width / baseWidth);
                const int y0 = y * pack.height / baseHeight,
                          y1 = std::max(y0 + 1, (y + 1) * pack.height / baseHeight);
                double r = 0, g = 0, b = 0, a = 0;
                int n = 0;
                for (int yy = y0; yy < y1 && yy < pack.height; yy++)
                    for (int xx = x0; xx < x1 && xx < pack.width; xx++) {
                        const uint8_t* p = &pack.pixels[(static_cast<size_t>(yy) * static_cast<size_t>(pack.width) +
                                                         static_cast<size_t>(xx)) *
                                                        4];
                        r += p[0] * p[3];
                        g += p[1] * p[3];
                        b += p[2] * p[3];
                        a += p[3];
                        n++;
                    }
                uint8_t* o = &scaled[(static_cast<size_t>(y) * static_cast<size_t>(baseWidth) +
                                      static_cast<size_t>(x)) *
                                     4];
                if (a > 0 && n > 0) {
                    o[0] = static_cast<uint8_t>(r / a + 0.5);
                    o[1] = static_cast<uint8_t>(g / a + 0.5);
                    o[2] = static_cast<uint8_t>(b / a + 0.5);
                    o[3] = static_cast<uint8_t>(a / n + 0.5);
                }
            }
        out.width = baseWidth;
        out.height = baseHeight;
        out.pixels = std::move(scaled);
        return true;
    }

    namespace {
        struct Decoded {
            bool ok = false;
            int width = 0, height = 0;
            std::vector<uint8_t> rgba;
        };

        const Decoded& Load(const std::string& resource) {
            static std::map<std::string, Decoded> cache;
            static std::mutex mutex;
            static uint64_t generation = ~0ull;
            std::lock_guard lock(mutex);
            if (generation != TexturePack::Instance().Generation()) {
                generation = TexturePack::Instance().Generation();
                cache.clear();
            }
            auto it = cache.find(resource);
            if (it != cache.end())
                return it->second;
            Decoded d;
            std::string bytes;
            Texture tex;
            if (resources::Read(resource, bytes) &&
                LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), tex)) {
                d.ok = true;
                d.width = tex.width;
                d.height = tex.height;
                d.rgba.assign(tex.pixels.begin(), tex.pixels.end());
                Texture replaced;
                if (PackSpriteFor(resource, d.width, d.height, replaced))
                    d.rgba.assign(replaced.pixels.begin(), replaced.pixels.end());
            }
            return cache.emplace(resource, std::move(d)).first->second;
        }
    }

    std::string SpriteRml(const std::string& resource, int srcX, int srcY, int width, int height, int dstX,
                          int dstY) {
        static std::map<std::string, std::string> cache;
        static std::mutex mutex;
        const std::string key = std::to_string(TexturePack::Instance().Generation()) + "/" + resource + ":" +
                                std::to_string(srcX) + "," + std::to_string(srcY) + "," +
                                std::to_string(width) + "," + std::to_string(height) + "@" +
                                std::to_string(dstX) + "," + std::to_string(dstY);
        {
            std::lock_guard lock(mutex);
            const auto it = cache.find(key);
            if (it != cache.end())
                return it->second;
        }
        const Decoded& img = Load(resource);
        std::string out;
        if (img.ok) {
            const auto pixel = [&](int x, int y) -> uint32_t {
                const int sx = srcX + x, sy = srcY + y;
                if (sx < 0 || sy < 0 || sx >= img.width || sy >= img.height)
                    return 0;
                const uint8_t* p = &img.rgba[(static_cast<size_t>(sy) * static_cast<size_t>(img.width) +
                                              static_cast<size_t>(sx)) *
                                             4];
                if (p[3] == 0)
                    return 0;
                return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) | p[3];
            };
            std::vector<uint8_t> used(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
            char color[40];
            constexpr int kMaxRects = 256;
            int rects = 0;
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++) {
                    const size_t at =
                        static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    const uint32_t v = pixel(x, y);
                    if (used[at] || v == 0)
                        continue;
                    int w = 1;
                    while (x + w < width && !used[at + static_cast<size_t>(w)] && pixel(x + w, y) == v)
                        w++;
                    int h = 1;
                    for (; y + h < height; h++) {
                        bool same = true;
                        for (int k = 0; k < w && same; k++)
                            same = !used[static_cast<size_t>(y + h) * static_cast<size_t>(width) +
                                         static_cast<size_t>(x + k)] &&
                                   pixel(x + k, y + h) == v;
                        if (!same)
                            break;
                    }
                    for (int yy = 0; yy < h; yy++)
                        for (int xx = 0; xx < w; xx++)
                            used[static_cast<size_t>(y + yy) * static_cast<size_t>(width) +
                                 static_cast<size_t>(x + xx)] = 1;
                    std::snprintf(color, sizeof(color), "#%08x", v);
                    out += "<div class=\"r\" style=\"left:" + std::to_string(dstX + x) +
                           "px;top:" + std::to_string(dstY + y) + "px;width:" + std::to_string(w) +
                           "px;height:" + std::to_string(h) + "px;background-color:" + color + ";\"></div>";
                    rects++;
                }
            // flat vanilla art becomes a few boxes; detailed pack art thousands (one gpu mesh each): draw as one texture
            if (rects > kMaxRects) {
                Texture crop;
                crop.width = width;
                crop.height = height;
                crop.pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
                for (int y = 0; y < height; y++)
                    for (int x = 0; x < width; x++) {
                        const int sx = srcX + x, sy = srcY + y;
                        if (sx < 0 || sy < 0 || sx >= img.width || sy >= img.height)
                            continue;
                        const uint8_t* p =
                            &img.rgba[(static_cast<size_t>(sy) * static_cast<size_t>(img.width) +
                                       static_cast<size_t>(sx)) *
                                      4];
                        std::copy(p, p + 4,
                                  &crop.pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) +
                                                static_cast<size_t>(x)) *
                                               4]);
                    }
                const std::string head = "sprite:" + key;
                UiAssets::Instance().SetHead(head, std::move(crop));
                out = "<mcimg src=\"mchead:" + head +
                      "\" style=\"display:block;position:absolute;left:" + std::to_string(dstX) +
                      "px;top:" + std::to_string(dstY) + "px;width:" + std::to_string(width) +
                      "px;height:" + std::to_string(height) + "px;\"/>";
            }
        }
        std::lock_guard lock(mutex);
        return cache.emplace(key, std::move(out)).first->second;
    }
}
