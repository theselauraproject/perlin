// noise values from the 26.3 jar's own classes (tools/javaref/Oracle.java), seed 12345
#include "core/Json.hpp"
#include "world/vanilla/DataPack.hpp"
#include "world/vanilla/Noise.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace perlin::server::vanilla;

static int failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                      \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

namespace {
    uint32_t Bits(float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    }
}

int main() {
    CHECK(SeedFromHashOf("").hi != 0);
    {
        // xoroshiro: seed 12345/67890 from the real class
        WorldRandom r(12345ull, 67890ull);
        CHECK(static_cast<int64_t>(r.NextLong()) == 10516574265ll);
        CHECK(static_cast<int64_t>(r.NextLong()) == -8655457884744189457ll);
        CHECK(static_cast<int64_t>(r.NextLong()) == -8457254179187378397ll);
    }
    {
        struct Case {
            const char* name;
            NoiseParameters p;
            uint32_t expect[6];
        };
        const double pts[6][3] = {{0, 0, 0},        {100.5, 70.25, -200.75},
                                  {-512, 100, 640}, {12345.678, -33.5, -98765.4321},
                                  {1e6, 5, -1e6},   {0.001, 0.002, 0.003}};
        NoiseParameters cont;
        cont.baseAmplitude = 0.8880832896205223;
        cont.baseOctave = -9;
        cont.octaveCount = 9;
        cont.amplitudeModifiers = {1, 1, 2, 2, 2, 1, 1, 1, 1};
        // RandomState: WorldRandom(seed).forkPositional(), then fromHashOf("minecraft:<name>")
        WorldRandom root = WorldRandom::FromSeed(12345);
        const WorldRandom::Factory positional = root.ForkPositional();
        NormalNoise noise(cont, positional.FromHashOf("minecraft:continentalness"));
        const uint32_t expected[6] = {0xbece23a6, 0x3f01a0f8, 0xbd6b554b, 0xbd6c10e3, 0xbf6d4f25, 0xbece27c2};
        for (int i = 0; i < 6; i++) {
            const uint32_t got = Bits(noise.Get(pts[i][0], pts[i][1], pts[i][2]));
            if (got != expected[i]) {
                std::printf("continentalness[%d]: got %08x expected %08x\n", i, got, expected[i]);
                failures++;
            }
        }
    }
    // the built-in noise definitions, against the real engine's values
    {
        auto pack = DataPack::Embedded();
        {
            struct Ref {
                const char* name;
                uint32_t expect[6];
            };
            static const Ref refs[] = {
                {"continentalness", {0xbece23a6, 0x3f01a0f8, 0xbd6b554b, 0xbd6c10e3, 0xbf6d4f25, 0xbece27c2}},
                {"erosion", {0xbf3c0caf, 0x3c0fd1fd, 0xbda886b0, 0x3e6407c7, 0x3e1868b4, 0xbf3c0d9e}},
                {"temperature", {0xbcca9ff2, 0xbea27065, 0x3ea0a25a, 0x3f38c2f9, 0x3cbe9be3, 0xbccaa202}},
                {"ridge", {0x3e33a71a, 0xbec190d7, 0xbec124f2, 0x3e209990, 0x3ef4bf2d, 0x3e33aefe}},
                {"offset", {0xbf0982fd, 0xbdeb4813, 0x3d80e03f, 0x3d4ec783, 0x3eeb64d5, 0xbf097734}},
                {"cave_layer", {0xbf35b4af, 0xbe5a5a71, 0xbe05d2c6, 0x3e59487d, 0x3d2c9c24, 0xbf35b360}},
                {"aquifer_barrier", {0x3c4779a0, 0x3f115c6f, 0x3c443a30, 0xbd241ef8, 0x3df78990, 0x3c508ae0}},
            };
            const double pts[6][3] = {{0, 0, 0},        {100.5, 70.25, -200.75},
                                      {-512, 100, 640}, {12345.678, -33.5, -98765.4321},
                                      {1e6, 5, -1e6},   {0.001, 0.002, 0.003}};
            WorldRandom root = WorldRandom::FromSeed(12345);
            const WorldRandom::Factory positional = root.ForkPositional();
            for (const Ref& ref : refs) {
                perlin::server::json::Value v;
                CHECK(perlin::server::json::Parse(
                    pack->Read(std::string("worldgen/noise/") + ref.name + ".json"), v));
                NoiseParameters p;
                p.baseAmplitude = v["base_amplitude"].Number(1.0);
                p.baseOctave = static_cast<int>(v["base_octave"].Int());
                p.octaveCount = static_cast<int>(v["octave_count"].Int(1));
                for (const auto& m : v["amplitude_modifiers"].items)
                    p.amplitudeModifiers.push_back(m.Number());
                NormalNoise noise(p, positional.FromHashOf(std::string("minecraft:") + ref.name));
                for (int i = 0; i < 6; i++) {
                    const uint32_t got = Bits(noise.Get(pts[i][0], pts[i][1], pts[i][2]));
                    if (got != ref.expect[i]) {
                        std::printf("%s[%d]: got %08x expected %08x\n", ref.name, i, got, ref.expect[i]);
                        failures++;
                    }
                }
            }
        }
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("vanilla_noise_test ok\n");
    return 0;
}
