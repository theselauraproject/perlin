#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/systems/renderer/LightBandCache.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <memory>

namespace {
    using namespace perlin;
    using namespace std::chrono_literals;

    constexpr int32_t kOpaqueNetworkId = 0x5e1f0001;
    constexpr int32_t kLampNetworkId = 0x5e1f0002;

    struct Blocks {
        uint16_t opaque = 0, lamp = 0;
        Blocks() {
            BlockPalette& palette = selftest::G().blockPalette;
            palette.LearnName(kOpaqueNetworkId, "perlin_test:opaque");
            palette.LearnName(kLampNetworkId, "perlin_test:lamp");
            opaque = palette.Intern(kOpaqueNetworkId);
            lamp = palette.Intern(kLampNetworkId);
            selftest::G().lightRegistry.SetCustom("perlin_test:opaque", 0, kMaxLight);
            selftest::G().lightRegistry.SetCustom("perlin_test:lamp", 14, 0);
            selftest::G().lightRegistry.Invalidate();
        }
        ~Blocks() { selftest::G().lightRegistry.ClearCustom(); }
    };

    std::unique_ptr<Level> OpenLevel(int chunks) {
        auto level = std::make_unique<Level>();
        for (int cx = 0; cx < chunks; cx++)
            for (int cz = 0; cz < chunks; cz++) {
                level->EnsureChunk(cx, cz);
                level->MarkComplete(cx, cz);
            }
        return level;
    }
}

namespace perlin::selftest {
    int LightBandCache() {
        Checker t("light_band_cache_test");
        Blocks blocks;
        const auto start = std::chrono::steady_clock::now();

        {
            auto level = OpenLevel(1);
            perlin::LightBandCache cache;
            const uint8_t open = cache.At(*level, 4, 100, 4, start);
            t.Equal(ChunkLighting::Result::Sky(open), 15, "open sky above an empty chunk is fully lit");
            t.Equal(ChunkLighting::Result::Block(open), 0, "no emitters means no block light");
        }
        {
            auto level = OpenLevel(2);
            level->SetBlock(5, 70, 5, blocks.opaque);
            perlin::LightBandCache cache;
            const uint8_t shaded = cache.At(*level, 5, 69, 5, start);
            t.Check(ChunkLighting::Result::Sky(shaded) < 15, "a cell under an opaque block loses sky light");
            const uint8_t above = cache.At(*level, 5, 72, 5, start);
            t.Equal(ChunkLighting::Result::Sky(above), 15, "a cell above the block stays lit");
        }
        {
            auto level = OpenLevel(1);
            level->SetBlock(8, 64, 8, blocks.lamp);
            perlin::LightBandCache cache;
            const uint8_t beside = cache.At(*level, 9, 64, 8, start);
            t.Check(ChunkLighting::Result::Block(beside) >= 12, "a lamp lights its neighbours");
            const uint8_t distant = cache.At(*level, 15, 64, 8, start);
            t.Check(ChunkLighting::Result::Block(distant) < ChunkLighting::Result::Block(beside),
                    "block light falls off with distance");
        }
        {
            auto level = OpenLevel(1);
            perlin::LightBandCache cache;
            auto now = start;
            const uint8_t before = cache.At(*level, 8, 64, 8, now);
            t.Equal(ChunkLighting::Result::Block(before), 0, "dark before the lamp exists");

            level->SetBlock(8, 64, 8, blocks.lamp);
            const uint8_t soon = cache.At(*level, 9, 64, 8, now + 100ms);
            t.Equal(ChunkLighting::Result::Block(soon), 0, "changes inside the debounce window keep the cached band");

            const uint8_t later = cache.At(*level, 9, 64, 8, now + 600ms);
            t.Check(ChunkLighting::Result::Block(later) >= 12, "after the debounce the band is relit");

            level->SetBlock(8, 64, 8, 0);
            const uint8_t afterRemoval = cache.At(*level, 9, 64, 8, now + 1200ms);
            t.Equal(ChunkLighting::Result::Block(afterRemoval), 0, "removing the lamp darkens the band on the next refresh");
        }
        {
            auto level = OpenLevel(1);
            perlin::LightBandCache cache;
            auto now = start;
            cache.At(*level, 8, 64, 8, now);
            level->SetBlock(8, 64, 8, blocks.lamp);
            const uint8_t stale = cache.At(*level, 9, 64, 8, now + 5s);
            t.Check(ChunkLighting::Result::Block(stale) >= 12, "a changed band refreshes once it is past the debounce");
        }
        {
            auto level = OpenLevel(3);
            perlin::LightBandCache cache;
            const auto now = start;
            const uint8_t a = cache.At(*level, 8, 64, 8, now);
            const uint8_t b = cache.At(*level, 24, 64, 8, now);
            const uint8_t c = cache.At(*level, 40, 64, 8, now);
            t.Check(ChunkLighting::Result::Sky(a) == 15 && ChunkLighting::Result::Sky(b) == 15,
                    "two uncached bands are computed within one frame's budget");
            t.Equal(c, uint8_t{0xF0}, "a third uncached band in the same frame is deferred as sky-lit");
            const uint8_t again = cache.At(*level, 40, 64, 8, now + 16ms);
            t.Equal(ChunkLighting::Result::Sky(again), 15, "the deferred band is computed on the next frame");
        }
        {
            auto first = OpenLevel(1);
            auto second = OpenLevel(1);
            second->SetBlock(8, 64, 8, blocks.lamp);
            perlin::LightBandCache cache;
            const uint8_t one = cache.At(*first, 9, 64, 8, start);
            t.Equal(ChunkLighting::Result::Block(one), 0, "first level is dark");
            const uint8_t two = cache.At(*second, 9, 64, 8, start + 16ms);
            t.Check(ChunkLighting::Result::Block(two) >= 12, "switching level drops the previous level's bands");
        }
        return t.Finish();
    }
}
