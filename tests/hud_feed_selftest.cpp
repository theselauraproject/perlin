#include "client/ui/HudFeed.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace perlin;
using namespace perlin::ui;

namespace {
    int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                 \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

    bool RefLocator(double eyeX, double eyeY, double eyeZ, float yaw, float pitchF, float fovF, double tx,
                    double ty, double tz, LocatorMarker& out) {
        const double lookElevation = static_cast<double>(pitchF);
        const double halfFov = static_cast<double>(fovF) * 0.5;
        const double dx = tx - eyeX, dy = ty - eyeY, dz = tz - eyeZ;
        const double horizontal = std::sqrt(dx * dx + dz * dz);
        double angle = std::atan2(dz, dx) * 180.0 / 3.14159265358979323846 - static_cast<double>(yaw);
        angle = std::fmod(angle + 540.0, 360.0) - 180.0;
        LocatorMarker m;
        m.angle = static_cast<float>(angle);
        m.distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
        const double elevation = std::atan2(dy, std::max(horizontal, 1e-6)) * 180.0 / 3.14159265358979323846;
        const double rel = elevation - lookElevation;
        m.pitch = std::fabs(rel) <= halfFov ? 0 : (rel > 0 ? 1 : -1);
        out = m;
        return angle > -60.0 && angle <= 60.0;
    }

    float RefActionBar(int overlayTime, float partial) {
        const float f = static_cast<float>(overlayTime) - partial;
        return std::clamp(static_cast<int>(f * 255.0f / 20.0f), 0, 255) > 8
                   ? std::min(255, static_cast<int>(f * 255.0f / 20.0f)) / 255.0f
                   : 0.0f;
    }

    float RefTitle(int titleTime, int fadeIn, int stay, int fadeOut, float partial) {
        const float f = static_cast<float>(titleTime) - partial;
        int alpha = 255;
        if (titleTime > fadeOut + stay) {
            const float f1 = static_cast<float>(fadeIn + stay + fadeOut) - f;
            alpha = static_cast<int>(f1 * 255.0f / std::max(1, fadeIn));
        }
        if (titleTime <= fadeOut)
            alpha = static_cast<int>(f * 255.0f / std::max(1, fadeOut));
        alpha = std::clamp(alpha, 0, 255);
        return alpha > 8 ? static_cast<float>(alpha) / 255.0f : 0.0f;
    }

    int RefRarity(const std::string& shortName, bool enchanted) {
        int rarity = 0;
        static const char* const kUncommon[] = {"experience_bottle", "nether_star",   "totem_of_undying",
                                                "heart_of_the_sea",  "dragon_breath", "enchanted_book"};
        static const char* const kRare[] = {"golden_apple", "beacon", "conduit"};
        static const char* const kEpic[] = {
            "enchanted_golden_apple",  "dragon_egg",      "command_block", "chain_command_block",
            "repeating_command_block", "structure_block", "jigsaw"};
        for (const char* n : kUncommon)
            if (shortName == n)
                rarity = 1;
        for (const char* n : kRare)
            if (shortName == n)
                rarity = 2;
        for (const char* n : kEpic)
            if (shortName == n)
                rarity = 3;
        if (shortName.starts_with("music_disc_"))
            rarity = 2;
        if (enchanted && shortName != "enchanted_book" && shortName != "enchanted_golden_apple")
            rarity = rarity <= 1 ? 2 : 3;
        return rarity;
    }

    Texture RefHead(const std::vector<uint8_t>& rgba, int skinWidth) {
        const int sc = std::max(1, skinWidth / 64);
        Texture t;
        t.width = t.height = 8;
        t.pixels.assign(8 * 8 * 4, 255);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const auto px = [&](int ox, int oy, uint8_t out[4]) {
                    const size_t i = (static_cast<size_t>((oy + y) * sc + sc / 2) * skinWidth +
                                      static_cast<size_t>((ox + x) * sc + sc / 2)) *
                                     4;
                    for (int c = 0; c < 4; c++)
                        out[c] = i + 3 < rgba.size() ? rgba[i + c] : 255;
                };
                uint8_t face[4], hat[4];
                px(8, 8, face);
                px(40, 8, hat);
                uint8_t* o = &t.pixels[static_cast<size_t>(y * 8 + x) * 4];
                const float a = hat[3] / 255.0f;
                for (int c = 0; c < 3; c++)
                    o[c] = static_cast<uint8_t>(face[c] * (1.0f - a) + hat[c] * a);
                o[3] = 255;
            }
        return t;
    }
}

