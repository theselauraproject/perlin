#pragma once
#include "client/resources/ServerBlocks.hpp"
#include "client/ui/Json.hpp"
namespace perlin {
    CollisionSet ParseBlockBoxComponent(const ui::Json* component);
}
