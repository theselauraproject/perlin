#pragma once
#include "client/mods/ModManager.hpp"

#include <string>
#include <vector>

namespace perlin::ui {
    std::string BuildPluginLayerRml(const std::vector<mods::UiLayer>& layers, int guiW, int guiH);
}