int main() {
    std::mt19937 rng(5);
    const auto real = [&](double lo, double hi) {
        return std::uniform_real_distribution<double>(lo, hi)(rng);
    };

    for (int i = 0; i < 200000; i++) {
        LocatorView v{real(-500, 500),
                      real(-64, 320),
                      real(-500, 500),
                      static_cast<float>(real(-720, 720)),
                      static_cast<float>(real(-90, 90)),
                      static_cast<float>(real(30, 110))};
        const double tx = real(-600, 600), ty = real(-64, 320), tz = real(-600, 600);
        LocatorMarker ref;
        const bool visible = RefLocator(v.eyeX, v.eyeY, v.eyeZ, v.yaw, v.pitch, v.fov, tx, ty, tz, ref);
        const auto got = ProjectLocator(v, tx, ty, tz);
        CHECK(visible == got.has_value());
        if (visible && got)
            CHECK(ref.angle == got->angle && ref.distance == got->distance && ref.pitch == got->pitch);
    }
    CHECK(ProjectLocator({0, 0, 0, 0, 0, 70}, 0, 0, 0).has_value() == [&] {
        LocatorMarker m;
        return RefLocator(0, 0, 0, 0, 0, 70, 0, 0, 0, m);
    }());

    for (float yaw : {-60.0f, 60.0f, -59.0f, 61.0f, -61.0f}) {
        const LocatorView v{0, 0, 0, yaw, 0, 70};
        LocatorMarker m;
        const bool visible = RefLocator(0, 0, 0, yaw, 0, 70, 10, 0, 0, m);
        CHECK(ProjectLocator(v, 10, 0, 0).has_value() == visible);
    }
    CHECK(ProjectLocator({0, 0, 0, -60.0f, 0, 70}, 10, 0, 0).has_value());
    CHECK(!ProjectLocator({0, 0, 0, 60.0f, 0, 70}, 10, 0, 0).has_value());

    for (int time = -5; time <= 400; time++)
        for (int k = 0; k < 40; k++) {
            const float partial = static_cast<float>(real(0, 1));
            CHECK(RefActionBar(time, partial) == ActionBarAlpha(time, partial));
            const int fadeIn = static_cast<int>(rng() % 30), stay = static_cast<int>(rng() % 100),
                      fadeOut = static_cast<int>(rng() % 30);
            CHECK(RefTitle(time, fadeIn, stay, fadeOut, partial) ==
                  TitleAlpha({time, fadeIn, stay, fadeOut}, partial));
        }

    const char* const names[] = {"diamond",          "experience_bottle",
                                 "nether_star",      "totem_of_undying",
                                 "golden_apple",     "beacon",
                                 "conduit",          "enchanted_golden_apple",
                                 "dragon_egg",       "command_block",
                                 "jigsaw",           "music_disc_13",
                                 "enchanted_book",   "stick",
                                 "heart_of_the_sea", "structure_block"};
    for (const char* n : names)
        for (bool ench : {false, true})
            CHECK(RefRarity(n, ench) == ItemRarity(n, ench));
    CHECK(RarityItemName(2, true, "Sword") == "\xC2\xA7"
                                              "b\xC2\xA7"
                                              "oSword");
    CHECK(RarityItemName(0, false, "Dirt") == "\xC2\xA7"
                                              "fDirt");

    for (int t = 0; t <= 40; t++)
        CHECK(ItemNameAlpha(t) == std::min(255.0f, static_cast<float>(t) * 256.0f / 10.0f) / 255.0f);

    for (int skinW : {64, 128, 256, 100}) {
        std::vector<uint8_t> rgba(static_cast<size_t>(skinW) * skinW * 4);
        for (auto& b : rgba)
            b = static_cast<uint8_t>(rng());
        CHECK(RefHead(rgba, skinW).pixels == SkinHeadTexture(rgba, skinW).pixels);
        rgba.resize(rgba.size() / 3);
        CHECK(RefHead(rgba, skinW).pixels == SkinHeadTexture(rgba, skinW).pixels);
    }
    const Texture def = DefaultHeadTexture();
    CHECK(def.width == 8 && def.pixels[0] == 170 && def.pixels[1] == 120 && def.pixels[2] == 90 &&
          def.pixels[3] == 255);
    CHECK(SkinHasHead(64, 16) && !SkinHasHead(63, 64) && !SkinHasHead(64, 15));

    CHECK(GameModeFromProtocol(0) == GameMode::Survival && GameModeFromProtocol(1) == GameMode::Creative &&
          GameModeFromProtocol(2) == GameMode::Adventure && GameModeFromProtocol(3) == GameMode::Spectator &&
          GameModeFromProtocol(4) == GameMode::Spectator && GameModeFromProtocol(6) == GameMode::Spectator &&
          GameModeFromProtocol(5) == GameMode::Survival);

    ChatSettings cs;
    cs.width = 0.5f;
    cs.scale = 0.0f;
    cs.lineSpacing = 0.5f;
    const ChatLog::Layout lay = ChatLayoutFor(cs);
    CHECK(lay.width == 180 && lay.scale == 0.1f && lay.lineHeight == 13 && lay.focusedLines == 180 / 13);

    std::printf(failures ? "hud_feed_selftest FAILED\n" : "hud_feed_selftest passed\n");
    return failures ? 1 : 0;
}
