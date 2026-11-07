#include "client/player/PlayerFov.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include "player_fov_reference.hpp"

static void Check(bool ok, const char* reason) {
    if (!ok) {
        std::cerr << reason << '\n';
        std::exit(1);
    }
}
static float Float(uint32_t bits) {
    return std::bit_cast<float>(bits);
}
int main() {
    for (const auto& r : targetReferences)
        Check(std::bit_cast<uint32_t>(
                  perlin::MovementFovTarget(Float(r.speed), Float(r.walk), r.flying, r.slowness)) == r.target,
              "native movement FOV target differs");
    perlin::PlayerFov fov;
    Check(fov.Sample(.5f) == 1, "initial FOV");
    for (const auto& r : tickReferences) {
        fov.Tick(Float(r.target));
        Check(std::bit_cast<uint32_t>(fov.Sample(1)) == r.current, "native tick FOV differs");
        for (int i = 0; i != 5; ++i)
            Check(std::bit_cast<uint32_t>(fov.Sample(i * .25f)) == r.samples[i],
                  "native partial-tick FOV differs");
    }
    perlin::PlayerFov low, high;
    for (const auto& r : tickReferences) {
        low.Tick(Float(r.target));
        high.Tick(Float(r.target));
        for (int i = 0; i < 12; ++i)
            (void)high.Sample(i / 12.0f);
        Check(low.Sample(.5f) == high.Sample(.5f), "render FPS changes FOV");
    }
    Check(perlin::MovementFovTarget(.13f, .1f, false) < 1.3f, "sprint attribute multiplied twice");
    Check(perlin::MovementFovTarget(.3f, .1f, false) > 1.5f, "speed effect still clipped to old cap");
    for (float bad :
         {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        Check(std::isfinite(perlin::MovementFovTarget(.1f, bad, false)), "invalid ability poisons camera");
    Check(fov.Sample(-1) == fov.Sample(0) && fov.Sample(2) == fov.Sample(1), "partial tick not bounded");
    std::cout << "native FOV targets, multi-tick transitions and frame interpolation passed\n";
}
