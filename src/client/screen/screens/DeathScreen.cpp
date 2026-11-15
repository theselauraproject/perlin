#include "client/ui/GuiViewport.hpp"
#include "client/screen/screens/DeathScreen.hpp"
#include "client/screen/screens/ScreenSupport.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/systems/model/HudState.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/Hud.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"
#include "util/Strings.hpp"

#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/SceneEmit.hpp"
#include "client/ui/SceneKit.hpp"
#include "util/Resources.hpp"

#include <filesystem>
#include <iostream>

namespace perlin::ui {
    namespace fs = std::filesystem;

    SceneDoc BuildDeathScreen(const DeathScreenText& t, int guiW, int guiH) {
        SceneDoc doc;
        doc.scene.theme = Theme::Death;
        doc.scene.sprites = true;
        SceneBuilder b(doc.scene.nodes);
        const float w = static_cast<float>(guiW), h = static_cast<float>(guiH);
        kit::Gradient(b, 0, 0, w, h, "vertical-gradient(#50000060 #803030a0)", "1");
        kit::Label(b, 0, 60, w, "ctr big nowrap", ParseFormatRuns(t.title, "#ffffff"), "3", "heading");
        kit::Label(b, 0, 85, w, "ctr nowrap", ParseFormatRuns(t.cause, "#ffffff"), "3", "cause");
        const float bx = static_cast<float>(guiW / 2 - 100);
        kit::Button(b, kDeathRespawnId, bx, static_cast<float>(guiH / 4 + 72), 200, kit::Plain(t.respawn),
                    !t.buttonsEnabled);
        kit::Button(b, kDeathTitleId, bx, static_cast<float>(guiH / 4 + 96), 200, kit::Plain(t.titleScreen),
                    !t.buttonsEnabled);
        return doc;
    }
}

namespace perlin::screen {
    Claim DeathScreen::Update(Frame& frame) {
        UpdateDeath(frame.hud, frame.live, frame.fbW, frame.fbH);
        return Claim::None;
    }

    void DeathScreen::UpdateDeath(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DEATH") != nullptr;
        bool dead = preview || (live && host.hudState.mode != ui::GameMode::Spectator &&
                                (host.hudState.health <= 0.0f || live->DeathPending()));
        if (live && live->DeathPending() && host.hudState.health > 0.0f && IsOpen() &&
            std::chrono::steady_clock::now() - openedAt > std::chrono::seconds(2))
            live->ClearDeath();
        const auto now = std::chrono::steady_clock::now();

        if (dead && respawnSent) {
            if (now - sentAt < std::chrono::seconds(3))
                dead = false;
            else
                respawnSent = false;
        } else if (!dead) {
            respawnSent = false;
        }
        if (!dead) {
            if (IsOpen()) {
                hud.Overlay().ClearOverlay();
                host.screens.Close(*this);
                signature.clear();
                host.captureCursor(true);
            }
            if (textMode) {
                textMode = false;
                if (!host.chat.IsOpen())
                    host.input.SetTextMode(false);
            }
            leftDown = false;
            return;
        }
        if (!IsOpen()) {
            host.screens.Open(*this);
            openedAt = now;
            host.captureCursor(false);
            Log("ui", "death screen shown: health={} pending={} cause='{}' hud={}", host.hudState.health,
                (live && live->DeathPending()), (live ? live->DeathCause() : std::string()), host.hudState.visible);
        }
        if (!textMode) {
            textMode = true;
            host.input.SetTextMode(true);
        }

        const bool buttons = now - openedAt >= std::chrono::seconds(1);
        ui::DeathScreenText t;
        t.title = i18n::Tr("perlin.death.title");
        t.cause = live ? live->DeathCause() : std::string();
        t.respawn = i18n::Tr("perlin.death.respawn");
        t.titleScreen = i18n::Tr("perlin.death.title_screen");
        t.buttonsEnabled = buttons;

        const ui::GuiViewport gui = ui::OverlayViewport(hud, fbW, fbH);
        const std::string signature =
            str::Key(t.title, t.cause, str::Cat(gui.w, "x", gui.h), buttons, t.respawn + t.titleScreen);
        if (signature != this->signature || !hud.Overlay().HasOverlay()) {
            this->signature = signature;
            std::string error;
            hud.Overlay().SetOverlay(ui::BuildDeathScreen(t, gui.w, gui.h), error);
        }
        const glm::vec2 pointer = PointerOrPreview(host.input);
        hud.Overlay().MouseMove(gui.ToGui(pointer.x), gui.ToGui(pointer.y));
        hud.UpdateOverlay(fbW, fbH, host.options.guiScale);

        const bool down = host.input.IsMouseDown(0);
        if (PERLIN_ENV("PERLIN_PREVIEW_MOUSE") && !hoverLogged) {
            hoverLogged = true;
            Log("ui", "death screen hover: '{}'", hud.Overlay().OverlayHover());
        }
        if (!down && leftDown && buttons && !preview) {
            const std::string hover = hud.Overlay().OverlayHover();
            Log("ui", "death screen click on '{}'", hover);
            if (hover == ui::kDeathRespawnId && live) {
                live->RequestRespawn();
                respawnSent = true;
                sentAt = now;
            }
        }
        leftDown = down;
    }
}
