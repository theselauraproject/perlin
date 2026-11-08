#include "client/PerlinInternal.hpp"
#include "util/Log.hpp"

namespace perlin {
    namespace {

        std::string FormDumpName(const std::string& title) {
            std::string name;
            for (const char c : ui::StripFormatting(title))
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
                    name += c;
                else if (!name.empty() && name.back() != '_')
                    name += '_';
            if (name.size() > 60)
                name.resize(60);
            return name;
        }
    }

    void Perlin::RegisterScreens() {

        screens.Add({"title", [this](ScreenFrame& f) { return UpdateTitleScreen(f.hud, f.fbW, f.fbH); },
                     [this] { return titleScreen.shown; }});
        screens.Add({"disconnect",
                     [this](ScreenFrame& f) { return UpdateDisconnectScreen(f.hud, f.fbW, f.fbH); },
                     [this] { return disconnect.shown; }});
        screens.Add({"loading", [this](ScreenFrame& f) { return UpdateLoadingScreen(f.hud, f.fbW, f.fbH); },
                     [this] { return loading.shown; }});
        screens.Add({"replay",
                     [this](ScreenFrame& f) {
                         if (!replayCtl.Playing() || !f.live || !f.live->IsReplay())
                             return false;
                         if (exportJob.active)
                             ExportOverlay(f.hud, f.fbW, f.fbH);
                         else
                             UpdateReplayScreen(f.hud, f.live, f.fbW, f.fbH);
                         return true;
                     },
                     [this] { return replayCtl.Playing(); }, false});
        screens.Add({"credits",
                     [this](ScreenFrame& f) {
                         UpdateCreditsScreen(f.hud, f.live, f.fbW, f.fbH);
                         return credits.shown;
                     },
                     [this] { return credits.shown; }});

        const auto inGame = [](auto update) {
            return [update](ScreenFrame& f) {
                update(f);
                return false;
            };
        };
        screens.Add({"death", inGame([this](ScreenFrame& f) {
                         UpdateDeathScreen(f.hud, f.live, hudState, f.fbW, f.fbH);
                     }),
                     [this] { return death.shown; }});
        screens.Add({"form",
                     inGame([this](ScreenFrame& f) { UpdateFormScreen(f.hud, f.live, f.fbW, f.fbH); }),
                     [this] { return form.shown; }});
        screens.Add({"inventory", inGame([this](ScreenFrame& f) {
                         UpdateInventoryScreen(f.hud, f.live, hudState, f.fbW, f.fbH);
                     }),
                     [this] { return inventory.shown; }});
        screens.Add({"pause",
                     inGame([this](ScreenFrame& f) { UpdatePauseScreen(f.hud, f.live, f.fbW, f.fbH); }),
                     [this] { return pause.shown; }});
        screens.Add({"bed", inGame([this](ScreenFrame& f) { UpdateBedScreen(f.hud, f.live, f.fbW, f.fbH); }),
                     [this] { return bed.shown; }, false});
        screens.Add({"chat",
                     inGame([this](ScreenFrame& f) { UpdateChatClicks(f.hud, f.live, f.fbW, f.fbH); }),
                     [this] { return chat.IsOpen(); }, false});
    }

    void Perlin::CloseOverlay(ui::Hud& hud, OverlayState& screen) {
        hud.Overlay().ClearOverlay();
        screen.shown = false;
        screen.leftDown = false;
        screen.signature.clear();
        if (!chat.IsOpen())
            input.SetTextMode(false);
    }

    void Perlin::UpdateServerLayer(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const auto hide = [&] {
            if (!serverLayer.shown)
                return;
            hud.Overlay().ClearServerLayer();
            serverLayer.shown = false;
            serverLayer.signature.clear();
        };
        if (!live || !options.showHud || !hudState.visible || death.shown || WorldCovered())
            return hide();

        const auto now = Clock::now();
        if (!toast.text.empty() && now - toast.at > std::chrono::seconds(4))
            toast.text.clear();
        ui::ServerLayerData data;
        if (toast.text.empty()) {
            BedrockSession::ServerToast t;
            if (live->TakeToast(t) && !DispatchModToast(t.title, t.message)) {
                toast.text = t.title + "\n" + t.message;
                toast.at = now;
                toast.title = t.title;
                toast.message = t.message;
            }
        }
        if (!toast.text.empty()) {
            data.toast = true;
            data.toastTitle = toast.title;
            data.toastMessage = toast.message;
        }
        if (!(hud.Ready() && hud.Overrides("hud")))
            for (const auto& b : live->BossBars())
                data.bars.push_back({b.title, b.progress, b.color, b.overlay});

        if (data.bars.size() > 8)
            data.bars.resize(8);
        const auto sb = live->Scoreboard();
        if (sb.visible && !pause.shown && !(hud.Ready() && hud.Overrides("scoreboard"))) {
            data.scoreboard = true;
            data.scoreboardTitle = sb.title;
            for (const auto& r : sb.rows)
                data.rows.emplace_back(r.name, r.score);
        }
        if (data.Empty())
            return hide();

        const int w = static_cast<int>(hud.GuiWidth()), h = static_cast<int>(hud.GuiHeight());
        std::string signature = ui::ServerLayerSignature(data, w, h);
        if (serverLayer.shown && signature == serverLayer.signature)
            return;
        serverLayer.signature = std::move(signature);
        std::string error;
        serverLayer.shown = hud.Overlay().SetServerLayer(ui::BuildServerLayerRml(data, w, h), error);
        if (!serverLayer.shown)
            Log("ui", "{}", error);
    }

