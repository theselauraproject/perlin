#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace perlin::jshader {
    struct JavaBiome {
        const char* name;
        int category;
        int precipitation;
        float temperature, downfall;
    };
    const std::vector<JavaBiome>& JavaBiomes();
    const std::vector<std::string>& BiomeCategoryNames();

    int JavaBiomeId(std::string_view name);

    int BiomePrecipitationAt(int id, double y);
}
