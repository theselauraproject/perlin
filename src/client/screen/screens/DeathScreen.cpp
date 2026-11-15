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
#include "client/ui/UiTemplate.hpp"
#include "util/Resources.hpp"

#include <filesystem>
#include <iostream>

namespace perlin::ui {
    namespace fs = std::filesystem;

    std::string DefaultDeathScreenTemplate() {
        return ui::ReadMarkup("ui/screens/death_screen.rml");
    }

    std::string LoadDeathScreenTemplate(const std::string& dataDir, uint64_t& version) {
        return LoadUiTemplate(dataDir, "ui/screens/death_screen.rml", DefaultDeathScreenTemplate(), version);
    }

    SceneDoc BuildDeathScreen(const std::string& templateText, const DeathScreenText& t, int guiW, int guiH) {
        SceneDoc doc;
        doc.rml = templateText;
        auto& vars = doc.scene.vars;
        vars["w"] = std::to_string(guiW);
        vars["h"] = std::to_string(guiH);
        vars["btn_x"] = std::to_string(guiW / 2 - 100);
        vars["btn1_y"] = std::to_string(guiH / 4 + 72);
        vars["btn2_y"] = std::to_string(guiH / 4 + 96);
        vars["respawn"] = t.respawn;
        vars["title_screen"] = t.titleScreen;
        doc.scene.flags["off"] = !t.buttonsEnabled;
        doc.scene.texts["title_runs"] = ParseFormatRuns(t.title, "#ffffff");
        doc.scene.texts["cause_runs"] = ParseFormatRuns(t.cause, "#ffffff");
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

        if (tmpl.Refresh("ui/screens/death_screen.rml", ui::DefaultDeathScreenTemplate(),
                               std::chrono::milliseconds(400), now, "death screen"))
            signature.clear();

        const ui::GuiViewport gui = ui::OverlayViewport(hud, fbW, fbH);
        const std::string signature =
            str::Key(t.title, t.cause, str::Cat(gui.w, "x", gui.h), buttons, t.respawn + t.titleScreen);
        if (signature != this->signature || !hud.Overlay().HasOverlay()) {
            this->signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDeathScreen(tmpl.text, t, gui.w, gui.h), error)) {
                Log("ui", "death_screen.rml: {}", error);
                tmpl.text = ui::DefaultDeathScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildDeathScreen(tmpl.text, t, gui.w, gui.h), error);
            }
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
