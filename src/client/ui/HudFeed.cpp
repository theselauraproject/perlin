#include "client/ui/HudFeed.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace perlin::ui {
    namespace {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kLocatorHalfWidthDegrees = 60.0;
        constexpr int kHeadSize = 8;
        constexpr std::array<const char*, 6> kUncommon = {"experience_bottle", "nether_star",
                                                          "totem_of_undying",  "heart_of_the_sea",
                                                          "dragon_breath",     "enchanted_book"};
        constexpr std::array<const char*, 3> kRare = {"golden_apple", "beacon", "conduit"};
        constexpr std::array<const char*, 7> kEpic = {
            "enchanted_golden_apple",  "dragon_egg",      "command_block", "chain_command_block",
            "repeating_command_block", "structure_block", "jigsaw"};
        constexpr std::array<const char*, 4> kRarityCode = {"f", "e", "b", "d"};

        template <size_t N> bool OneOf(std::string_view name, const std::array<const char*, N>& names) {
            return std::ranges::any_of(names, [&](const char* n) { return name == n; });
        }
    }

    GameMode GameModeFromProtocol(int gameType) {
        switch (gameType) {
        case 1:
            return GameMode::Creative;
        case 2:
            return GameMode::Adventure;
        case 3:
        case 4:
        case 6:
            return GameMode::Spectator;
        default:
            return GameMode::Survival;
        }
    }

    ChatLog::Layout ChatLayoutFor(const ChatSettings& c) {
        ChatLog::Layout layout;
        layout.width = static_cast<int>(std::floor(c.width * 280.0f + 40.0f));
        layout.scale = std::max(0.1f, c.scale);
        layout.lineHeight = std::max(1, static_cast<int>(9.0f * (c.lineSpacing + 1.0f)));
        layout.focusedLines =
            static_cast<int>(std::floor(c.focusedHeight * 160.0f + 20.0f)) / layout.lineHeight;
        layout.unfocusedLines =
            static_cast<int>(std::floor(c.unfocusedHeight * 160.0f + 20.0f)) / layout.lineHeight;
        layout.textAlpha = c.textOpacity;
        layout.backgroundAlpha = c.backgroundOpacity;
        return layout;
    }

    std::optional<LocatorMarker> ProjectLocator(const LocatorView& view, double x, double y, double z) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(view.eyeX) ||
            !std::isfinite(view.eyeY) || !std::isfinite(view.eyeZ) || !std::isfinite(view.yaw) ||
            !std::isfinite(view.pitch) || !std::isfinite(view.fov))
            return std::nullopt;
        const double dx = x - view.eyeX, dy = y - view.eyeY, dz = z - view.eyeZ;
        const double horizontal = std::sqrt(dx * dx + dz * dz);
        double angle = std::atan2(dz, dx) * 180.0 / kPi - static_cast<double>(view.yaw);
        angle = std::fmod(angle + 540.0, 360.0) - 180.0;
        if (!(angle > -kLocatorHalfWidthDegrees && angle <= kLocatorHalfWidthDegrees))
            return std::nullopt;
        LocatorMarker m;
        m.angle = static_cast<float>(angle);
        m.distance = static_cast<float>(std::min(std::sqrt(dx * dx + dy * dy + dz * dz),
                                                static_cast<double>(std::numeric_limits<float>::max())));
        const double elevation = std::atan2(dy, std::max(horizontal, 1e-6)) * 180.0 / kPi;
        const double rel = elevation - static_cast<double>(view.pitch);
        const double halfFov = static_cast<double>(view.fov) * 0.5;
        m.pitch = std::fabs(rel) <= halfFov ? 0 : (rel > 0 ? 1 : -1);
        return m;
    }

    float ActionBarAlpha(int overlayTime, float partial) {
        const float remaining = static_cast<float>(overlayTime) - partial;
        if (remaining <= 0.0f)
            return 0.0f;
        const float x = std::clamp(1.0f - remaining / static_cast<float>(kActionBarTicks), 0.0f, 1.0f);
        const float eased = x <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * x - 10.0f);
        return std::clamp(1.0f - eased, 0.0f, 1.0f);
    }

    float TitleAlpha(const TitleTimes& t, float partial) {
        const float remaining = static_cast<float>(t.remaining) - partial;
        if (remaining <= 0.0f)
            return 0.0f;
        const float total = static_cast<float>(t.fadeIn + t.stay + t.fadeOut);
        const float elapsed = total - remaining;
        if (t.fadeIn > 0 && elapsed < static_cast<float>(t.fadeIn))
            return std::clamp(elapsed / static_cast<float>(t.fadeIn), 0.0f, 1.0f);
        if (remaining < static_cast<float>(t.fadeOut))
            return std::clamp(remaining / static_cast<float>(std::max(1, t.fadeOut)), 0.0f, 1.0f);
        return 1.0f;
    }

    int ItemRarity(std::string_view shortName, bool enchanted) {
        int rarity = 0;
        if (OneOf(shortName, kUncommon))
            rarity = 1;
        if (OneOf(shortName, kRare))
            rarity = 2;
        if (OneOf(shortName, kEpic))
            rarity = 3;
        if (shortName.starts_with("music_disc_"))
            rarity = 2;
        if (enchanted && shortName != "enchanted_book" && shortName != "enchanted_golden_apple")
            rarity = rarity <= 1 ? 2 : 3;
        return rarity;
    }

    std::string RarityItemName(int rarity, bool customNamed, const std::string& name) {
        return std::string("\xC2\xA7") + kRarityCode[static_cast<size_t>(std::clamp(rarity, 0, 3))] +
               (customNamed ? "\xC2\xA7o" : "") + name;
    }

    float ItemNameAlpha(int toolTimer) {
        return std::min(255.0f, static_cast<float>(toolTimer) * 256.0f / 10.0f) / 255.0f;
    }

    bool SkinHasHead(int skinWidth, int skinHeight) {
        return skinWidth >= 64 && skinHeight >= 16;
    }

    Texture DefaultHeadTexture() {
        Texture t;
        t.width = t.height = kHeadSize;
        t.pixels.assign(kHeadSize * kHeadSize * 4, 255);
        for (size_t i = 0; i < static_cast<size_t>(kHeadSize) * kHeadSize; i++) {
            t.pixels[i * 4] = 170;
            t.pixels[i * 4 + 1] = 120;
            t.pixels[i * 4 + 2] = 90;
        }
        return t;
    }

    Texture SkinHeadTexture(const std::vector<uint8_t>& rgba, int skinWidth) {
        const int sc = std::max(1, skinWidth / 64);
        Texture t;
        t.width = t.height = kHeadSize;
        t.pixels.assign(kHeadSize * kHeadSize * 4, 255);
        const auto sample = [&](int ox, int oy, int x, int y, uint8_t out[4]) {
            const size_t i = (static_cast<size_t>((oy + y) * sc + sc / 2) * skinWidth +
                              static_cast<size_t>((ox + x) * sc + sc / 2)) *
                             4;
            for (int c = 0; c < 4; c++)
                out[c] = i + 3 < rgba.size() ? rgba[i + c] : 255;
        };
        for (int y = 0; y < kHeadSize; y++)
            for (int x = 0; x < kHeadSize; x++) {
                uint8_t face[4], hat[4];
                sample(8, 8, x, y, face);
                sample(40, 8, x, y, hat);
                uint8_t* o = &t.pixels[static_cast<size_t>(y * kHeadSize + x) * 4];
                const float a = hat[3] / 255.0f;
                for (int c = 0; c < 3; c++)
                    o[c] = static_cast<uint8_t>(face[c] * (1.0f - a) + hat[c] * a);
                o[3] = 255;
            }
        return t;
    }
}
