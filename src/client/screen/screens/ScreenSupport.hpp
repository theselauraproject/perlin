#pragma once
#include "client/systems/multiplayer/HudItemConvert.hpp"
#include "client/screen/OverlayScreen.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/model/HudState.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace perlin::ui {
    class Hud;
}

namespace perlin::screen {
    glm::vec2 PointerOrPreview(const Input& input);
    bool IsEscape(const InputManager::Typed& typed);
    bool EscapeTyped(const std::vector<InputManager::Typed>& typed);
    void DumpGenerated(const std::string& relative, std::string_view text);
    void CloseOverlay(const Host& host, ui::Hud& hud, OverlayScreen& screen);
    void CenterCursorForGui(const Host& host);
}
