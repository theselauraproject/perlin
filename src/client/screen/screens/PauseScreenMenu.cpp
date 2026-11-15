#include "client/ui/GuiViewport.hpp"
#include "client/screen/screens/ScreenInternal.hpp"
#include "client/screen/screens/PauseFrame.hpp"
#include "client/screen/screens/WardrobeController.hpp"
#include <algorithm>
#include "util/Log.hpp"

namespace perlin::screen {
    Claim PauseScreen::Update(Frame& frame) {
        UpdatePauseScreen(frame.hud, frame.live, frame.fbW, frame.fbH);
        return Claim::None;
    }


    void PauseScreen::UpdatePauseScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_PAUSE") != nullptr;
        const auto now = Clock::now();
        const bool canOpen = (live || preview) && !host.screens.IsOpen(Id::Death) && !host.screens.IsOpen(Id::Form) && !host.screens.IsOpen(Id::Inventory) &&
                             !host.chat.IsOpen() && !host.screens.IsOpen(Id::Bed);
        if (services.modRequest.openView >= 0 && canOpen) {
            if (!this->IsOpen()) {
                host.screens.Open(*this);
                this->openedAt = now;
                this->leftDown = true;
                host.inputManager.TakeTyped();
                host.input.SetTextMode(true);
                screen::CenterCursorForGui(host);
            }
            this->view = services.modRequest.openView;
            optionsUi.page = services.modRequest.openPage;
            optionsUi.sub = 0;
            if (this->view == 5)
                this->wardrobe.init = false;
            if (optionsUi.page == 11 && !services.modRequest.settingsFor.empty())
                packUi.modSelected = services.modRequest.settingsFor;
            services.modRequest.openView = -1;
            services.modRequest.openPage = 0;
            this->signature.clear();
            optionsUi.signature.clear();
            hud.Overlay().ClearOverlay();
        }
        if (!this->IsOpen() && services.title.options && services.title.active) {
            host.screens.Open(*this);
            this->view = services.title.openView;
            services.title.openView = 1;
            if (this->view == 5)
                this->wardrobe.init = false;
            optionsUi.page = 0;
            optionsUi.sub = 0;
            this->openedAt = now;
            this->signature.clear();
            optionsUi.signature.clear();
            this->leftDown = true;
            host.inputManager.TakeTyped();
            host.input.SetTextMode(true);
        } else if (!this->IsOpen() && canOpen &&
                   (host.inputManager.Pressed(Action::Pause) || (preview && !this->previewOpened))) {
            this->previewOpened = true;
            host.screens.Open(*this);
            this->view = PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")
                             ? (PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x77   ? 3
                                : PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x6d ? 4
                                : PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x64 ? 5
                                                                                  : 1)
                             : 0;
            optionsUi.page =
                PERLIN_ENV("PERLIN_PREVIEW_OPTIONS") ? std::atoi(PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")) : 0;
            this->openedAt = now;
            this->signature.clear();
            host.inputManager.TakeTyped();
            host.input.SetTextMode(true);
            screen::CenterCursorForGui(host);
        } else if (!this->IsOpen() && !live && !preview && host.inputManager.Pressed(Action::Pause)) {
            host.captureCursor(false);
        }
        if (this->view != 5 || !this->IsOpen())
            this->wardrobe.init = false;
        if (!this->IsOpen())
            return;
        bool resume = host.screens.IsOpen(Id::Death) || host.screens.IsOpen(Id::Inventory) || host.chat.IsOpen();
        if (optionsUi.bindCapture < 0)
            for (const auto& t : host.inputManager.TakeTyped()) {
                if (this->view == 4) {
                    ui::TextEdit::Focused().Apply(t, packUi.modSearch, ui::TextLimits{.maxBytes = 40});
                }
                if (!t.character && t.key == Key::Escape &&
                    now - this->openedAt > std::chrono::milliseconds(100)) {
                    if (this->view == 1 && optionsUi.page == 11) {
                        services.manager.SettingsClosed(services.modRequest.settingsFor);
                        this->view = 4;
                        optionsUi.page = 0;
                        optionsUi.sub = 0;
                        optionsUi.signature.clear();
                        hud.Overlay().ClearOverlay();
                    } else if (this->view == 1 && optionsUi.page != 0) {
                        services.commitTexturePacks();
                        services.options.Save();
                        packUi.shaderPendingInit = false;
                        optionsUi.page = optionsUi.page == 5 ? 3 : optionsUi.page == 10 ? 8 : 0;
                        optionsUi.sub = 0;
                        optionsUi.kbScroll = 0;
                        optionsUi.signature.clear();
                        hud.Overlay().ClearOverlay();
                    } else if (this->view != 0) {
                        services.options.Save();
                        this->view = 0;
                        optionsUi.signature.clear();
                        this->signature.clear();
                        hud.Overlay().ClearOverlay();
                    } else {
                        resume = true;
                    }
                }
            }
        if ((services.title.options || services.replay.Playing()) && this->IsOpen() && this->view == 0)
            resume = true;
        if (resume) {
            if (services.title.options) {
                services.title.options = false;
                services.title.signature.clear();
            }
            this->view = 0;
            hud.Overlay().ClearOverlay();
            host.screens.Close(*this);
            if (!host.chat.IsOpen() && !host.screens.IsOpen(Id::Death) && !host.screens.IsOpen(Id::Inventory))
                host.input.SetTextMode(false);
            return;
        }

        if (this->tmpl.Refresh("ui/screens/pause_screen.rml", ui::DefaultPauseScreenTemplate(),
                               std::chrono::milliseconds(400), now, "pause screen"))
            this->signature.clear();
        const auto [scale, guiW, guiH] = ui::OverlayViewport(hud, fbW, fbH);
        const glm::vec2 pointer = screen::PointerOrPreview(host.input);
        float mx = pointer.x, my = pointer.y;
        bool down = host.input.IsMouseDown(0);
        if (const char* spec = PERLIN_ENV("PERLIN_PREVIEW_CLICK")) {
            const double elapsed = std::chrono::duration<double>(now - this->openedAt).count();
            std::string rest = spec;
            size_t pos = 0;
            while (pos < rest.size()) {
                size_t end = rest.find(',', pos);
                if (end == std::string::npos)
                    end = rest.size();
                double at = 0;
                float cx = 0, cy = 0;
                if (std::sscanf(rest.substr(pos, end - pos).c_str(), "%lf:%f:%f", &at, &cx, &cy) == 3 &&
                    elapsed >= at && elapsed < at + 0.25) {
                    mx = cx;
                    my = cy;
                    down = true;
                }
                pos = end + 1;
            }
        }
        const bool clickReady = now - this->openedAt > std::chrono::milliseconds(150);
        PauseFrame f{hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady};
        if (this->view == 1 && optionsUi.page == 5)
            return UpdateKeybindPage(f);
        if (this->view == 5)
            return UpdateWardrobe(f);
        if ((this->view == 1 && (optionsUi.page == 6 || optionsUi.page == 7 || optionsUi.page == 8)) ||
            this->view == 4)
            return UpdatePackMenu(f);
        if (this->view == 1 || this->view == 3 || this->view == 4)
            return UpdateOptionsMenu(f);
        UpdatePauseMenu(f);
    }

