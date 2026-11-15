#include "client/screen/screens/ScreenSupport.hpp"

#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/Hud.hpp"
#include "util/Env.hpp"
#include "util/FileIo.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace perlin::screen {
    glm::vec2 PointerOrPreview(const Input& input) {
        glm::vec2 p(input.MouseX(), input.MouseY());
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &p.x, &p.y);
        return p;
    }

    bool IsEscape(const InputManager::Typed& typed) {
        return !typed.character && typed.key == Key::Escape;
    }

    bool EscapeTyped(const std::vector<InputManager::Typed>& typed) {
        return std::ranges::any_of(typed, [](const InputManager::Typed& t) { return IsEscape(t); });
    }

    void DumpGenerated(const std::string& relative, std::string_view text) {
        file::Write(paths::DataDir() + "/ui_generated/" + relative, text);
    }

    void CloseOverlay(const Host& host, ui::Hud& hud, OverlayScreen& screen) {
        hud.Overlay().ClearOverlay();
        host.screens.Close(screen);
        screen.leftDown = false;
        screen.signature.clear();
        if (!host.chat.IsOpen())
            host.input.SetTextMode(false);
    }

    void CenterCursorForGui(const Host& host) {
        if (host.options.centerCursor)
            host.input.RequestCursorCenter();
    }
}
