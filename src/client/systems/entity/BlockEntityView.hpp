#pragma once
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/model/BlockEntities.hpp"
#include "client/systems/gfx/Tessellator.hpp"

#include <vector>

namespace perlin::entity {
    void BuildBlockEntityDraws(const std::vector<SpecialBlock>& specials, const BlockEntityStore& store,
                               std::vector<BlockEntityDraw>& out);
}
