#include "client/PerlinPause.hpp"
#include "util/Log.hpp"

namespace perlin {
    void Perlin::UpdatePauseScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_PAUSE") != nullptr;
        const auto now = Clock::now();
        const bool canOpen = (live || preview) && !death.shown && !form.shown && !inventory.shown &&
                             !chat.IsOpen() && !bed.shown;
        if (modOpenView >= 0 && canOpen) {
            if (!pause.shown) {
                pause.shown = true;
                pause.openedAt = now;
                pause.leftDown = true;
                inputManager.TakeTyped();
                input.SetTextMode(true);
                CenterCursorForGui();
            }
            pause.view = modOpenView;
            optionsUi.page = modOpenPage;
            optionsUi.sub = 0;
            if (pause.view == 5)
                wardrobe.init = false;
            if (optionsUi.page == 11 && !modSettingsFor.empty())
                packUi.modSelected = modSettingsFor;
            modOpenView = -1;
            modOpenPage = 0;
            pause.signature.clear();
            optionsUi.signature.clear();
            hud.Overlay().ClearOverlay();
        }
        if (!pause.shown && titleScreen.options && titleScreen.active) {
            pause.shown = true;
            pause.view = titleScreen.openView;
            titleScreen.openView = 1;
            if (pause.view == 5)
                wardrobe.init = false;
            optionsUi.page = 0;
            optionsUi.sub = 0;
            pause.openedAt = now;
            pause.signature.clear();
            optionsUi.signature.clear();
            pause.leftDown = true;
            inputManager.TakeTyped();
            input.SetTextMode(true);
        } else if (!pause.shown && canOpen &&
                   (inputManager.Pressed(Action::Pause) || (preview && !pause.previewOpened))) {
            pause.previewOpened = true;
            pause.shown = true;
            pause.view = PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")
                             ? (PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x77   ? 3
                                : PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x6d ? 4
                                : PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x64 ? 5
                                                                                  : 1)
                             : 0;
            optionsUi.page =
                PERLIN_ENV("PERLIN_PREVIEW_OPTIONS") ? std::atoi(PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")) : 0;
            pause.openedAt = now;
            pause.signature.clear();
            inputManager.TakeTyped();
            input.SetTextMode(true);
            CenterCursorForGui();
        } else if (!pause.shown && !live && !preview && inputManager.Pressed(Action::Pause)) {
            platform->SetCursorCaptured(false);
        }
        if (pause.view != 5 || !pause.shown)
            wardrobe.init = false;
        if (!pause.shown)
            return;
        bool resume = death.shown || inventory.shown || chat.IsOpen();
        if (optionsUi.bindCapture < 0)
            for (const auto& t : inputManager.TakeTyped()) {
                if (pause.view == 4) {
                    ui::TextEdit::Focused().Apply(t, packUi.modSearch, ui::TextLimits{40});
                }
                if (!t.character && t.key == Key::Escape &&
                    now - pause.openedAt > std::chrono::milliseconds(100)) {
                    if (pause.view == 1 && optionsUi.page == 11) {
                        mods::ModManager::Instance().SettingsClosed(modSettingsFor);
                        pause.view = 4;
                        optionsUi.page = 0;
                        optionsUi.sub = 0;
                        optionsUi.signature.clear();
                        hud.Overlay().ClearOverlay();
                    } else if (pause.view == 1 && optionsUi.page != 0) {
                        CommitTexturePacks();
                        options.Save();
                        packUi.shaderPendingInit = false;
                        optionsUi.page = optionsUi.page == 5 ? 3 : optionsUi.page == 10 ? 8 : 0;
                        optionsUi.sub = 0;
                        optionsUi.kbScroll = 0;
                        optionsUi.signature.clear();
                        hud.Overlay().ClearOverlay();
                    } else if (pause.view != 0) {
                        options.Save();
                        pause.view = 0;
                        optionsUi.signature.clear();
                        pause.signature.clear();
                        hud.Overlay().ClearOverlay();
                    } else {
                        resume = true;
                    }
                }
            }
        if ((titleScreen.options || replayCtl.Playing()) && pause.shown && pause.view == 0)
            resume = true;
        if (resume) {
            if (titleScreen.options) {
                titleScreen.options = false;
                titleScreen.signature.clear();
            }
            pause.view = 0;
            hud.Overlay().ClearOverlay();
            pause.shown = false;
            if (!chat.IsOpen() && !death.shown && !inventory.shown)
                input.SetTextMode(false);
            return;
        }

        if (pause.tmpl.Refresh("pause_screen.rml", ui::DefaultPauseScreenTemplate(),
                               std::chrono::milliseconds(400), now, "pause screen"))
            pause.signature.clear();
        const auto [scale, guiW, guiH] = detail::OverlayViewport(hud, fbW, fbH);
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        bool down = input.IsMouseDown(0);
        if (const char* spec = PERLIN_ENV("PERLIN_PREVIEW_CLICK")) {
            const double elapsed = std::chrono::duration<double>(now - pause.openedAt).count();
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
        const bool clickReady = now - pause.openedAt > std::chrono::milliseconds(150);
        PauseFrame f{hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady};
        if (pause.view == 1 && optionsUi.page == 5)
            return UpdateKeybindPage(f);
        if (pause.view == 5)
            return UpdateWardrobe(f);
        if ((pause.view == 1 && (optionsUi.page == 6 || optionsUi.page == 7 || optionsUi.page == 8)) ||
            pause.view == 4)
            return UpdatePackMenu(f);
        if (pause.view == 1 || pause.view == 3 || pause.view == 4)
            return UpdateOptionsMenu(f);
        UpdatePauseMenu(f);
    }

    void Perlin::UpdatePauseMenu(PauseFrame& f) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        if (titleScreen.options) {
            titleScreen.options = false;
            titleScreen.signature.clear();
            pause.view = 0;
            pause.shown = false;
            hud.Overlay().ClearOverlay();
            return;
        }

        ui::PauseScreenText t;
        t.title = i18n::Tr("menu.game");
        t.back = i18n::Tr("menu.returnToGame");
        t.options = i18n::Tr("menu.options");
        t.world = detail::TrOr("perlin.worldOptions", "World Options");
        t.mods = detail::TrOr("perlin.mods", "Mods");
        t.quit = i18n::Tr("menu.disconnect");
        t.advancements = detail::TrOr("gui.advancements", "Advancements");
        t.stats = detail::TrOr("gui.stats", "Statistics");
        t.feedback = detail::TrOr("menu.sendFeedback", "Give Feedback");
        t.bugs = detail::TrOr("menu.reportBugs", "Report Bugs");
        t.lan = detail::TrOr("menu.shareToLan", "Open to LAN");
        t.players = detail::TrOr("gui.playerList", "Player List");
        t.showReplay = live != nullptr && !preview && !live->IsReplay();
        t.replayRecording = replayCtl.Recording();
        t.replayStart = detail::TrOr("perlin.replay.start", "Start Replay");
        t.replayStop = detail::TrOr("perlin.replay.stop", "Stop Replay");
        t.showPanel = pause.players && live;
        if (t.showPanel) {
            t.panelTitle = t.players;
            t.roleOperator = detail::TrOr("permissions.level.operator", "Operator");
            t.roleMember = detail::TrOr("permissions.level.member", "Member");
            t.roleVisitor = detail::TrOr("permissions.level.visitor", "Visitor");
            auto list = live->PlayerList();
            std::stable_sort(list.begin(), list.end(),
                             [](const BedrockSession::TabPlayer& a, const BedrockSession::TabPlayer& b) {
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
        if (signature != pause.signature || !hud.Overlay().HasOverlay()) {
            pause.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildPauseScreenRml(pause.tmpl.text, t, guiW, guiH), error)) {
                Log("ui", "pause_screen.rml: {}", error);
                pause.tmpl.text = ui::DefaultPauseScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildPauseScreenRml(pause.tmpl.text, t, guiW, guiH), error);
            }
        }
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        if (!down && pause.leftDown && clickReady) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kPauseResumeId) {
                hud.Overlay().ClearOverlay();
                pause.shown = false;
                input.SetTextMode(false);
            } else if (hover == ui::kPauseReplayStartId && live && !replayCtl.Recording()) {
                ToggleReplayRecording(true);
                pause.signature.clear();
            } else if (hover == ui::kPauseReplayStopId && live && replayCtl.Recording()) {
                ToggleReplayRecording(false);
                pause.signature.clear();
            } else if (hover == ui::kPauseQuitId && !preview) {
                LeaveToServerList(hud);
            } else if (hover == ui::kPauseFeedbackId) {
                platform->OpenUrl("https://github.com/theselauraproject/perlin/issues");
            } else if (hover == ui::kPausePlayersId) {
                pause.players = !pause.players;
            } else if (hover == ui::kPauseBugsId) {
                platform->OpenUrl("https://github.com/theselauraproject/perlin/issues/new?labels=bug");
            } else if (hover == ui::kPauseWardrobeId) {
                pause.view = 5;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseModsId) {
                pause.view = 4;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseWorldId) {
                pause.view = 3;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseOptionsId) {
                pause.view = 1;
                optionsUi.page = 0;
                optionsUi.sub = 0;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            }
        }
        pause.leftDown = down;
    }

    void Perlin::UpdateKeybindPage(PauseFrame& f) {
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
                        (optionsUi.bindDevice == 1 ? options.padKeys[o] : options.keys[o]) == b &&
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
            optionsUi.bindDevice == 1 ? std::vector<mods::PluginKey>{} : mods::ModManager::Instance().Keys();
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
                std::clamp(optionsUi.kbScroll - inputManager.Scroll() * 20.0f, 0.0f, kl.maxScroll);
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
                    ui::BuildKeybindsScreenRml(
                        rows,
                        optionsUi.bindDevice == 1 ? std::string("Controller Binds")
                                                  : detail::TrOr("controls.keybinds.title", "Key Binds"),
                        detail::TrOr("gui.done", "Done"), detail::TrOr("controls.reset", "Reset"),
                        optionsUi.kbScroll, guiW, guiH),
                    error))
                Log("ui", "keybinds screen: {}", error);
        }
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        input.SetPadCapture(optionsUi.bindCapture >= 0 && optionsUi.bindDevice == 1);
        if (optionsUi.bindCapture >= 0) {
            bool captured = false;
            if (optionsUi.bindDevice == 1 && optionsUi.bindCapture < 1000 &&
                Clock::now() - optionsUi.bindStartedAt > std::chrono::milliseconds(300))
                for (int code = 0; code < 40 && !captured; code++)
                    if (input.IsPadDown(code)) {
                        SetBindingOf(static_cast<Action>(optionsUi.bindCapture), Binding::PadButton(code));
                        captured = true;
                    }
            for (const auto& t : inputManager.TakeTyped()) {
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
                            mods::ModManager::Instance().SetKeyBinding(
                                pluginKeys[k].plugin, pluginKeys[k].id,
                                Binding::KeyboardKey(t.key).Serialize());
                    } else
                        SetBindingOf(static_cast<Action>(optionsUi.bindCapture), Binding::KeyboardKey(t.key));
                }
                captured = true;
            }
            // the click that opened the prompt has to be released before left can be bound
            if (!input.IsMouseDown(Mouse::Left))
                optionsUi.bindLeftArmed = true;
            for (int b = 0; b <= Mouse::Forward && !captured && optionsUi.bindDevice == 0; b++)
                if ((b != Mouse::Left || optionsUi.bindLeftArmed) && input.IsMouseDown(b) &&
                    Clock::now() - optionsUi.bindStartedAt > std::chrono::milliseconds(120)) {
                    if (optionsUi.bindCapture >= 1000) {
                        if (const size_t k = static_cast<size_t>(optionsUi.bindCapture - 1000);
                            k < pluginKeys.size())
                            mods::ModManager::Instance().SetKeyBinding(pluginKeys[k].plugin, pluginKeys[k].id,
                                                                       Binding::MouseButton(b).Serialize());
                    } else
                        SetBindingOf(static_cast<Action>(optionsUi.bindCapture), Binding::MouseButton(b));
                    captured = true;
                }
            if (captured) {
                optionsUi.bindCapture = -1;
                options.Save();
                optionsUi.signature.clear();
            }
            pause.leftDown = down;
            return;
        }
        const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        if (down && !pause.leftDown && clickReady) {
            if (inside(kl.doneX, kl.doneY, kl.doneW, 20.0f)) {
                options.Save();
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
                        options.Save();
                        optionsUi.signature.clear();
                        break;
                    }
                }
            }
        }
        pause.leftDown = down;
    }
}
