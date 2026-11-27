#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin {
    struct ItemNbtInfo {
        bool ok = false;
        int tagCount = 0;
        bool hasDamage = false;
        int damage = -1;
        bool hasColor = false;
        int32_t color = 0;
        std::string customName;
        std::vector<std::pair<int, int>> enchants;
        std::vector<std::string> lore;
    };

    ItemNbtInfo ParseItemNbt(const std::vector<uint8_t>& bytes);

    // Item-stack NBT uses little-endian tags. Keep unrelated tags byte-for-byte
    // when an authoritative armor update replaces the root Damage int tag.
    bool SetItemNbtDamage(std::vector<uint8_t>& bytes, int16_t damage);
}