    void PauseScreen::UpdatePauseMenu(PauseFrame& f) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        if (services.title.options) {
            services.title.options = false;
            services.title.signature.clear();
            this->view = 0;
            host.screens.Close(*this);
            hud.Overlay().ClearOverlay();
            return;
        }

        ui::PauseScreenText t;
        t.title = i18n::Tr("menu.game");
        t.back = i18n::Tr("menu.returnToGame");
        t.options = i18n::Tr("menu.options");
        t.world = i18n::TrOr("perlin.worldOptions", "World Options");
        t.mods = i18n::TrOr("perlin.mods", "Mods");
        t.quit = i18n::Tr("menu.disconnect");
        t.advancements = i18n::TrOr("gui.advancements", "Advancements");
        t.stats = i18n::TrOr("gui.stats", "Statistics");
        t.feedback = i18n::TrOr("menu.sendFeedback", "Give Feedback");
        t.bugs = i18n::TrOr("menu.reportBugs", "Report Bugs");
        t.lan = i18n::TrOr("menu.shareToLan", "Open to LAN");
        t.players = i18n::TrOr("gui.playerList", "Player List");
        t.showReplay = live != nullptr && !preview && !live->IsReplay();
        t.replayRecording = services.replay.Recording();
        t.replayStart = i18n::TrOr("perlin.replay.start", "Start Replay");
        t.replayStop = i18n::TrOr("perlin.replay.stop", "Stop Replay");
        t.showPanel = this->players && live;
        if (t.showPanel) {
            t.panelTitle = t.players;
            t.roleOperator = i18n::TrOr("permissions.level.operator", "Operator");
            t.roleMember = i18n::TrOr("permissions.level.member", "Member");
            t.roleVisitor = i18n::TrOr("permissions.level.visitor", "Visitor");
            auto list = live->PlayerList();
            std::ranges::stable_sort(
                list, [](const BedrockSession::TabPlayer& a, const BedrockSession::TabPlayer& b) {
                    if (a.perm != b.perm)
                        return a.perm > b.perm;
                    return a.name < b.name;
                });
            for (const BedrockSession::TabPlayer& p : list)
                t.panel.push_back({p.name, p.perm});
        }
        std::string panelSig;
        for (const auto& r : t.panel)
            panelSig += r.name + ":" + std::to_string(r.perm) + ",";
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) +
                                      (t.showPanel ? "P" : "p") + panelSig +
                                      (t.showReplay ? (t.replayRecording ? "R" : "r") : "") + t.replayStart +
                                      t.replayStop + t.title + t.back + t.options + t.quit + t.advancements +
                                      t.stats + t.feedback + t.bugs + t.lan + t.players + t.world + t.mods;
        if (signature != this->signature || !hud.Overlay().HasOverlay()) {
            this->signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildPauseScreen(this->tmpl.text, t, guiW, guiH), error)) {
                Log("ui", "pause_screen.rml: {}", error);
                this->tmpl.text = ui::DefaultPauseScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildPauseScreen(this->tmpl.text, t, guiW, guiH), error);
            }
        }
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, services.options.guiScale);
        if (!down && this->leftDown && clickReady) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kPauseResumeId) {
                hud.Overlay().ClearOverlay();
                host.screens.Close(*this);
                host.input.SetTextMode(false);
            } else if (hover == ui::kPauseReplayStartId && live && !services.replay.Recording()) {
                services.toggleReplayRecording(true);
                this->signature.clear();
            } else if (hover == ui::kPauseReplayStopId && live && services.replay.Recording()) {
                services.toggleReplayRecording(false);
                this->signature.clear();
            } else if (hover == ui::kPauseQuitId && !preview) {
                services.leaveToServerList(hud);
            } else if (hover == ui::kPauseFeedbackId) {
                services.openUrl("https://github.com/theselauraproject/perlin/issues");
            } else if (hover == ui::kPausePlayersId) {
                this->players = !this->players;
            } else if (hover == ui::kPauseBugsId) {
                services.openUrl("https://github.com/theselauraproject/perlin/issues/new?labels=bug");
            } else if (hover == ui::kPauseWardrobeId) {
                this->view = 5;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseModsId) {
                this->view = 4;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseWorldId) {
                this->view = 3;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseOptionsId) {
                this->view = 1;
                optionsUi.page = 0;
                optionsUi.sub = 0;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            }
        }
        this->leftDown = down;
    }

    void PauseScreen::UpdateKeybindPage(PauseFrame& f) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        struct Cat {
            const char* name;
            std::vector<std::pair<Action, const char*>> items;
        };
        static const Cat kCats[] = {{"Movement",
                                     {{Action::Jump, "Jump"},
                                      {Action::Sneak, "Sneak"},
                                      {Action::Sprint, "Sprint"},
                                      {Action::Left, "Strafe Left"},
                                      {Action::Right, "Strafe Right"},
                                      {Action::Back, "Walk Backwards"},
                                      {Action::Forward, "Walk Forwards"}}},
                                    {"Gameplay",
                                     {{Action::Attack, "Attack/Destroy"},
                                      {Action::PickBlock, "Pick Block"},
                                      {Action::Use, "Use Item/Place Block"}}},
                                    {"Inventory",
                                     {{Action::Drop, "Drop Selected Item"},
                                      {Action::SwapOffhand, "Swap Item With Offhand"},
                                      {Action::Hotbar1, "Hotbar Slot 1"},
                                      {Action::Hotbar2, "Hotbar Slot 2"},
                                      {Action::Hotbar3, "Hotbar Slot 3"},
                                      {Action::Hotbar4, "Hotbar Slot 4"},
                                      {Action::Hotbar5, "Hotbar Slot 5"},
                                      {Action::Hotbar6, "Hotbar Slot 6"},
                                      {Action::Hotbar7, "Hotbar Slot 7"},
                                      {Action::Hotbar8, "Hotbar Slot 8"},
                                      {Action::Hotbar9, "Hotbar Slot 9"},
                                      {Action::Inventory, "Open/Close Inventory"}}},
                                    {"Multiplayer",
                                     {{Action::PlayerList, "List Players"},
                                      {Action::Chat, "Open Chat"},
                                      {Action::Command, "Open Command"}}},
                                    {"Miscellaneous",
                                     {{Action::DebugScreen, "Debug Screen"},
                                      {Action::ToggleHud, "Hide GUI"},
                                      {Action::Screenshot, "Take Screenshot"},
                                      {Action::CyclePerspective, "Toggle Perspective"},
                                      {Action::Zoom, "Zoom"},
                                      {Action::Fullscreen, "Toggle Fullscreen"}}}};
        const auto keyText = [](const Binding& b) {
            std::string v = b.Serialize();
            for (const char* prefix : {"key.keyboard.", "key.mouse.", "pad."}) {
                const std::string pre = prefix;
                if (v.starts_with(pre))
                    v = v.substr(pre.size());
            }
            if (v == "left" || v == "right" || v == "middle")
                v += " button";
            else if (v == "back" || v == "forward")
                v = "mouse " + v;
            else if (v == "shift" || v == "control" || v == "alt")
                v = "left " + v;
            if (v.empty())
                return std::string("Not bound");
            if (v.size() == 1)
                v[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(v[0])));
            else {
                bool word = true;
                for (char& c : v) {
                    if (c == '_')
                        c = ' ';
                    if (word && c >= 'a' && c <= 'z')
                        c = static_cast<char>(c - 32);
                    word = c == ' ';
                }
            }
            return v;
        };
        const KeyBindings defaults = optionsUi.bindDevice == 1 ? DefaultPadBindings() : DefaultBindings();
        std::vector<ui::KeybindRow> rows;
        std::vector<int> rowAction;
        for (const Cat& c : kCats) {
            ui::KeybindRow h;
            h.header = true;
            h.label = c.name;
            rows.push_back(h);
            rowAction.push_back(-1);
            for (const auto& it : c.items) {
                ui::KeybindRow r;
                r.label = it.second;
                const Binding& b = BindingOf(it.first);
                const bool waiting = optionsUi.bindCapture == static_cast<int>(it.first);
                bool conflict = false;
                for (size_t o = 0; o < kActionCount; o++)
                    if (o != static_cast<size_t>(it.first) && b.device != Binding::Device::None &&
                        (optionsUi.bindDevice == 1 ? services.options.padKeys[o] : services.options.keys[o]) == b &&
                        !((it.first == Action::Attack || it.first == Action::Look) &&
                          static_cast<Action>(o) == Action::Look) &&
                        !(it.first == Action::Look))
                        conflict = true;
                r.value = waiting    ? "> " + keyText(b) + " <"
                          : conflict ? "[ " + keyText(b) + " ]"
                                     : keyText(b);
                r.color = waiting ? 2 : conflict ? 1 : 0;
                r.canReset = !(b == defaults[static_cast<size_t>(it.first)]);
                rows.push_back(r);
                rowAction.push_back(static_cast<int>(it.first));
            }
        }
        const std::vector<mods::PluginKey> pluginKeys =
            optionsUi.bindDevice == 1 ? std::vector<mods::PluginKey>{} : services.manager.Keys();
        std::string lastPlugin;
        for (size_t k = 0; k < pluginKeys.size(); k++) {
            const mods::PluginKey& pk = pluginKeys[k];
            if (pk.plugin != lastPlugin) {
                lastPlugin = pk.plugin;
                ui::KeybindRow h;
                h.header = true;
                h.label = pk.plugin;
                rows.push_back(h);
                rowAction.push_back(-1);
            }
            ui::KeybindRow r;
            r.label = pk.label;
            const Binding b = Binding::Parse(pk.binding);
            const bool waiting = optionsUi.bindCapture == 1000 + static_cast<int>(k);
            r.value = waiting ? "> " + keyText(b) + " <" : keyText(b);
            r.color = waiting ? 2 : 0;
            r.canReset = false;
            rows.push_back(r);
            rowAction.push_back(1000 + static_cast<int>(k));
        }
        const ui::KeybindLayout kl = ui::ComputeKeybindLayout(static_cast<int>(rows.size()), guiW, guiH);
        if (optionsUi.bindCapture < 0)
            optionsUi.kbScroll =
                std::clamp(optionsUi.kbScroll - host.inputManager.Scroll() * 20.0f, 0.0f, kl.maxScroll);
        else
            optionsUi.kbScroll = std::clamp(optionsUi.kbScroll, 0.0f, kl.maxScroll);
        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "k" +
                                std::to_string(static_cast<int>(optionsUi.kbScroll));
        for (const ui::KeybindRow& r : rows)
            signature += "|" + r.value + (r.canReset ? "r" : "");
        if (signature != optionsUi.signature || !hud.Overlay().HasOverlay()) {
            optionsUi.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(
                    ui::BuildKeybindsScreen(
                        rows,
                        optionsUi.bindDevice == 1 ? std::string("Controller Binds")
                                                  : i18n::TrOr("controls.keybinds.title", "Key Binds"),
                        i18n::TrOr("gui.done", "Done"), i18n::TrOr("controls.reset", "Reset"),
                        optionsUi.kbScroll, guiW, guiH),
                    error))
                Log("ui", "keybinds screen: {}", error);
        }
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, services.options.guiScale);
        host.input.SetPadCapture(optionsUi.bindCapture >= 0 && optionsUi.bindDevice == 1);
        if (optionsUi.bindCapture >= 0) {
            bool captured = false;
            if (optionsUi.bindDevice == 1 && optionsUi.bindCapture < 1000 &&
                Clock::now() - optionsUi.bindStartedAt > std::chrono::milliseconds(300))
                for (int code = 0; code < 40 && !captured; code++)
                    if (host.input.IsPadDown(code)) {
                        SetBindingOf(static_cast<Action>(optionsUi.bindCapture), Binding::PadButton(code));
                        captured = true;
                    }
            for (const auto& t : host.inputManager.TakeTyped()) {
                if (t.character || Clock::now() - optionsUi.bindStartedAt < std::chrono::milliseconds(120))
                    continue;
                if (captured)
                    continue;
                if (t.key != Key::Escape && optionsUi.bindDevice == 1) {
                    captured = true;
                    continue;
                }
                if (t.key != Key::Escape) {
                    if (optionsUi.bindCapture >= 1000) {
                        if (const size_t k = static_cast<size_t>(optionsUi.bindCapture - 1000);
                            k < pluginKeys.size())
                            services.manager.SetKeyBinding(
                                pluginKeys[k].plugin, pluginKeys[k].id,
                                Binding::KeyboardKey(t.key).Serialize());
                    } else
                        SetBindingOf(static_cast<Action>(optionsUi.bindCapture), Binding::KeyboardKey(t.key));
                }
                captured = true;
            }
            if (!host.input.IsMouseDown(Mouse::Left))
                optionsUi.bindLeftArmed = true;
            for (int b = 0; b <= Mouse::Forward && !captured && optionsUi.bindDevice == 0; b++)
                if ((b != Mouse::Left || optionsUi.bindLeftArmed) && host.input.IsMouseDown(b) &&
                    Clock::now() - optionsUi.bindStartedAt > std::chrono::milliseconds(120)) {
                    if (optionsUi.bindCapture >= 1000) {
                        if (const size_t k = static_cast<size_t>(optionsUi.bindCapture - 1000);
                            k < pluginKeys.size())
                            services.manager.SetKeyBinding(pluginKeys[k].plugin, pluginKeys[k].id,
                                                                       Binding::MouseButton(b).Serialize());
                    } else
                        SetBindingOf(static_cast<Action>(optionsUi.bindCapture), Binding::MouseButton(b));
                    captured = true;
                }
            if (captured) {
                optionsUi.bindCapture = -1;
                services.options.Save();
                optionsUi.signature.clear();
            }
            this->leftDown = down;
            return;
        }
        const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        if (down && !this->leftDown && clickReady) {
            if (inside(kl.doneX, kl.doneY, kl.doneW, 20.0f)) {
                services.options.Save();
                optionsUi.page = 3;
                optionsUi.kbScroll = 0;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (gy >= kl.listTop && gy < kl.listBottom) {
                for (int i = 0; i < static_cast<int>(rows.size()); i++) {
                    const int action = rowAction[static_cast<size_t>(i)];
                    if (action < 0)
                        continue;
                    const float y = kl.RowY(i, optionsUi.kbScroll);
                    if (inside(kl.keyX, y, kl.keyW, 20.0f)) {
                        optionsUi.bindCapture = action;
                        optionsUi.bindStartedAt = Clock::now();
                        optionsUi.bindLeftArmed = false;
                        optionsUi.signature.clear();
                        break;
                    }
                    if (inside(kl.resetX, y, kl.resetW, 20.0f) && rows[static_cast<size_t>(i)].canReset) {
                        SetBindingOf(static_cast<Action>(action), defaults[static_cast<size_t>(action)]);
                        services.options.Save();
                        optionsUi.signature.clear();
                        break;
                    }
                }
            }
        }
        this->leftDown = down;
    }

    void PauseScreen::UpdateWardrobe(PauseFrame& f) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        WardrobeFrame frame;
        frame.now = now;
        frame.gx = mx / static_cast<float>(scale);
        frame.gy = my / static_cast<float>(scale);
        frame.down = down;
        frame.click = down && !this->leftDown && clickReady;
        frame.scroll = host.inputManager.Scroll();
        frame.fbW = fbW;
        frame.fbH = fbH;
        frame.guiW = guiW;
        frame.guiH = guiH;
        WardrobeController controller(wardrobe, {services.options, services.activeAccountId,
                                                 services.setAccountSkin, services.openUrl, services.pickFile, hud,
                                                 live, optionsUi.signature});
        if (controller.Update(frame) == WardrobeController::Outcome::Done) {
            this->view = 0;
            this->signature.clear();
            optionsUi.signature.clear();
            hud.Overlay().ClearOverlay();
        }
        this->leftDown = down;
    }
}
