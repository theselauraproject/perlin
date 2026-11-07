#pragma once

#include "client/player/PerlinBlockOffset.hpp"
#include "client/ui/Json.hpp"

namespace perlin {
    std::optional<physics::bds::BlockRandomOffset> ParseBlockOffsetDefinition(const ui::Json* value);
}
