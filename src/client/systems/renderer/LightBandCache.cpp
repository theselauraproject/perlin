#include "client/systems/renderer/LightBandCache.hpp"

namespace perlin {
    uint8_t LightBandCache::At(Level& level, int bx, int by, int bz, TimePoint now) {
        if (owner != &level || bands.size() > 2048) {
            bands.clear();
            owner = &level;
        }
        if (now != budgetFrame) {
            budgetFrame = now;
            budget = 2;
        }
        const int cx = bx >> 4, cz = bz >> 4, band = by >> 4;
        const uint64_t key = (static_cast<uint64_t>(cx & 0xFFFFF) << 28) |
                             (static_cast<uint64_t>(cz & 0xFFFFF) << 8) |
                             static_cast<uint64_t>((band + 128) & 0xFF);
        Band& b = bands[key];
        const auto age = now - b.at;
        const bool fresh = b.at == TimePoint{};
        const uint64_t counter = level.changeCounter.load(std::memory_order_relaxed);
        if (fresh && budget <= 0)
            return 0xF0;
        if (fresh || (budget > 0 && ((counter != b.counter && age > std::chrono::milliseconds(400)) ||
                                          age > std::chrono::seconds(20)))) {
            budget--;
            if (ChunkLighting::Compute(level, {cx, cz}, band * 16 - 8, band * 16 + 23, b.lit, true)) {
                b.at = now;
                b.counter = counter;
            } else if (fresh) {
                return 0xF0;
            }
        }
        return b.lit.At(bx & 15, by, bz & 15);
    }
}
