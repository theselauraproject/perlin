#pragma once
#include "client/systems/resources/ServerBlocks.hpp"
#include "util/JsonValue.hpp"
namespace perlin {
    CollisionSet ParseBlockBoxComponent(const json::Value* component);
}
