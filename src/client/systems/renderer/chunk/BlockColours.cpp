#include "client/systems/renderer/chunk/BlockColours.hpp"

#include "client/systems/blocks/BiomeTints.hpp"
#include "client/systems/blocks/BlockLibrary.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace perlin {
    namespace {
        bool Has(const std::string& s, const char* part) {
            return s.contains(part);
        }
    }

    BlockColours::Rgb BlockColours::ColourOf(uint16_t id, int biome, bool top, const BlockLibrary& blocks,
                                     const BiomeTints& tints) {
        const uint32_t key =
            (static_cast<uint32_t>(id) << 9) | static_cast<uint32_t>(biome & 0xff) << 1 | (top ? 1u : 0u);
        const auto cached = colours.find(key);
        if (cached != colours.end())
            return cached->second;

        const BlockAppearance& look = blocks.Look(id);
        const int tile = look.top;
        const int px = BlockLibrary::TilePx();
        const std::vector<uint8_t>& atlas = blocks.AtlasPixels();
        float sum[3] = {0, 0, 0};
        float weight = 0;
        const size_t base = static_cast<size_t>(tile) * px * px * 4;
        if (tile > 0 && base + static_cast<size_t>(px) * px * 4 <= atlas.size()) {
            for (int i = 0; i < px * px; i++) {
                const uint8_t* p = &atlas[base + static_cast<size_t>(i) * 4];
                const float a = p[3] / 255.0f;
                sum[0] += p[0] * a;
                sum[1] += p[1] * a;
                sum[2] += p[2] * a;
                weight += a;
            }
        }
        float c[3] = {0.5f, 0.5f, 0.5f};
        if (weight > 0)
            for (int i = 0; i < 3; i++)
                c[i] = sum[i] / weight / 255.0f;

        const std::string name = BlockPalette::Instance().Name(id);
        if (top && Has(name, "red_mushroom_block")) {
            c[0] = 0.71f;
            c[1] = 0.16f;
            c[2] = 0.15f;
        } else if (top && Has(name, "brown_mushroom_block")) {
            c[0] = 0.58f;
            c[1] = 0.44f;
            c[2] = 0.32f;
        }
        std::array<float, 3> tint = {1, 1, 1};
        const std::string biomeName = tints.Ready() ? tints.NameForId(biome) : std::string();
        if (Has(name, "water")) {
            if (tints.Ready())
                tint = tints.Water(biomeName);
            c[0] = c[1] = c[2] = 1.0f;
        } else if (tints.Ready() && (Has(name, "leaves") || (Has(name, "vine") && !Has(name, "cave_vines") &&
                                                               !Has(name, "twisting") && !Has(name, "weeping")))) {
            const float hi = std::max({c[0], c[1], c[2]}), lo = std::min({c[0], c[1], c[2]});
            const bool coloured = (tile > 0 && blocks.SaturationOf(tile) > 0.25f) || (hi > 0.0f && (hi - lo) / hi > 0.18f) ||
                                  Has(name, "cherry") || Has(name, "azalea") || Has(name, "pale_oak");
            if (!coloured)
                tint = Has(name, "birch")      ? tints.MapAverage("birch")
                       : Has(name, "spruce")   ? tints.MapAverage("evergreen")
                       : Has(name, "mangrove") ? tints.MapAverage("mangrove_swamp_foliage")
                                               : tints.Foliage(biomeName);
        } else if (tints.Ready() && (name == "minecraft:grass_block" || name == "minecraft:short_grass" ||
                                     name == "minecraft:tall_grass" || name == "minecraft:fern" ||
                                     name == "minecraft:large_fern" || name == "minecraft:sugar_cane" ||
                                     name == "minecraft:reeds" || look.tintTop)) {
            tint = tints.Grass(biomeName);
        } else {
            tint = {look.tintR, look.tintG, look.tintB};
        }
        Rgb out;
        out.r = static_cast<uint8_t>(std::clamp(c[0] * tint[0], 0.0f, 1.0f) * 255.0f + 0.5f);
        out.g = static_cast<uint8_t>(std::clamp(c[1] * tint[1], 0.0f, 1.0f) * 255.0f + 0.5f);
        out.b = static_cast<uint8_t>(std::clamp(c[2] * tint[2], 0.0f, 1.0f) * 255.0f + 0.5f);
        colours[key] = out;
        return out;
    }
}