    void Perlin::UpdateFormScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const auto now = Clock::now();

        const char* previewForm = live ? nullptr : PERLIN_ENV("PERLIN_PREVIEW_FORM");
        if (!live && !previewForm) {
            if (form.shown)
                CloseOverlay(hud, form);
            return;
        }
        const auto respond = [&](const std::string* response) {
            if (live)
                live->SendFormResponse(form.model.id, response, false);
            else
                Log("form", "preview answer: {}", (response ? *response : std::string("closed")));
        };
        const auto respondAndClose = [&](const std::string* response) {
            respond(response);
            CloseOverlay(hud, form);
        };
        if (now - form.lastWarm > std::chrono::seconds(2)) {
            form.lastWarm = now;
            form.ui.Warm(ServerPacks::Instance().Directories());
        }
        if (live && live->TakeFormClosed() && form.shown)
            CloseOverlay(hud, form);
        if (form.shown && !form.engine && form.ui.Ready() && !form.engineFailed)
            form.engine = true;

        if (!form.shown) {
            if (death.shown || inventory.shown || pause.shown || chat.IsOpen())
                return;
            BedrockSession::ServerForm sf;
            if (live && !live->TakeServerForm(sf))
                return;
            if (!live) {
                if (form.previewOpened)
                    return;
                form.previewOpened = true;
                sf.id = 1;
                sf.json = file::Read(previewForm);
            }
            if (!ui::ParseForm(sf.id, sf.json, form.model)) {
                Log("form", "cannot parse form {}", sf.id);
                if (live)
                    live->SendFormResponse(sf.id, nullptr, false);
                return;
            }

            if (!live)
                if (const char* clicks = PERLIN_ENV("PERLIN_PREVIEW_FORM_CLICKS")) {
                    std::stringstream list(clicks);
                    std::string ignored;

                    for (std::string id; std::getline(list, id, ',');)
                        if (!ui::FormDismissDropdown(form.model, id == "-" ? std::string() : id))
                            ui::FormClick(form.model, id, ignored);
                }
            Log("form", "opened form {} '{}'", sf.id, form.model.title);
            detail::DumpGenerated("last_form.json", sf.json);
            if (const std::string name = FormDumpName(form.model.title); !name.empty())
                detail::DumpGenerated("forms/" + name + ".json", sf.json);
            form.ui.Warm(ServerPacks::Instance().Directories());
            form.ui.WaitReady(150);

            form.engineFailed = false;
            form.engine = form.ui.Ready();
            form.shown = true;
            form.scroll = 0.0f;
            form.contentH = form.viewH = 0.0f;
            form.signature.clear();
            form.openedAt = now;
            form.leftDown = true;
            input.SetTextMode(true);
            CenterCursorForGui();
        }

        bool cancel = false;
        for (const auto& t : inputManager.TakeTyped()) {
            if (detail::IsEscape(t))
                cancel = true;
            else
                ui::FormEdit(form.model, t);
        }
        form.scroll = std::max(0.0f, form.scroll - inputManager.Scroll() * 20.0f);
        if (cancel)
            return respondAndClose(nullptr);

        if (!form.engine && form.ui.Building() && now - form.openedAt < std::chrono::seconds(3))
            return;

        const detail::GuiViewport gui = detail::OverlayViewport(hud, fbW, fbH);
        form.scroll = std::clamp(form.scroll, 0.0f, std::max(0.0f, form.contentH - form.viewH));
        const std::string signature =
            str::Key(form.model.id, form.model.focus, form.model.openDropdown,
                     ui::FormCustomResponse(form.model), ui::FormImageVersion(), str::Cat(gui.w, "x", gui.h),
                     static_cast<int>(form.scroll), form.engine ? "e" : "b");

