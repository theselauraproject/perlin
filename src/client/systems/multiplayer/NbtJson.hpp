#pragma once
#include "util/JsonValue.hpp"

#include <cstdint>
#include <vector>

namespace perlin {
    bool NetworkNbtToJson(const std::vector<uint8_t>& bytes, json::Value& out);
    size_t NetworkNbtPrefix(const uint8_t* data, size_t size, json::Value& out);
}
