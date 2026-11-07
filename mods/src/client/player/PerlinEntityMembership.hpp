#pragma once

#include <cstdint>
#include <span>

namespace perlin::physics::bds {
    struct EntityMembership {
        std::span<const std::span<const uint32_t>> pages;

        bool Contains(uint32_t entity) const {
            const uint32_t index = entity & 0x3FFFF;
            const uint32_t page = index >> 11;
            if (page >= pages.size() || (index & 0x7FF) >= pages[page].size())
                return false;
            return (pages[page][index & 0x7FF] ^ (entity & 0xFFFC0000)) < 0x3FFFF;
        }
    };
}
