#include "client/input/KeyMapping.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/Environment.hpp"
#include "client/renderer/Fog.hpp"

#include <cstdio>
#include <initializer_list>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main() {
    for (int key : std::initializer_list<int>{'A', 'Z', '0', '9', Key::Space, Key::Shift, Key::Escape,
                                              Key::Up, Key::F1, Key::F1 + 4, Key::F12}) {
        const std::string name = KeyName(key);
        CHECK(!name.empty());
        CHECK(KeyFromName(name) == key);
    }

    CHECK(KeyFromName("key.keyboard.nonsense") == 0);
    CHECK(KeyFromName("w") == 0);
    CHECK(MouseFromName(MouseName(Mouse::Right)) == Mouse::Right);

    const KeyBindings defaults = DefaultBindings();
    for (size_t i = 0; i < kActionCount; i++) {
        const Binding& b = defaults[i];
        CHECK(b.device != Binding::Device::None);
        CHECK(Binding::Parse(b.Serialize()) == b);
    }
    CHECK(Binding::Parse("").device == Binding::Device::None);

    for (int code : {Pad::A, Pad::Y, Pad::Start, Pad::LeftStick, Pad::LeftTrigger, Pad::RightTrigger,
                     Pad::LeftStickUp, Pad::RightStickRight}) {
        const Binding b = Binding::PadButton(code);
        CHECK(Binding::Parse(b.Serialize()) == b);
    }
    CHECK(Binding::Parse("pad.nonsense").device == Binding::Device::None);
    const KeyBindings pad = DefaultPadBindings();
    for (Action a : {Action::Forward, Action::Back, Action::Left, Action::Right, Action::Jump, Action::Sneak,
                     Action::Attack, Action::Use, Action::Pause})
        CHECK(pad[static_cast<size_t>(a)].device == Binding::Device::Gamepad);

    const auto near = [](float a, float b, float tolerance = 1.0e-3f) {
        return std::fabs(a - b) <= tolerance;
    };
    const auto at = [](double ticks) {
        EnvironmentInput input;
        input.ticks = ticks;
        return SampleEnvironment(input);
    };
    const float pi = 3.14159265f;
    const auto angleNear = [&](float a, float b, float tolerance) {
        const float twoPi = 2.0f * pi;
        float d = std::fmod(std::fabs(a - b), twoPi);
        return std::min(d, twoPi - d) <= tolerance;
    };

    CHECK(angleNear(at(6000.5).sunAngle, 0.0f, 1.0e-3f));
    CHECK(angleNear(at(18000.0).sunAngle, pi, 5.0e-3f));
    CHECK(angleNear(at(6000.0).moonAngle, pi, 5.0e-3f));
    CHECK(near(at(6000.0).skyColor.z, 1.0f));
    CHECK(near(at(18000.0).skyColor.z, 0.0f));
    CHECK(at(18000.0).starBrightness == 0.5f);
    CHECK(at(6000.0).starBrightness == 0.0f);
    CHECK(near(at(6000.0).cloudColor.w, 0.8f));
    CHECK(near(at(18000.0).skyLightFactor, 0.24f));
    CHECK(near(at(6000.0).skyLightFactor, 1.0f));
    CHECK(at(6000.0).sunriseColor.w == 0.0f);
    CHECK(at(12243.0).sunriseColor.w > 0.6f);
    CHECK(at(30000.0).skyColor == at(6000.0).skyColor);
    CHECK(at(-18000.0).skyColor == at(6000.0).skyColor);
    for (int day = 0; day < 16; day++)
        CHECK(at(day * 24000.0 + 5000.0).moonPhase == day % 8);
    CHECK(EaseSunCurve(0.0f) == 0.0f && near(EaseSunCurve(1.0f), 1.0f) && near(EaseSunCurve(0.5f), 0.5f));
    CHECK(near(at(0.0).sunAngle, 2.0f * pi * EaseSunCurve(0.75f), 5.0e-3f));

    EnvironmentInput raining;
    raining.ticks = 6000.0;
    raining.rain = 1.0f;
    CHECK(SampleEnvironment(raining).skyColor.z < at(6000.0).skyColor.z);
    CHECK(SampleEnvironment(raining).starBrightness == 0.0f);

    LightmapParams day;
    day.skyFactor = 1.0f;
    day.skyLightColor = glm::vec3(1.0f);
    day.ambientColor = glm::vec3(10.0f / 255.0f);
    day.blockLightTint = glm::vec3(1.0f, 216.0f / 255.0f, 140.0f / 255.0f);
    uint8_t map[kLightmapBytes];
    BuildLightmap(day, map);
    auto texel = [&](int sky, int block) { return map + (sky * kLightmapSize + block) * 4; };
    CHECK(texel(15, 0)[0] == 255 && texel(15, 0)[2] == 255);
    CHECK(texel(0, 0)[0] == 24);
    CHECK(texel(0, 8)[2] < texel(0, 8)[0]);
    for (int level = 0; level < 15; level++)
        CHECK(texel(level, 0)[1] <= texel(level + 1, 0)[1]);
    day.skyFactor = 0.24f;
    uint8_t dim[kLightmapBytes];
    BuildLightmap(day, dim);
    CHECK(dim[(15 * kLightmapSize) * 4 + 1] < map[(15 * kLightmapSize) * 4 + 1]);

    FogInput fogInput;
    fogInput.renderDistanceChunks = 8;
    const FogState fog = ComputeFog(at(6000.0), fogInput);
    CHECK(near(fog.renderDistanceEnd, 128.0f) && near(fog.renderDistanceStart, 115.2f));
    CHECK(near(fog.skyEnd, 128.0f) && near(fog.cloudEnd, 2048.0f));
    CHECK(near(fog.environmentalEnd, 1024.0f));
    CHECK(fog.color.w == 1.0f);
    CHECK(ComputeFog(at(18000.0), fogInput).color.z < fog.color.z);

    std::printf(failures ? "%d FAILED\n" : "input self-tests passed\n", failures);
    return failures ? 1 : 0;
}
