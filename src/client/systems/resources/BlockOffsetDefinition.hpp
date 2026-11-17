#pragma once

#include "world/movement/PerlinBlockOffset.hpp"
#include "util/JsonValue.hpp"

namespace perlin {
    std::optional<physics::bds::BlockRandomOffset> ParseBlockOffsetDefinition(const json::Value* value);
}
