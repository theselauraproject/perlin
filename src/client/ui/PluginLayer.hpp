#pragma once
#include "client/systems/mods/ModManager.hpp"
#include "client/ui/Scene.hpp"

#include <string>
#include <vector>

namespace perlin::ui {
    SceneDoc BuildPluginLayer(const std::vector<mods::UiLayer>& layers, int guiW, int guiH);
}
