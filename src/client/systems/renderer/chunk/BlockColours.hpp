#pragma once
#include <cstdint>
#include <unordered_map>

namespace perlin {
    class BlockLibrary;
    class BiomeTints;

    class BlockColours {
    public:
        struct Rgb {
            uint8_t r, g, b;
        };
        Rgb ColourOf(uint16_t id, int biome, bool top, const BlockLibrary& blocks, const BiomeTints& tints);
        void Invalidate() { colours.clear(); }

    private:
        std::unordered_map<uint32_t, Rgb> colours;
    };
}
