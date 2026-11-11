#pragma once
#include <string>
#include <string_view>
#include <vector>

// java biome ids as iris numbers them (registration order of net.minecraft.world.level.biome.Biomes in 26.3), the
// iris biome categories (BiomeUniforms.getBiomeCategory, from the biome tags) and the climate behind
// biome_precipitation; the client knows bedrock biome names, which map onto these
namespace perlin::jshader {
    struct JavaBiome {
        const char* name;
        int category;      // iris BiomeCategories ordinal (CAT_*)
        int precipitation; // has_precipitation
        float temperature, downfall;
    };
    const std::vector<JavaBiome>& JavaBiomes();
    const std::vector<std::string>& BiomeCategoryNames();
    // iris id of a bedrock (or java) biome name, with or without the minecraft: prefix; plains when unknown
    int JavaBiomeId(std::string_view name);
    // PPT_NONE / PPT_RAIN / PPT_SNOW at a height (java's height adjusted temperature, without its noise)
    int BiomePrecipitationAt(int id, double y);
}