        if (form.engine && form.ui.Animating() && now - form.lastAnim >= std::chrono::milliseconds(80)) {
            form.signature.clear();
            form.lastAnim = now;
        }
        if (signature != form.signature || !hud.Overlay().HasOverlay()) {
            form.signature = signature;
            std::string rml, error;
            float contentH = 0.0f, viewH = 0.0f;
            if (form.engine && !form.ui.Build(form.model, gui.w, gui.h, form.scroll, rml, contentH, viewH)) {
                Log("form", "json ui build failed: {}, using the built-in layout", form.ui.Problem());
                form.engine = false;
                form.engineFailed = true;
            }
            if (!form.engine)
                rml = ui::BuildFormRml(form.model, gui.w, gui.h, form.scroll, contentH, viewH);
            form.contentH = contentH;
            form.viewH = viewH;

            detail::DumpGenerated("last_form.rml", rml);
            if (!hud.Overlay().SetOverlay(rml, error)) {
                Log("form", "{}", error);
                if (!form.engine)
                    return respondAndClose(nullptr);
                form.engine = false;
                form.engineFailed = true;
                form.signature.clear();
                return;
            }
        }
        hud.Overlay().MouseMove(gui.ToGui(input.MouseX()), gui.ToGui(input.MouseY()));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down) {
            form.dragSlider = -1;
        } else {
            const int hoverSlider = ui::FormSliderAt(hud.Overlay().OverlayHover());
            if (form.dragSlider < 0 && hoverSlider >= 0 && !form.leftDown)
                form.dragSlider = hoverSlider;
            if (form.dragSlider >= 0)
                ui::FormDrag(form.model, form.dragSlider, gui.ToGui(input.MouseX()));
        }
        if (!down && form.leftDown && now - form.openedAt > std::chrono::milliseconds(200)) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kFormCloseId)
                return respondAndClose(nullptr);
            if (ui::FormDismissDropdown(form.model, hover)) {
                form.leftDown = down;
                return;
            }
            std::string response;
            if (!hover.empty() && ui::FormClick(form.model, hover, response))
                return respondAndClose(&response);

            if (!hover.empty() && !hover.starts_with("i:"))
                form.model.focus = -1;
        }
        form.leftDown = down;
    }

    void Perlin::UpdateBedScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool asleep = live && live->Sleeping() && !death.shown && !pause.shown && !inventory.shown &&
                            !form.shown && !chat.IsOpen();
        if (!asleep) {
            if (bed.shown)
                CloseOverlay(hud, bed);
            return;
        }
        if (!bed.shown) {
            bed.shown = true;
            bed.leftDown = input.IsMouseDown(0);
            input.SetTextMode(true);
        }
        const detail::GuiViewport gui = detail::OverlayViewport(hud, fbW, fbH);
        std::string label = i18n::Tr("multiplayer.stopSleeping");
        if (label.empty() || label == "multiplayer.stopSleeping")
            label = "Leave Bed";
        const std::string signature = str::Key(label, str::Cat(gui.w, "x", gui.h));
        if (signature != bed.signature || !hud.Overlay().HasOverlay()) {
            bed.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildBedScreenRml(label, gui.w, gui.h), error))
                Log("ui", "bed screen: {}", error);
        }
        hud.Overlay().MouseMove(gui.ToGui(input.MouseX()), gui.ToGui(input.MouseY()));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool down = input.IsMouseDown(0);

        const bool escaped = detail::EscapeTyped(inputManager.TakeTyped());
        bool leave = escaped || inputManager.Pressed(Action::Pause);
        if (!down && bed.leftDown && hud.Overlay().OverlayHover() == ui::kBedLeaveId)
            leave = true;
        bed.leftDown = down;
        if (leave)
            live->SendStopSleeping();
    }

    void Perlin::UpdateCreditsScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {

        const bool preview = !live && PERLIN_ENV("PERLIN_PREVIEW_CREDITS") != nullptr;
        if (!live && !preview) {
            credits.shown = false;
            return;
        }
        if ((preview || live->TakeCreditsRequest()) && !credits.shown) {
            credits.shown = true;
            credits.scroll = 0;
            credits.last = Clock::now();
            input.SetTextMode(true);
        }
        if (!credits.shown)
            return;
        const detail::GuiViewport gui = detail::OverlayViewport(hud, fbW, fbH);
        const auto now = Clock::now();

        credits.scroll += detail::SecondsSince(credits.last, now) * 20.0;
        credits.last = now;

        const std::string signature = str::Key(static_cast<int>(credits.scroll), str::Cat(gui.w, "x", gui.h));
        if (signature != credits.signature || !hud.Overlay().HasOverlay()) {
            credits.signature = signature;
            std::string error;
            const std::string& player = live ? live->PlayerName() : options.playerName;
            if (!hud.Overlay().SetOverlay(
                    ui::BuildCreditsScreenRml(gui.w, gui.h, static_cast<int>(credits.scroll), player), error))
                Log("ui", "credits screen: {}", error);
        }
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool escaped = detail::EscapeTyped(inputManager.TakeTyped());
        const bool done = escaped || credits.scroll > gui.h + ui::CreditsHeight();
        if (!done)
            return;
        credits.shown = false;
        hud.Overlay().ClearOverlay();
        if (!chat.IsOpen())
            input.SetTextMode(false);
        if (live)
            live->SendCreditsFinished();
    }

    void Perlin::UpdateDeathScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW,
                                   int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DEATH") != nullptr;
        bool dead = preview || (live && state.mode != ui::GameMode::Spectator &&
                                (state.health <= 0.0f || live->DeathPending()));
        if (live && live->DeathPending() && state.health > 0.0f && death.shown &&
            Clock::now() - death.openedAt > std::chrono::seconds(2))
            live->ClearDeath();
        const auto now = Clock::now();

        if (dead && death.respawnSent) {
            if (now - death.sentAt < std::chrono::seconds(3))
                dead = false;
            else
                death.respawnSent = false;
        } else if (!dead) {
            death.respawnSent = false;
        }
        if (!dead) {
            if (death.shown) {
                hud.Overlay().ClearOverlay();
                death.shown = false;
                death.signature.clear();
                platform->SetCursorCaptured(true);
            }
            if (death.textMode) {
                death.textMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            death.leftDown = false;
            return;
        }
        if (!death.shown) {
            death.shown = true;
            death.openedAt = now;
            platform->SetCursorCaptured(false);
            Log("ui", "death screen shown: health={} pending={} cause='{}' hud={}", state.health,
                (live && live->DeathPending()), (live ? live->DeathCause() : std::string()), state.visible);
        }
        if (!death.textMode) {
            death.textMode = true;
            input.SetTextMode(true);
        }

        const bool buttons = now - death.openedAt >= std::chrono::seconds(1);
        ui::DeathScreenText t;
        t.title = i18n::Tr("perlin.death.title");
        t.cause = live ? live->DeathCause() : std::string();
        t.respawn = i18n::Tr("perlin.death.respawn");
        t.titleScreen = i18n::Tr("perlin.death.title_screen");
        t.buttonsEnabled = buttons;

        if (death.tmpl.Refresh("death_screen.rml", ui::DefaultDeathScreenTemplate(),
                               std::chrono::milliseconds(400), now, "death screen"))
            death.signature.clear();

        const detail::GuiViewport gui = detail::OverlayViewport(hud, fbW, fbH);
        const std::string signature =
            str::Key(t.title, t.cause, str::Cat(gui.w, "x", gui.h), buttons, t.respawn + t.titleScreen);
        if (signature != death.signature || !hud.Overlay().HasOverlay()) {
            death.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDeathScreenRml(death.tmpl.text, t, gui.w, gui.h), error)) {
                Log("ui", "death_screen.rml: {}", error);
                death.tmpl.text = ui::DefaultDeathScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildDeathScreenRml(death.tmpl.text, t, gui.w, gui.h), error);
            }
        }
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        hud.Overlay().MouseMove(gui.ToGui(pointer.x), gui.ToGui(pointer.y));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (PERLIN_ENV("PERLIN_PREVIEW_MOUSE") && !death.hoverLogged) {
            death.hoverLogged = true;
            Log("ui", "death screen hover: '{}'", hud.Overlay().OverlayHover());
        }
        if (!down && death.leftDown && buttons && !preview) {
            const std::string hover = hud.Overlay().OverlayHover();
            Log("ui", "death screen click on '{}'", hover);
            if (hover == ui::kDeathRespawnId && live) {
                live->RequestRespawn();
                death.respawnSent = true;
                death.sentAt = now;
            }
        }
        death.leftDown = down;
    }

    void Perlin::UpdateChatClicks(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        if (!chat.IsOpen() || (!live && !PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))) {
            chatLeftDown = false;
            return;
        }
        const detail::GuiViewport gui = detail::OverlayViewport(hud, fbW, fbH);
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        hud.Overlay().MouseMove(gui.ToGui(pointer.x), gui.ToGui(pointer.y));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool down = input.IsMouseDown(0);
        if (down && !chatLeftDown) {
            const std::string link = hud.Overlay().HoverLink();
            if (link.starts_with("link:url:")) {
                const std::string url = link.substr(9);
                if (url.starts_with("https://") && platform)
                    platform->OpenUrl(url);
                else if (url.starts_with("http://") && platform)
                    platform->OpenUrl("https://" + url.substr(7));
            } else if (link.size() > 6 && link[5] == '/' && live) {
                live->SendCommand(link.substr(6));
                chat.Close();
                input.SetTextMode(false);
            }
        }
        chatLeftDown = down;
    }
}
