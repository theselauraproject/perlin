#include "client/multiplayer/SkinLoader.hpp"

#include "client/resources/TextureLoader.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace perlin {
    namespace {
        std::string HashId(const char* prefix, const std::vector<uint8_t>& data, int w, int h) {
            uint64_t hash = 1469598103934665603ULL;
            for (uint8_t b : data) {
                hash ^= b;
                hash *= 1099511628211ULL;
            }
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s_%dx%d_%016llx", prefix, w, h,
                          static_cast<unsigned long long>(hash));
            return buf;
        }

        bool SizeIn(int w, int h, std::initializer_list<std::pair<int, int>> allowed) {
            return std::any_of(allowed.begin(), allowed.end(),
                               [&](auto& s) { return s.first == w && s.second == h; });
        }
    }

    std::vector<uint8_t> UpgradeLegacySkin(const std::vector<uint8_t>& src) {
        std::vector<uint8_t> out(64 * 64 * 4, 0);
        std::memcpy(out.data(), src.data(), std::min<size_t>(src.size(), 64 * 32 * 4));
        struct Copy {
            int x, y, dx, dy, w, h;
        };
        static constexpr Copy kCopies[] = {
            {4, 16, 16, 32, 4, 4},   {8, 16, 16, 32, 4, 4},    {0, 20, 24, 32, 4, 12},
            {4, 20, 16, 32, 4, 12},  {8, 20, 8, 32, 4, 12},    {12, 20, 16, 32, 4, 12},
            {44, 16, -8, 32, 4, 4},  {48, 16, -8, 32, 4, 4},   {40, 20, 0, 32, 4, 12},
            {44, 20, -8, 32, 4, 12}, {48, 20, -16, 32, 4, 12}, {52, 20, -8, 32, 4, 12},
        };
        for (const Copy& c : kCopies)
            for (int yy = 0; yy < c.h; yy++)
                for (int xx = 0; xx < c.w; xx++) {
                    const int sx = c.x + xx, sy = c.y + yy;
                    const int tx = c.x + c.dx + (c.w - 1 - xx), ty = c.y + c.dy + yy;
                    std::memcpy(&out[(static_cast<size_t>(ty) * 64 + tx) * 4],
                                &out[(static_cast<size_t>(sy) * 64 + sx) * 4], 4);
                }
        return out;
    }

    bool DetectSlimSkin(const std::vector<uint8_t>& rgba, int width, int height) {
        if (width < 64 || height < 64 ||
            rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
            return false;
        const float s = static_cast<float>(width) / 64.0f;
        const auto transparentFraction = [&](int x0, int x1, int y0, int y1) {
            int total = 0, clear = 0;
            const int px0 = static_cast<int>(static_cast<float>(x0) * s),
                      px1 = static_cast<int>(static_cast<float>(x1) * s);
            const int py0 = static_cast<int>(static_cast<float>(y0) * s),
                      py1 = static_cast<int>(static_cast<float>(y1) * s);
            for (int y = py0; y < py1 && y < height; y++)
                for (int x = px0; x < px1 && x < width; x++) {
                    total++;
                    if (rgba[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) *
                                 4 +
                             3] < 16)
                        clear++;
                }
            return total == 0 ? 0.0f : static_cast<float>(clear) / static_cast<float>(total);
        };
        const float right = transparentFraction(54, 56, 20, 32);
        const float left = transparentFraction(46, 48, 52, 64);
        const float rightFront = transparentFraction(47, 48, 20, 32);
        if (right >= 0.9f && left >= 0.9f)
            return true;
        if (right >= 0.9f && left < 0.1f && rightFront < 0.1f)
            return true;
        return false;
    }

    bool LoadSkinConfig(const SkinConfig& config, LoadedSkin& out, std::string& error) {
        out = {};
        out.slimArms = config.slimArms;
        if (!config.skinPath.empty()) {
            Texture tex;
            if (!LoadTexture(config.skinPath, tex) || tex.pixels.empty()) {
                error = "could not read skin image '" + config.skinPath + "' (it must be a PNG)";
                return false;
            }
            if (!SizeIn(tex.width, tex.height, {{64, 32}, {64, 64}, {128, 128}})) {
                error = "skin '" + config.skinPath + "' is " + std::to_string(tex.width) + "x" +
                        std::to_string(tex.height) + "; Minecraft accepts 64x32, 64x64 or 128x128";
                return false;
            }
            if (tex.width == 64 && tex.height == 32) {
                out.skinRgba = UpgradeLegacySkin(tex.pixels);
                out.skinWidth = out.skinHeight = 64;
            } else {
                out.skinRgba = std::move(tex.pixels);
                out.skinWidth = tex.width;
                out.skinHeight = tex.height;
            }
            if (config.autoSlim)
                out.slimArms = DetectSlimSkin(out.skinRgba, out.skinWidth, out.skinHeight);
            out.skinId =
                HashId("Perlin", out.skinRgba, out.skinWidth, out.skinHeight) + (out.slimArms ? "_slim" : "");
        }
        if (!config.capePath.empty()) {
            Texture tex;
            if (!LoadTexture(config.capePath, tex) || tex.pixels.empty()) {
                error = "could not read cape image '" + config.capePath + "' (it must be a PNG)";
                return false;
            }
            if (!SizeIn(tex.width, tex.height, {{64, 32}, {128, 64}, {256, 128}})) {
                error = "cape '" + config.capePath + "' is " + std::to_string(tex.width) + "x" +
                        std::to_string(tex.height) + "; Minecraft accepts 64x32, 128x64 or 256x128";
                return false;
            }
            out.capeRgba = std::move(tex.pixels);
            out.capeWidth = tex.width;
            out.capeHeight = tex.height;
            out.capeId = HashId("PerlinCape", out.capeRgba, out.capeWidth, out.capeHeight);
        }
        return true;
    }
}
