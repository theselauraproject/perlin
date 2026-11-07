#pragma once
#include "client/ui/Json.hpp"

#include <cstdint>
#include <vector>

namespace perlin {
    bool NetworkNbtToJson(const std::vector<uint8_t>& bytes, ui::Json& out);
    size_t NetworkNbtPrefix(const uint8_t* data, size_t size, ui::Json& out);
}
