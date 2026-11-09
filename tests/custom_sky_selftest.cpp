#include "client/renderer/CustomSky.hpp"
#include "client/renderer/SkyGeometry.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>

using namespace perlin;

static int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static bool Near(float a, float b, float e = 1e-3f) {
    return std::fabs(a - b) < e;
}

int main() {
    sky::Layer l;
    std::string error;
    // a night layer: fades in 18:00-19:00, out 5:00-6:00
    CHECK(sky::ParseLayer("startFadeIn=18:00\nendFadeIn=19:00\nendFadeOut=6:00\nblend=add\nrotate=true\n"
                          "speed=0.5\naxis=1 0 0\n",
                          "optifine/sky/world0", 1, l, error));
    CHECK(l.source == "optifine/sky/world0/sky1.png");
    CHECK(l.startFadeIn == 12000 && l.endFadeIn == 13000 && l.endFadeOut == 0);
    // startFadeOut follows from the other three: as long as the fade in
    CHECK(l.startFadeOut == 23000);
    CHECK(Near(sky::FadeBrightness(l, 12500), 0.5f));
    CHECK(Near(sky::FadeBrightness(l, 18000), 1.0f));
    CHECK(Near(sky::FadeBrightness(l, 23500), 0.5f));
    CHECK(Near(sky::FadeBrightness(l, 6000), 0.0f));
    // parseAxis keeps (z, y, -x)
    CHECK(l.axis == glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(l.blend == sky::Blend::Add && Near(l.speed, 0.5f));

    // no times: always shown
    sky::Layer always;
    CHECK(sky::ParseLayer("source=./clouds.png\nblend=alpha\nrotate=false\nweather=clear rain\n", "mcpatcher/sky/world0",
                          2, always, error));
    CHECK(always.source == "mcpatcher/sky/world0/clouds.png");
    CHECK(!always.timed && Near(sky::FadeBrightness(always, 777), 1.0f));
    CHECK(always.clear && always.rain && !always.thunder);

    // partial times are invalid
    sky::Layer bad;
    CHECK(!sky::ParseLayer("startFadeIn=18:00\n", "optifine/sky/world0", 3, bad, error));

    sky::Layer ranges;
    CHECK(sky::ParseLayer("heights=(-64)-0 100-120\ndays=0 2-3\ndaysLoop=4\nsource=~/sky/x.png\n", "optifine/sky/world0", 4,
                          ranges, error));
    CHECK(ranges.heights.size() == 2 && ranges.heights[0] == std::make_pair(-64, 0));
    CHECK(ranges.days.size() == 2 && ranges.daysLoop == 4);
    CHECK(ranges.source == "optifine/sky/x.png");

    // weather and rotation through Evaluate
    sky::Set set;
    set.layers.push_back(always);
    sky::Inputs in;
    in.rain = 1.0f;
    in.thunder = 0.0f;
    auto draws = sky::Evaluate(set.layers, in);
    CHECK(draws.size() == 1 && Near(draws[0].brightness, 1.0f));
    set.layers[0].rain = false;
    draws = sky::Evaluate(set.layers, in);
    CHECK(draws.empty());

    // the OptiFine box: template cells land on their labelled sides once the sky's -90 degree turn is applied
    const auto box = BuildCustomSkyBox();
    const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 1, 0));
    const glm::vec3 expected[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}, {1, 0, 0}};
    for (int side = 0; side < 6; side++) {
        glm::vec3 centre(0.0f);
        for (int i = 0; i < 4; i++) {
            const TextureVertex& v = box[static_cast<size_t>(side) * 4 + i];
            centre += glm::vec3(turn * glm::vec4(v.x, v.y, v.z, 1.0f)) / 4.0f;
        }
        CHECK(glm::length(centre / 100.0f - expected[side]) < 1e-3f);
    }
    // renderSkyEnd: six faces, one per axis direction, tiled 16 times
    const auto end = BuildEndSkyBox();
    CHECK(end.size() == 24);
    glm::vec3 sum(0.0f);
    for (int face = 0; face < 6; face++) {
        glm::vec3 centre(0.0f);
        for (int i = 0; i < 4; i++) {
            const TextureVertex& v = end[static_cast<size_t>(face) * 4 + i];
            centre += glm::vec3(v.x, v.y, v.z) / 4.0f;
            CHECK(v.u == 0.0f || v.u == 16.0f);
        }
        CHECK(Near(glm::length(centre), 100.0f));
        sum += centre;
    }
    CHECK(glm::length(sum) < 1e-3f);

    // sun.properties: the source defaults to the png of the same name, the blend is the file's
    sky::Layer sun;
    CHECK(sky::ParseLayer("source=./sun.png\nblend=alpha\n", "optifine/sky/world0", 0, sun, error));
    CHECK(sun.source == "optifine/sky/world0/sun.png" && sun.blend == sky::Blend::Alpha);

    std::printf("custom_sky_selftest: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
