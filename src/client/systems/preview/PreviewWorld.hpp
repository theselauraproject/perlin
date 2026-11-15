#pragma once
#include "world/level/Level.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin {
    bool BuildPreviewWorld(Level& level, int radiusChunks);
    std::vector<std::pair<int32_t, std::string>> PreviewBlockList();
}
