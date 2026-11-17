#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin {
    struct ItemNbtInfo {
        bool ok = false;
        int tagCount = 0;
        int damage = -1;
        bool hasColor = false;
        int32_t color = 0;
        std::string customName;
        std::vector<std::pair<int, int>> enchants;
        std::vector<std::string> lore;
    };

    ItemNbtInfo ParseItemNbt(const std::vector<uint8_t>& bytes);
}
