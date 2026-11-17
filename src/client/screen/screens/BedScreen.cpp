#include "client/ui/GuiViewport.hpp"
#include "client/screen/screens/BedScreen.hpp"
#include "util/Resources.hpp"
#include "client/screen/screens/ScreenSupport.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/Hud.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "util/Log.hpp"
#include "util/Strings.hpp"

#include "client/ui/SceneEmit.hpp"
#include "client/ui/SceneKit.hpp"

namespace perlin::ui {
    SceneDoc BuildBedScreen(const std::string& label, int guiW, int guiH) {
        SceneDoc doc;
        doc.scene.theme = Theme::Bed;
        doc.scene.sprites = true;
        SceneBuilder b(doc.scene.nodes);
        kit::Button(b, kBedLeaveId, static_cast<float>(guiW / 2 - 100), static_cast<float>(guiH - 40), 200,
                    kit::Plain(label));
        return doc;
    }
}

namespace perlin::screen {
    Claim BedScreen::Update(Frame& frame) {
        UpdateBed(frame.hud, frame.live, frame.fbW, frame.fbH);
        return Claim::None;
    }

    void BedScreen::UpdateBed(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool asleep = live && live->Sleeping() && !host.screens.IsOpen(Id::Death) && !host.screens.IsOpen(Id::Pause) && !host.screens.IsOpen(Id::Inventory) &&
                            !host.screens.IsOpen(Id::Form) && !host.chat.IsOpen();
        if (!asleep) {
            if (IsOpen())
                CloseOverlay(host, hud, *this);
            return;
        }
        if (!IsOpen()) {
            host.screens.Open(*this);
            leftDown = host.input.IsMouseDown(0);
            host.input.SetTextMode(true);
        }
        const ui::GuiViewport gui = ui::OverlayViewport(hud, fbW, fbH);
        std::string label = i18n::Tr("multiplayer.stopSleeping");
        if (label.empty() || label == "multiplayer.stopSleeping")
            label = "Leave Bed";
        const std::string signature = str::Key(label, str::Cat(gui.w, "x", gui.h));
        if (signature != this->signature || !hud.Overlay().HasOverlay()) {
            this->signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildBedScreen(label, gui.w, gui.h), error))
                Log("ui", "bed screen: {}", error);
        }
        hud.Overlay().MouseMove(gui.ToGui(host.input.MouseX()), gui.ToGui(host.input.MouseY()));
        hud.UpdateOverlay(fbW, fbH, host.options.guiScale);
        const bool down = host.input.IsMouseDown(0);

        const bool escaped = EscapeTyped(host.inputManager.TakeTyped());
        bool leave = escaped || host.inputManager.Pressed(Action::Pause);
        if (!down && leftDown && hud.Overlay().OverlayHover() == ui::kBedLeaveId)
            leave = true;
        leftDown = down;
        if (leave)
            live->SendStopSleeping();
    }
}
