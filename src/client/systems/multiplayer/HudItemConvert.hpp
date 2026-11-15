#pragma once
#include "client/systems/model/HudState.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"

namespace perlin {
    ui::HudItem ToHudItem(const BedrockSession::ItemStackInfo& info);
}
