#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include "client/ui/HudState.hpp"
#include "client/ui/RmlEmit.hpp"

namespace perlin::ui {
    class JavaRandom {
    public:
        explicit JavaRandom(int64_t seed) { SetSeed(seed); }
        void SetSeed(int64_t seed) { state = (static_cast<uint64_t>(seed) ^ 0x5DEECE66Dull) & kMask; }
        int NextInt(int bound);

    private:
        int Next(int bits);
        static constexpr uint64_t kMask = (1ull << 48) - 1;
        uint64_t state = 0;
    };

    uint32_t LocatorColorFor(const std::array<uint8_t, 16>& id);
    void RegisterHudRenderers(RmlEmitter& emitter, const HudState& state, const UiServices& services);

    // lets mods hide or move a vanilla hud element by renderer name ("heart_renderer", "cursor_renderer"...);
    // false hides it; dx/dy move it in gui pixels
    using HudElementFilter = std::function<bool(std::string_view renderer, float& dx, float& dy)>;
    void SetHudElementFilter(HudElementFilter filter);
    bool FilterHudElement(std::string_view renderer, float& dx, float& dy);
    // bumped when the filter's answers change: hud rebuilt
    uint64_t HudFilterVersion();
    void BumpHudFilter();
}
