#include "client/PerlinPause.hpp"
#include "client/update/Updater.hpp"
#include "util/Log.hpp"

namespace perlin {
    namespace {
        std::string Percent(float v) {
            return std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%";
        }
        std::string OnOff(bool on) {
            return on ? detail::TrOr("options.on", "ON") : detail::TrOr("options.off", "OFF");
        }
        float Snap(float f, float steps) {
            return std::round(std::clamp(f, 0.0f, 1.0f) * steps) / steps;
        }
        constexpr int kGuiScales[] = {0, 1, 2, 3, 4};
    }

    struct OptRow {
        std::string label, value;
        float fraction;
        std::function<void(float, bool)> apply;
        bool header = false;
        bool locked = false;
        bool wide = false;
        int tab = 0;
        std::string icon;
    };

    // the rows of one options page; a row's apply gets click=true on a click, the pointer's fraction on a drag
    struct OptionsModel {
        std::vector<OptRow> rows;
        std::string title = detail::TrOr("options.title", "Options");
        std::function<void(int)> navigate;

        void Nav(const std::string& label, int page) {
            rows.push_back({label, "", -1.0f, [go = navigate, page](float, bool click) {
                                if (click)
                                    go(page);
                            }});
        }
        void Toggle(const std::string& label, bool& ref) {
            rows.push_back({label, OnOff(ref), -1.0f, [&ref](float, bool click) {
                                if (click)
                                    ref = !ref;
                            }});
        }
        void Slider(const std::string& label, const std::string& value, float fraction,
                    std::function<void(float)> set) {
            rows.push_back({label, value, fraction, [set = std::move(set)](float f, bool) { set(f); }});
        }
        // cycles through values on each click; shows the matching display name
        void Choice(const std::string& label, std::string& ref,
                    std::vector<std::pair<std::string, std::string>> values) {
            std::string shown = ref;
            for (const auto& [id, name] : values)
                if (id == ref)
                    shown = name;
            rows.push_back({label, shown, -1.0f, [&ref, values](float, bool click) {
                                if (!click)
                                    return;
                                size_t at = 0;
                                for (size_t i = 0; i < values.size(); i++)
                                    if (values[i].first == ref)
                                        at = i;
                                ref = values[(at + 1) % values.size()].first;
                            }});
        }
        void Volume(const char* key, const char* fallback, float& ref) {
            Slider(detail::TrOr(key, fallback),
                   ref <= 0.001f ? detail::TrOr("options.off", "OFF") : Percent(ref), ref,
                   [&ref](float f) { ref = Snap(f, 100.0f); });
        }
    };

    void Perlin::UpdateOptionsMenu(PauseFrame& f) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        packUi.modRowTips.clear();
        OptionsModel m;
        m.navigate = [this](int page) {
            optionsUi.page = page;
            optionsUi.sub = 0;
            optionsUi.signature.clear();
            optionsUi.bindCapture = -1;
        };
        if (pause.view == 3)
            BuildWorldOptions(m, f);
        else if (pause.view == 4)
            BuildModsOptions(m, f);
        else if (optionsUi.page == 0)
            BuildMainOptions(m, f);
        else if (optionsUi.page == 1)
            BuildVideoOptions(m, f);
        else if (optionsUi.page == 7)
            BuildResourcePacksOptions(m, f);
        else if (optionsUi.page == 8)
            BuildShaderPacksOptions(m, f);
        else if (optionsUi.page == 2)
            BuildSoundOptions(m, f);
        else if (optionsUi.page == 3)
            BuildControlsOptions(m, f);
        else if (optionsUi.page == 11)
            BuildModSettingsOptions(m, f);
        else if (optionsUi.page == 10 && renderer && renderer->JavaShaderPackActive())
            BuildJavaShaderSettingsOptions(m, f);
        else if (optionsUi.page == 10)
            BuildShaderSettingsOptions(m, f);
        else if (optionsUi.page == 9)
            BuildSkinOptions(m, f);
        else if (optionsUi.page == 4)
            BuildHandOptions(m, f);
        else
            BuildKeyBindsOptions(m, f);

        std::vector<ui::OptionsRow> view;
        for (const OptRow& r : m.rows) {
            ui::OptionsRow row{r.label, r.value, r.fraction};
            row.header = r.header;
            row.locked = r.locked;
            row.wide = r.wide;
            row.tab = r.tab;
            ui::SettingInfo info;
            if (!r.header && ui::FindSettingInfo(r.label, info)) {
                row.tip = info.description;
                row.impact = info.impact;
            }
            row.icon = r.icon;
            if (optionsUi.page == 11 && pause.view == 1) {
                const size_t idx = view.size();
                if (idx < packUi.modRowTips.size())
                    row.tip = packUi.modRowTips[idx];
            }
            view.push_back(row);
        }
        const int scrollKey = pause.view * 100 + optionsUi.page;
        if (scrollKey != optionsUi.scrollKey) {
            optionsUi.scrollKey = scrollKey;
            optionsUi.scroll = 0.0f;
        }
        const ui::OptionsLayout probe = ui::ComputeOptionsLayout(view, guiW, guiH, 0.0f);
        if (probe.scrolling && optionsUi.bindCapture < 0)
            optionsUi.scroll =
                std::clamp(optionsUi.scroll - inputManager.Scroll() * 20.0f, 0.0f, probe.maxScroll);
        else if (!probe.scrolling)
            optionsUi.scroll = 0.0f;
        const ui::OptionsLayout layout = ui::ComputeOptionsLayout(view, guiW, guiH, optionsUi.scroll);
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        int hoverRow = -1;
        for (int i = 0; i < layout.count; i++)
            if (layout.visible[static_cast<size_t>(i)] && !view[static_cast<size_t>(i)].header &&
                gx >= layout.x[i] && gx < layout.x[i] + layout.rowW[static_cast<size_t>(i)] &&
                gy >= layout.y[i] && gy < layout.y[i] + layout.h && gy >= layout.listTop - 2.0f &&
                gy < layout.listBottom) {
                hoverRow = i;
                break;
            }
        if (hoverRow != optionsUi.hoverRow) {
            optionsUi.hoverRow = hoverRow;
            optionsUi.hoverSince = Clock::now();
        }
        const bool showTip =
            hoverRow >= 0 && Clock::now() - optionsUi.hoverSince > std::chrono::milliseconds(350);
        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + m.title +
                                std::to_string(static_cast<int>(optionsUi.scroll)) +
                                (showTip ? "T" + std::to_string(hoverRow) : "");
        for (const ui::OptionsRow& r : view)
            signature += "|" + r.label + r.value;
        if (signature != optionsUi.signature || !hud.Overlay().HasOverlay()) {
            optionsUi.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(
                    ui::BuildOptionsScreenRml(view, m.title, detail::TrOr("gui.done", "Done"),
                                              optionsUi.scroll, guiW, guiH, showTip ? hoverRow : -1),
                    error))
                Log("ui", "options screen: {}", error);
        }
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        const auto sliderFraction = [&](int row) {
            return (gx - layout.x[row] - 4.0f) / (layout.rowW[static_cast<size_t>(row)] - 8.0f);
        };
        input.SetPadCapture(optionsUi.bindCapture >= 0 && optionsUi.bindDevice == 1);
        if (optionsUi.bindCapture >= 0) {
            bool captured = false;
            if (optionsUi.bindDevice == 1 &&
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
                if (t.key != Key::Escape && optionsUi.bindDevice == 0)
                    SetBindingOf(static_cast<Action>(optionsUi.bindCapture), Binding::KeyboardKey(t.key));
                captured = true;
            }
            for (int b = 1; b <= 2 && !captured && optionsUi.bindDevice == 0; b++)
                if (input.IsMouseDown(b) &&
                    Clock::now() - optionsUi.bindStartedAt > std::chrono::milliseconds(120)) {
                    SetBindingOf(static_cast<Action>(optionsUi.bindCapture), Binding::MouseButton(b));
                    captured = true;
                }
            if (captured) {
                optionsUi.bindCapture = -1;
                optionsUi.bindWasCapturing = true;
                options.Save();
                optionsUi.signature.clear();
            }
            pause.leftDown = down;
            return;
        }
        optionsUi.bindWasCapturing = false;
        if (down && !pause.leftDown && clickReady) {
            for (int i = 0; i < layout.count; i++)
                if (layout.visible[static_cast<size_t>(i)] && !m.rows[static_cast<size_t>(i)].header &&
                    inside(layout.x[i], layout.y[i], layout.rowW[static_cast<size_t>(i)], layout.h)) {
                    const OptRow& row = m.rows[static_cast<size_t>(i)];
                    if (row.fraction >= 0.0f) {
                        optionsUi.drag = i;
                        row.apply(sliderFraction(i), true);
                    } else {
                        row.apply(0.0f, true);
                    }
                    break;
                }
        } else if (down && optionsUi.drag >= 0 && optionsUi.drag < static_cast<int>(m.rows.size())) {
            m.rows[static_cast<size_t>(optionsUi.drag)].apply(sliderFraction(optionsUi.drag), false);
        }
        if (!down && pause.leftDown) {
            if (optionsUi.drag >= 0 || clickReady) {
                if (inside(layout.doneX, layout.doneY, layout.doneW, layout.h) && optionsUi.drag < 0) {
                    options.Save();
                    if (pause.view == 1 && optionsUi.page == 11) {
                        mods::ModManager::Instance().SettingsClosed(modSettingsFor);
                        pause.view = 4;
                        optionsUi.page = 0;
                        optionsUi.sub = 0;
                    } else if (pause.view == 1 && optionsUi.page != 0) {
                        optionsUi.page = optionsUi.page == 5 ? 3 : optionsUi.page == 10 ? 8 : 0;
                        optionsUi.sub = 0;
                    } else {
                        pause.view = 0;
                        pause.signature.clear();
                    }
                    optionsUi.signature.clear();
                    hud.Overlay().ClearOverlay();
                }
            }
            if (optionsUi.drag >= 0)
                options.Save();
            optionsUi.drag = -1;
        }
        pause.leftDown = down;
    }

    void Perlin::BuildWorldOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("perlin.worldOptions", "World Options");
        const BedrockSession::WorldSettings ws = live ? live->World() : BedrockSession::WorldSettings{};
        const int perm = live ? live->OwnPermission() : 2;
        const bool isOp = perm >= 2;
        const bool cheats = isOp && (!live || ws.commandsEnabled);
        const double nowSec = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        const auto ruleDefault = [](const std::string& k) {
            if (k == "playerssleepingpercentage")
                return 100;
            if (k == "spawnradius")
                return 10;
            if (k == "randomtickspeed")
                return 1;
            for (const char* off :
                 {"showcoordinates", "showdaysplayed", "doimmediaterespawn", "keepinventory", "locatorbar"})
                if (k == off)
                    return 0;
            return 1;
        };
        const auto ruleValue = [this, ws, nowSec, ruleDefault](const std::string& k) {
            const auto o = optionsUi.worldOverride.find(k);
            if (o != optionsUi.worldOverride.end() && nowSec - o->second.second < 2.0)
                return o->second.first;
            const auto it = ws.rules.find(k);
            return it == ws.rules.end() ? ruleDefault(k) : it->second;
        };
        const auto run = [live](const std::string& cmd) {
            if (live)
                live->SendCommand(cmd);
        };
        const auto setRule = [this, run, nowSec](const std::string& k, int v, bool boolean) {
            optionsUi.worldOverride[k] = {v, nowSec};
            run("gamerule " + k + " " + (boolean ? (v != 0 ? "true" : "false") : std::to_string(v)));
        };
        const auto head = [&](const std::string& text) {
            OptRow r{text, "", -1.0f, [](float, bool) {}};
            r.header = true;
            m.rows.push_back(r);
        };
        const auto cyc = [&](const std::string& label, const std::vector<std::string>& names, int at,
                             bool enabled, std::function<void(int)> pick) {
            const int safe = std::clamp(at, 0, static_cast<int>(names.size()) - 1);
            m.rows.push_back({label, names[static_cast<size_t>(safe)], -1.0f, [=](float, bool click) {
                                  if (click && enabled)
                                      pick((safe + 1) % static_cast<int>(names.size()));
                              }});
            m.rows.back().locked = !enabled;
        };
        const auto rule = [&](const std::string& label, const std::string& key, bool enabled) {
            const bool on = ruleValue(key) != 0;
            m.rows.push_back({label, OnOff(on), -1.0f, [=](float, bool click) {
                                  if (click && enabled)
                                      setRule(key, on ? 0 : 1, true);
                              }});
            m.rows.back().locked = !enabled;
        };
        const auto ruleSlider = [&](const std::string& label, const std::string& key, int maxValue,
                                    const char* suffix, bool enabled) {
            const int v = std::clamp(ruleValue(key), 0, maxValue);
            m.rows.push_back({label, std::to_string(v) + suffix,
                              static_cast<float>(v) / static_cast<float>(maxValue), [=](float f, bool) {
                                  if (enabled)
                                      setRule(key,
                                              static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) *
                                                                           static_cast<float>(maxValue))),
                                              false);
                              }});
        };
        head("Game");
        static const char* const kDiff[] = {"peaceful", "easy", "normal", "hard"};
        cyc(detail::TrOr("options.difficulty", "Difficulty"), {"Peaceful", "Easy", "Normal", "Hard"},
            ws.difficulty, isOp, [=](int i) { run(std::string("difficulty ") + kDiff[i]); });
        static const char* const kModes[] = {"survival", "creative", "adventure"};
        cyc("Default Mode", {"Survival", "Creative", "Adventure"}, ws.worldGameMode, isOp,
            [=](int i) { run(std::string("defaultgamemode ") + kModes[i]); });
        static const char* const kPersonal[] = {"survival", "creative", "adventure", "spectator"};
        cyc("Your Game Mode", {"Survival", "Creative", "Adventure", "Spectator"}, live ? live->GameType() : 0,
            isOp, [=](int i) { run(std::string("gamemode ") + kPersonal[i]); });
        head("World");
        rule("Friendly Fire", "pvp", isOp);
        rule("Show Coordinates", "showcoordinates", isOp);
        rule("Player Waypoints", "locatorbar", isOp);
        rule("Show Days Played", "showdaysplayed", isOp);
        rule("Fire Spreads", "dofiretick", isOp);
        rule("Recipes Unlocking", "recipesunlock", isOp);
        rule("TNT Explodes", "tntexplodes", isOp);
        rule("Respawn Blocks Explode", "respawnblocksexplode", isOp);
        rule("Mob Loot", "domobloot", isOp);
        rule("Natural Regeneration", "naturalregeneration", isOp);
        rule("Tile Drops", "dotiledrops", isOp);
        rule("Immediate Respawn", "doimmediaterespawn", isOp);
        ruleSlider("Skip Night By Sleeping", "playerssleepingpercentage", 100, "%", isOp);
        ruleSlider("Respawn Radius", "spawnradius", 128, " blocks", isOp);
        head("Cheats");
        {
            const auto co = optionsUi.worldOverride.find("commandsenabled");
            const bool on = (co != optionsUi.worldOverride.end() && nowSec - co->second.second < 3.0)
                                ? co->second.first != 0
                                : (!live || ws.commandsEnabled);
            m.rows.push_back(
                {"Activate Cheats", OnOff(on), -1.0f, [=, this](float, bool click) {
                     if (click && isOp) {
                         optionsUi.worldOverride["commandsenabled"] = {on ? 0 : 1, nowSec};
                         if (live)
                             live->SendSettingsCommand(std::string("changesetting allow-cheats ") +
                                                       (on ? "false" : "true"));
                         else
                             run(std::string("changesetting allow-cheats ") + (on ? "false" : "true"));
                     }
                 }});
            m.rows.back().locked = !isOp;
        }
        rule("Normal Day/Night Cycle", "dodaylightcycle", cheats);
        {
            const bool always = ruleValue("dodaylightcycle") == 0;
            m.rows.push_back({"Always Day", OnOff(always), -1.0f, [=, this](float, bool click) {
                                  if (click && cheats) {
                                      optionsUi.worldOverride["dodaylightcycle"] = {always ? 1 : 0, nowSec};
                                      run(always ? "alwaysday false" : "alwaysday true");
                                  }
                              }});
            m.rows.back().locked = !cheats;
        }
        rule("Keep Inventory", "keepinventory", cheats);
        rule("Mob Spawn Normally", "domobspawning", cheats);
        rule("Mob Griefing", "mobgriefing", cheats);
        rule("Entities Drop Loot", "doentitydrops", cheats);
        rule("Normal Weather Cycle", "doweathercycle", cheats);
        rule("Command Blocks Enabled", "commandblocksenabled", cheats);
        ruleSlider("Tick Speed", "randomtickspeed", 100, "", cheats);
        m.rows.push_back({"Reset Tick Speed", "", -1.0f, [=](float, bool click) {
                              if (click && cheats)
                                  setRule("randomtickspeed", 1, false);
                          }});
    }

    void Perlin::BuildModsOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("perlin.mods", "Mods");
        auto& mm = mods::ModManager::Instance();
        m.rows.push_back({"Reload All Mods", "", -1.0f, [&mm](float, bool click) {
                              if (click)
                                  mm.ReloadAll();
                          }});
        m.Toggle("Hot Reload On Save", options.hotReload);
        const auto infos = mm.List();
        if (infos.empty())
            m.rows.push_back({"No .wasm mods in", mm.Directory(), -1.0f, [](float, bool) {}});
        for (const mods::ModInfo& info : infos) {
            const std::string name = info.name;
            std::string state = info.enabled ? info.state : std::string("disabled");
            if (!info.error.empty())
                state += " - " + info.error.substr(0, 24);
            m.rows.push_back({name, state, -1.0f, [this, name](float, bool click) {
                                  if (!click)
                                      return;
                                  ToggleMod(name);
                              }});
        }
    }

    void Perlin::BuildMainOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.Slider(detail::TrOr("options.fov", "FOV"),
                 std::to_string(static_cast<int>(std::lround(options.fov))), (options.fov - 30.0f) / 80.0f,
                 [this](float f) { options.fov = std::round(30.0f + std::clamp(f, 0.0f, 1.0f) * 80.0f); });
        m.rows.back().wide = true;
        m.Nav(detail::TrOr("perlin.wardrobe.skinCustomization", "Skin Customization..."), 9);
        m.Nav(detail::TrOr("options.sounds.title", "Music & Sounds..."), 2);
        m.Nav(detail::TrOr("options.videoTitle", "Video Settings..."), 1);
        m.Nav(detail::TrOr("options.controls", "Controls..."), 3);
        m.Nav(detail::TrOr("options.language", "Language..."), 6);
        m.Nav(detail::TrOr("perlin.options.hand", "Hand & Camera..."), 4);
        m.Nav(detail::TrOr("resourcePack.title", "Resource Packs..."), 7);
        m.Nav(detail::TrOr("perlin.shaderPacks", "Shader Packs..."), 8);
        // nightly builds keep themselves current (src/client/update); local builds have nothing to follow
        if (update::Supported()) {
            m.Toggle(detail::TrOr("perlin.options.autoUpdate", "Auto Update"), options.autoUpdate);
            m.rows.back().wide = true;
        }
    }

    void Perlin::BuildVideoOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("options.videoTitle", "Video Settings");
        m.Toggle(detail::TrOr("options.graphics.fancy", "Fancy Graphics"), options.fancyGraphics);
        if (!options.enableLods && options.renderDistance > LodStore::kRealChunks) {
            options.renderDistance = LodStore::kRealChunks;
            if (live) {
                live->SetChunkRadius(options.renderDistance);
                live->SetNearRadius(1 << 20);
                live->RequestChunkRadiusNow();
            }
        }
        const int maxDistance = options.enableLods ? 256 : LodStore::kRealChunks;
        const auto distanceToSlider = [maxDistance](int d) {
            if (maxDistance <= LodStore::kRealChunks)
                return static_cast<float>(d - 2) / static_cast<float>(maxDistance - 2);
            return d <= LodStore::kRealChunks
                       ? 0.5f * static_cast<float>(d - 2) / static_cast<float>(LodStore::kRealChunks - 2)
                       : 0.5f + 0.5f * static_cast<float>(d - LodStore::kRealChunks) /
                                    static_cast<float>(maxDistance - LodStore::kRealChunks);
        };
        m.Slider(
            detail::TrOr("options.renderDistance", "Render Distance"),
            std::to_string(options.renderDistance) + " " + detail::TrOr("options.chunks", "chunks"),
            distanceToSlider(options.renderDistance), [this, live, maxDistance](float f) {
                f = std::clamp(f, 0.0f, 1.0f);
                int distance;
                if (maxDistance <= LodStore::kRealChunks)
                    distance = 2 + static_cast<int>(std::lround(f * static_cast<float>(maxDistance - 2)));
                else if (f <= 0.5f)
                    distance = 2 + static_cast<int>(
                                       std::lround(f * 2.0f * static_cast<float>(LodStore::kRealChunks - 2)));
                else
                    distance =
                        LodStore::kRealChunks +
                        static_cast<int>(std::lround(
                            (f - 0.5f) * 2.0f * static_cast<float>(maxDistance - LodStore::kRealChunks)));
                if (distance != options.renderDistance) {
                    options.renderDistance = distance;
                    if (live) {
                        live->SetChunkRadius(options.renderDistance);
                        live->SetMaxViewDistance(std::max(32, options.renderDistance));
                        live->SetNearRadius(options.enableLods &&
                                                    options.renderDistance > LodStore::kRealChunks
                                                ? LodStore::kRealChunks
                                                : 1 << 20);
                        live->RequestChunkRadiusNow();
                    }
                }
            });
        m.Toggle("Enable LODs", options.enableLods);
        if (live) {
            const int wantNear = options.enableLods && options.renderDistance > LodStore::kRealChunks
                                     ? LodStore::kRealChunks
                                     : 1 << 20;
            if (live->NearRadius() != wantNear) {
                live->SetNearRadius(wantNear);
                live->SetMaxViewDistance(std::max(32, options.renderDistance));
                live->RequestChunkRadiusNow();
            }
        }
        m.Slider(detail::TrOr("options.framerateLimit", "Max Framerate"),
                 options.maxFps == 0 ? detail::TrOr("options.framerateLimit.max", "Unlimited")
                                     : std::to_string(options.maxFps) + " fps",
                 options.maxFps == 0 ? 1.0f : static_cast<float>(options.maxFps - 10) / 250.0f,
                 [this](float f) {
                     const int steps = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 25.0f));
                     options.maxFps = steps >= 25 ? 0 : 10 + steps * 10;
                 });
        m.Toggle(detail::TrOr("options.vsync", "VSync"), options.vsync);
        m.Toggle(detail::TrOr("options.fullscreen", "Fullscreen"), options.fullscreen);
        m.rows.push_back({std::string("GUI Scale"),
                          options.guiScale == 0 ? detail::TrOr("options.guiScale.auto", "Auto")
                                                : std::to_string(options.guiScale),
                          -1.0f, [this](float, bool click) {
                              if (!click)
                                  return;
                              size_t at = 0;
                              for (size_t i = 0; i < std::size(kGuiScales); i++)
                                  if (kGuiScales[i] == options.guiScale)
                                      at = i;
                              options.guiScale = kGuiScales[(at + 1) % std::size(kGuiScales)];
                          }});
        m.Slider(detail::TrOr("options.gamma", "Brightness"),
                 options.gamma <= 0.01f   ? detail::TrOr("options.gamma.min", "Moody")
                 : options.gamma >= 0.99f ? detail::TrOr("options.gamma.max", "Bright")
                                          : Percent(options.gamma),
                 options.gamma, [this](float f) { options.gamma = Snap(f, 100.0f); });
        m.Slider(detail::TrOr("options.fov", "FOV"),
                 std::to_string(static_cast<int>(std::lround(options.fov))), (options.fov - 30.0f) / 80.0f,
                 [this](float f) { options.fov = std::round(30.0f + std::clamp(f, 0.0f, 1.0f) * 80.0f); });
        m.Slider(detail::TrOr("options.motionBlur", "Motion Blur"),
                 options.motionBlur <= 0.001f ? detail::TrOr("options.off", "OFF")
                                              : Percent(options.motionBlur),
                 options.motionBlur, [this](float f) { options.motionBlur = Snap(f, 100.0f); });
        m.Slider(detail::TrOr("options.accessibility.menu_background_blurriness", "Menu Blur"),
                 std::to_string(options.menuBlur), static_cast<float>(options.menuBlur) / 10.0f,
                 [this](float f) {
                     options.menuBlur = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 10.0f));
                 });
        m.Toggle(detail::TrOr("options.entityShadows", "Entity Shadows"), options.shadows);
        m.Toggle(detail::TrOr("options.fovEffectScale", "FOV Effects"), options.fovEffects);
        m.Slider(detail::TrOr("options.damageTilt", "Damage Tilt"),
                 Percent(static_cast<float>(options.damageTilt) / 100.0f),
                 static_cast<float>(options.damageTilt) / 100.0f, [this](float f) {
                     options.damageTilt = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 100.0f));
                 });
        m.Toggle(detail::TrOr("perlin.options.damageTint", "Damage Tint"), options.damageTint);
        m.Slider("Cloud Range",
                 std::to_string(options.cloudRange) + " " + detail::TrOr("options.chunks", "chunks"),
                 static_cast<float>(options.cloudRange - 2) / 126.0f, [this](float f) {
                     options.cloudRange =
                         2 + static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 126.0f));
                 });
        m.Toggle("Better Grass", options.betterGrass);
        {
            std::vector<std::pair<std::string, std::string>> apis = {{"auto", "Auto"}};
            for (rhi::Api api : rhi::CompiledApis())
                apis.emplace_back(rhi::ApiId(api), rhi::ApiName(api));
            size_t at = 0;
            for (size_t i = 0; i < apis.size(); i++)
                if (apis[i].first == options.renderer)
                    at = i;
            const size_t last = apis.size() - 1;
            m.Slider("Renderer", apis[at].second,
                     last ? static_cast<float>(at) / static_cast<float>(last) : 0.0f,
                     [this, apis, last](float f) {
                         options.renderer = apis[static_cast<size_t>(std::lround(std::clamp(f, 0.0f, 1.0f) *
                                                                                 static_cast<float>(last)))]
                                                .first;
                     });
        }
        m.Choice("Glass", options.glass,
                 {{"normal", "Normal"},
                  {"connected", "Connected"},
                  {"connectedClear", "Connected Clear"},
                  {"clear", "Clear"}});
        {
            static const char* const kQuality[5] = {"Off", "Low", "Medium", "High", "Ultra"};
            m.Slider("Shadow Quality", kQuality[std::clamp(options.shadowQuality, 0, 4)],
                     static_cast<float>(options.shadowQuality) / 4.0f, [this](float f) {
                         options.shadowQuality =
                             static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 4.0f));
                     });
        }
        m.Slider("Shadow Distance",
                 options.shadowDistance == 0 ? std::string("Auto") : std::to_string(options.shadowDistance),
                 static_cast<float>(options.shadowDistance) / 256.0f, [this](float f) {
                     options.shadowDistance =
                         static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 64.0f)) * 4;
                 });
        m.Toggle("Mob & Player Shadows", options.shadowEntities);
        m.Slider("Bloom",
                 options.bloom <= 0.001f ? detail::TrOr("options.off", "OFF") : Percent(options.bloom),
                 options.bloom, [this](float f) { options.bloom = Snap(f, 20.0f); });
        m.Slider("Held Item Light", std::to_string(options.heldLight),
                 static_cast<float>(options.heldLight) / 15.0f, [this](float f) {
                     options.heldLight = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 15.0f));
                 });
        m.Choice("Dynamic Lights", options.dynamicLights,
                 {{"off", "Off"}, {"fast", "Fast"}, {"fancy", "Fancy"}});
        m.Toggle("Third Person Crosshair", options.thirdPersonCrosshair);
        m.Toggle("Third Person Nametag", options.thirdPersonNametag);
        {
            OptRow h{"Zoom", "", -1.0f, [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        }
        auto& z = options.zoom;
        m.Slider("Zoom Level", Percent(z.level),
                 (z.level - z.minLevel) / std::max(0.001f, z.maxLevel - z.minLevel), [this](float f) {
                     auto& zz = options.zoom;
                     zz.level = zz.minLevel + std::round(std::clamp(f, 0.0f, 1.0f) * 100.0f) / 100.0f *
                                                  (zz.maxLevel - zz.minLevel);
                 });
        m.Slider("Zoom In Time", std::to_string(static_cast<int>(std::lround(z.inSeconds * 1000.0f))) + " ms",
                 z.inSeconds / 2.0f, [this](float f) {
                     options.zoom.inSeconds = std::round(std::clamp(f, 0.0f, 1.0f) * 100.0f) / 50.0f;
                 });
        m.Slider("Zoom Out Time",
                 std::to_string(static_cast<int>(std::lround(z.outSeconds * 1000.0f))) + " ms",
                 z.outSeconds / 2.0f, [this](float f) {
                     options.zoom.outSeconds = std::round(std::clamp(f, 0.0f, 1.0f) * 100.0f) / 50.0f;
                 });
        m.Choice("Zoom Easing", z.easing,
                 {{"linear", "Linear"},
                  {"cubic", "Cubic"},
                  {"quint", "Quint"},
                  {"sine", "Sine"},
                  {"smooth", "Smooth"}});
        m.Slider("Zoom Scroll Step", Percent(z.scrollStep), z.scrollStep / 0.9f, [this](float f) {
            options.zoom.scrollStep = std::round(std::clamp(f, 0.0f, 1.0f) * 90.0f) / 100.0f;
        });
        m.Slider("Zoom Smoothing", std::to_string(static_cast<int>(std::lround(z.smoothing))),
                 z.smoothing / 30.0f,
                 [this](float f) { options.zoom.smoothing = std::round(std::clamp(f, 0.0f, 1.0f) * 30.0f); });
        m.Toggle("Zoom Reset On Press", options.zoom.resetOnPress);
        m.Toggle("Cinematic Camera", options.zoom.cinematic);
        m.Toggle("Scale Sensitivity", options.zoom.scaleSensitivity);
        {
            OptRow h{"Advanced", "", -1.0f, [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        }
        {
            static bool lastSupersampling = false;
            static std::string lastUpscaler = "off";
            if (options.rgss && options.upscaler != "off") {
                if (options.rgss && !lastSupersampling)
                    options.upscaler = "off";
                else
                    options.rgss = false;
            }
            lastSupersampling = options.rgss;
            lastUpscaler = options.upscaler;
        }
        if (options.upscaler == "off")
            m.Toggle(detail::TrOr("options.rgss", "Supersampling"), options.rgss);
        m.Choice("Upscaler", options.upscaler,
                 {{"off", "Off"},
                  {"bilinear", "Bilinear"},
                  {"bicubic", "Bicubic"},
                  {"fsr1", "FSR 1.0"},
                  {"temporal", "Temporal"}});
        if (options.upscaler != "off") {
            m.Slider("Upscale Quality", std::to_string(options.upscaleCustom) + "%",
                     static_cast<float>(options.upscaleCustom - 25) / 75.0f, [this](float f) {
                         options.upscaleCustom =
                             25 + static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 75.0f));
                     });
            m.Slider("Target FPS",
                     options.upscaleTargetFps <= 0 ? detail::TrOr("options.off", "OFF")
                                                   : std::to_string(options.upscaleTargetFps),
                     static_cast<float>(options.upscaleTargetFps) / 360.0f, [this](float f) {
                         const int v = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 72.0f)) * 5;
                         options.upscaleTargetFps = v < 20 ? 0 : v;
                     });
            m.Slider("Upscale Sharpness",
                     options.upscaleSharpness <= 0 ? detail::TrOr("options.off", "OFF")
                                                   : std::to_string(options.upscaleSharpness) + "%",
                     static_cast<float>(options.upscaleSharpness) / 100.0f, [this](float f) {
                         options.upscaleSharpness =
                             static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 100.0f));
                     });
        }
        {
            const bool autoGen = options.frameGen == "auto";
            const int gen = autoGen                    ? 5
                            : options.frameGen == "4x" ? 4
                            : options.frameGen == "3x" ? 3
                            : options.frameGen == "2x" ? 2
                                                       : 1;
            const std::string label = autoGen ? "Auto (" +
                                                    (fgActive > 1 ? std::to_string(fgActive) + "x"
                                                                  : detail::TrOr("options.off", "OFF")) +
                                                    ")"
                                      : gen == 1 ? detail::TrOr("options.off", "OFF")
                                                 : std::to_string(gen) + "x";
            m.Slider("Frame Generation", label, static_cast<float>(gen - 1) / 4.0f, [this](float f) {
                const int g = 1 + static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 4.0f));
                options.frameGen = g == 5 ? "auto" : g == 4 ? "4x" : g == 3 ? "3x" : g == 2 ? "2x" : "off";
            });
        }
        {
            OptRow h{"Developer", "", -1.0f, [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        }
        m.Toggle("Hot Reload On Save", options.hotReload);
    }

    void Perlin::BuildResourcePacksOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("resourcePack.title", "Resource Packs");
        const std::string dir = paths::DataDir() + "/texturepacks";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        {
            OptRow h{"Active (top wins)", "", -1.0f, [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        }
        if (options.texturePacks.empty())
            m.rows.push_back({"Default textures", "", -1.0f, [](float, bool) {}});
        for (size_t i = 0; i < options.texturePacks.size(); i++) {
            const std::string name = options.texturePacks[i];
            m.rows.push_back({"Raise: " + name, "", -1.0f, [this, i](float, bool click) {
                                  if (click && i > 0) {
                                      std::swap(options.texturePacks[i], options.texturePacks[i - 1]);
                                      options.Save();
                                      ApplyTexturePacks();
                                  }
                              }});
            m.rows.push_back({"Remove: " + name, "", -1.0f, [this, i](float, bool click) {
                                  if (click && i < options.texturePacks.size()) {
                                      options.texturePacks.erase(options.texturePacks.begin() +
                                                                 static_cast<long>(i));
                                      options.Save();
                                      ApplyTexturePacks();
                                  }
                              }});
        }
        {
            OptRow h{"Available", "", -1.0f, [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        }
        std::vector<std::string> found;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            const std::string ext = entry.path().extension().string();
            if (entry.is_directory(ec) || ext == ".zip" || ext == ".mcpack")
                found.push_back(entry.path().filename().string());
        }
        std::sort(found.begin(), found.end());
        for (const std::string& name : found) {
            if (std::find(options.texturePacks.begin(), options.texturePacks.end(), name) !=
                options.texturePacks.end())
                continue;
            m.rows.push_back({name, "Add", -1.0f, [this, name](float, bool click) {
                                  if (click) {
                                      options.texturePacks.insert(options.texturePacks.begin(), name);
                                      options.Save();
                                      ApplyTexturePacks();
                                  }
                              }});
        }
        m.rows.push_back({"Folder", dir, -1.0f, [](float, bool) {}});
    }

    void Perlin::BuildShaderPacksOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("perlin.shaderPacks", "Shader Packs");
        const std::string dir = paths::DataDir() + "/shaderpacks";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        m.rows.push_back({"Built-in Shaders", options.shaderPack.empty() ? "Active" : "Use", -1.0f,
                          [this](float, bool click) {
                              if (click) {
                                  options.shaderPack.clear();
                                  options.Save();
                                  ApplyShaderPack();
                              }
                          }});
        std::vector<std::string> found;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            const std::string ext = entry.path().extension().string();
            if (entry.is_directory(ec) || ext == ".zip")
                found.push_back(entry.path().filename().string());
        }
        std::sort(found.begin(), found.end());
        for (const std::string& name : found)
            m.rows.push_back(
                {name, options.shaderPack == name ? "Active" : "Use", -1.0f, [this, name](float, bool click) {
                     if (click) {
                         options.shaderPack = name;
                         options.Save();
                         ApplyShaderPack();
                     }
                 }});
        m.rows.push_back({"Folder", dir, -1.0f, [](float, bool) {}});
    }

    void Perlin::BuildSoundOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("options.sounds.title", "Music & Sounds");
        m.Volume("soundCategory.master", "Master Volume", options.sound.master);
        m.Volume("soundCategory.music", "Music", options.sound.music);
        m.Volume("soundCategory.record", "Jukebox/Note Blocks", options.sound.record);
        m.Volume("soundCategory.weather", "Weather", options.sound.weather);
        m.Volume("soundCategory.block", "Blocks", options.sound.block);
        m.Volume("soundCategory.hostile", "Hostile Creatures", options.sound.hostile);
        m.Volume("soundCategory.neutral", "Friendly Creatures", options.sound.neutral);
        m.Volume("soundCategory.player", "Players", options.sound.player);
        m.Volume("soundCategory.ambient", "Ambient/Environment", options.sound.ambient);
        m.Volume("soundCategory.voice", "Voice/Speech", options.sound.voice);
        m.Toggle(detail::TrOr("options.showSubtitles", "Show Subtitles"), options.sound.subtitles);
    }

    void Perlin::BuildControlsOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("options.controls", "Controls");
        static const char* kTabs[3] = {"Keyboard", "Controller", "Touch"};
        for (int t = 0; t < 3; t++) {
            OptRow tab{kTabs[t], "", -1.0f, [this, t](float, bool click) {
                           if (click && optionsUi.controlsTab != t) {
                               optionsUi.controlsTab = t;
                               optionsUi.signature.clear();
                           }
                       }};
            tab.tab = t + 1;
            tab.locked = optionsUi.controlsTab == t;
            m.rows.push_back(tab);
        }
        const auto bindsRow = [&](const std::string& label, int device) {
            m.rows.push_back({label, "", -1.0f, [this, device](float, bool click) {
                                  if (click) {
                                      optionsUi.bindDevice = device;
                                      optionsUi.page = 5;
                                      optionsUi.sub = 0;
                                      optionsUi.signature.clear();
                                      optionsUi.bindCapture = -1;
                                  }
                              }});
        };
        if (optionsUi.controlsTab == 0) {
            m.Slider(detail::TrOr("options.sensitivity", "Sensitivity"),
                     Percent(options.mouseSensitivity / 0.15f), options.mouseSensitivity / 0.30f,
                     [this](float f) {
                         options.mouseSensitivity =
                             std::round(std::clamp(f, 0.0f, 1.0f) * 200.0f) / 100.0f * 0.15f;
                     });
            m.Toggle(detail::TrOr("options.invertMouse", "Invert Mouse"), options.invertMouse);
            m.Toggle(detail::TrOr("perlin.options.centerCursor", "Center Cursor"), options.centerCursor);
            m.Choice(detail::TrOr("key.sprint", "Sprint"), options.sprintMode,
                     {{"hold", detail::TrOr("options.key.hold", "Hold")},
                      {"toggle", detail::TrOr("options.key.toggle", "Toggle")}});
            m.Choice(detail::TrOr("key.sneak", "Sneak"), options.sneakMode,
                     {{"hold", detail::TrOr("options.key.hold", "Hold")},
                      {"toggle", detail::TrOr("options.key.toggle", "Toggle")}});
            bindsRow(detail::TrOr("controls.keybinds", "Key Binds..."), 0);
        } else if (optionsUi.controlsTab == 1) {
            m.rows.push_back(
                {"Gamepad", input.PadConnected() ? "Connected" : "Not connected", -1.0f, [](float, bool) {}});
            m.rows.back().locked = true;
            m.Slider("Look Sensitivity", Percent(options.padSensitivity), options.padSensitivity / 2.0f,
                     [this](float f) {
                         options.padSensitivity = std::round(std::clamp(f, 0.05f, 1.0f) * 200.0f) / 100.0f;
                     });
            m.Slider("Stick Dead Zone", Percent(options.padDeadzone), options.padDeadzone / 0.6f,
                     [this](float f) {
                         options.padDeadzone = std::round(std::clamp(f, 0.0f, 1.0f) * 60.0f) / 100.0f;
                     });
            m.Toggle("Invert Look Y", options.padInvertY);
            m.Toggle("Vibration", options.padVibration);
            bindsRow("Controller Binds...", 1);
        } else {
            m.Slider("Look Sensitivity", Percent(options.touchSensitivity), options.touchSensitivity / 2.0f,
                     [this](float f) {
                         options.touchSensitivity = std::round(std::clamp(f, 0.05f, 1.0f) * 200.0f) / 100.0f;
                     });
            m.Slider("Button Size", Percent(options.touchScale), (options.touchScale - 0.5f) / 1.3f,
                     [this](float f) {
                         options.touchScale =
                             std::round((0.5f + std::clamp(f, 0.0f, 1.0f) * 1.3f) * 20.0f) / 20.0f;
                     });
            m.Slider("Button Opacity", Percent(options.touchOpacity), (options.touchOpacity - 0.2f) / 0.8f,
                     [this](float f) {
                         options.touchOpacity =
                             std::round((0.2f + std::clamp(f, 0.0f, 1.0f) * 0.8f) * 20.0f) / 20.0f;
                     });
            OptRow edit{"Edit Touch Layout...", "", -1.0f, [this](float, bool click) {
                            if (click) {
                                optionsUi.touchEditing = true;
                                optionsUi.touchEditHidden = false;
                                options.Save();
                                optionsUi.signature.clear();
                            }
                        }};
            edit.wide = true;
            m.rows.push_back(edit);
            OptRow reset{"Reset Touch Layout", "", -1.0f, [this](float, bool click) {
                             if (click) {
                                 options.touchLayout.clear();
                                 options.Save();
                             }
                         }};
            reset.wide = true;
            m.rows.push_back(reset);
        }
    }

    void Perlin::BuildModSettingsOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        auto& mm = mods::ModManager::Instance();
        std::string modTitle = modSettingsFor;
        for (const mods::ModInfo& mi : mm.List())
            if (mi.name == modSettingsFor && !mi.displayName.empty())
                modTitle = mi.displayName;
        m.title = modTitle + " - " + detail::TrOr("perlin.mods.settings", "Settings");
        const std::string owner = modSettingsFor;
        const auto iconOf = [&](const abi::SettingDef& def) -> std::string {
            if (def.icon.starts_with("item:"))
                return def.icon;
            if (def.icon.starts_with("texture:"))
                return def.icon.substr(8);
            if (def.icon.starts_with("file:")) {
                const std::string rel = def.icon.substr(5);
                if (rel.empty() || rel.contains("..") || rel[0] == '/' || rel[0] == '\\')
                    return {};
                const std::string full =
                    (std::filesystem::path(mm.Directory()) / (owner + ".data") / rel).string();
                const std::string key = "modset:" + owner + ":" + rel;
                std::error_code ec;
                const std::string stamp = std::to_string(static_cast<long long>(
                    std::filesystem::last_write_time(full, ec).time_since_epoch().count()));
                std::string& seen = packUi.modIconStamp[key];
                if (seen != stamp) {
                    seen = stamp;
                    Texture t;
                    if (LoadTexture(full, t))
                        ui::UiAssets::Instance().SetHead(key, t);
                }
                return ui::UiAssets::Instance().HasHead(key) ? "mchead:" + key : std::string();
            }
            return {};
        };
        const std::vector<abi::SettingDef> defs = mm.Settings(owner);
        for (const abi::SettingDef& def : defs) {
            const size_t before = m.rows.size();
            const std::string label = def.label.empty() ? def.id : def.label;
            const auto withUnit = [&](std::string v) { return def.unit.empty() ? v : v + " " + def.unit; };
            switch (def.kind) {
            case abi::SettingKind::Header: {
                OptRow h{label, "", -1.0f, [](float, bool) {}};
                h.header = true;
                m.rows.push_back(h);
                break;
            }
            case abi::SettingKind::Label: {
                m.rows.push_back({label, def.value, -1.0f, [](float, bool) {}});
                m.rows.back().locked = true;
                break;
            }
            case abi::SettingKind::Toggle: {
                const bool on = def.value == "true" || def.value == "1";
                m.rows.push_back({label, OnOff(on), -1.0f, [owner, id = def.id, on](float, bool click) {
                                      if (click)
                                          mods::ModManager::Instance().ChangeSetting(owner, id,
                                                                                     on ? "false" : "true");
                                  }});
                break;
            }
            case abi::SettingKind::Integer:
            case abi::SettingKind::Number: {
                const double lo = def.min, hi = std::max(def.max, def.min + 1.0e-9);
                const double cur = std::clamp(std::atof(def.value.c_str()), lo, hi);
                const bool whole = def.kind == abi::SettingKind::Integer;
                char buf[48];
                if (whole)
                    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(std::llround(cur)));
                else
                    std::snprintf(buf, sizeof(buf), def.step > 0 && def.step < 0.095 ? "%.2f" : "%.1f", cur);
                m.rows.push_back({label, withUnit(buf), static_cast<float>((cur - lo) / (hi - lo)),
                                  [owner, id = def.id, lo, hi, whole, step = def.step](float f, bool) {
                                      double v =
                                          lo + std::clamp(static_cast<double>(f), 0.0, 1.0) * (hi - lo);
                                      if (step > 0)
                                          v = lo + std::round((v - lo) / step) * step;
                                      if (whole)
                                          v = std::round(v);
                                      v = std::clamp(v, lo, hi);
                                      char out[48];
                                      std::snprintf(out, sizeof(out), whole ? "%.0f" : "%.6g", v);
                                      mods::ModManager::Instance().ChangeSetting(owner, id, out);
                                  }});
                break;
            }
            case abi::SettingKind::Choice: {
                size_t at = 0;
                for (size_t i = 0; i < def.choices.size(); i++)
                    if (def.choices[i] == def.value)
                        at = i;
                const std::string shown = at < def.choiceLabels.size() && !def.choiceLabels[at].empty()
                                              ? def.choiceLabels[at]
                                              : def.value;
                m.rows.push_back({label, shown, -1.0f,
                                  [owner, id = def.id,
                                   next = def.choices.empty() ? std::string()
                                                              : def.choices[(at + 1) % def.choices.size()]](
                                      float, bool click) {
                                      if (click)
                                          mods::ModManager::Instance().ChangeSetting(owner, id, next);
                                  }});
                break;
            }
            case abi::SettingKind::Button:
                m.rows.push_back({label, "", -1.0f, [owner, id = def.id](float, bool click) {
                                      if (click)
                                          mods::ModManager::Instance().ChangeSetting(owner, id, "pressed");
                                  }});
                break;
            }
            if (m.rows.size() > before && !m.rows.back().header) {
                m.rows.back().icon = iconOf(def);
                packUi.modRowTips.resize(m.rows.size());
                packUi.modRowTips[m.rows.size() - 1] = def.description;
            }
        }
        if (defs.empty()) {
            OptRow h{detail::TrOr("perlin.mods.noSettings", "This mod has no settings"), "", -1.0f,
                     [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        } else {
            m.rows.push_back({detail::TrOr("perlin.mods.resetSettings", "Reset to Defaults"), "", -1.0f,
                              [owner](float, bool click) {
                                  if (click)
                                      mods::ModManager::Instance().ResetSettings(owner);
                              }});
        }
    }

    void Perlin::BuildJavaShaderSettingsOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("perlin.shaders.settings.title", "Shader Pack Settings");
        static std::map<std::string, std::string> javaEdits;
        static std::string javaEditsPack;
        static bool javaDirty = false;
        if (javaEditsPack != options.shaderPack) {
            javaEditsPack = options.shaderPack;
            javaEdits.clear();
            javaDirty = false;
        }
        const std::string packPath = paths::DataDir() + "/shaderpacks/" + options.shaderPack;
        m.rows.push_back({javaDirty ? "Apply Changes (reloads shaders)" : "Reload Shaders", "", -1.0f,
                          [this, packPath](float, bool click) {
                              if (!click)
                                  return;
                              javaDirty = false;
                              javaPackPending = packPath;
                          }});
        m.rows.push_back({"Reset All to Defaults", "", -1.0f, [this, packPath](float, bool click) {
                              if (!click)
                                  return;
                              javaEdits.clear();
                              jshader::WriteJavaPackOptions(packPath, {});
                              javaDirty = true;
                              javaPackPending = packPath;
                          }});
        std::string lastGroup = "\x01";
        for (const JavaPackOption& o : renderer->JavaShaderPackOptions()) {
            if (o.group != lastGroup) {
                lastGroup = o.group;
                OptRow h{o.group.empty() ? std::string("Shader Options") : o.group, "", -1.0f,
                         [](float, bool) {}};
                h.header = true;
                m.rows.push_back(h);
            }
            const auto edited = javaEdits.find(o.name);
            const std::string current = edited != javaEdits.end() ? edited->second : o.current;
            std::string shown = current;
            if (o.toggle)
                shown =
                    current == "true" ? detail::TrOr("options.on", "ON") : detail::TrOr("options.off", "OFF");
            m.rows.push_back({o.label, shown, -1.0f, [this, o, current, packPath](float, bool click) {
                                  if (!click || o.values.empty())
                                      return;
                                  size_t at = 0;
                                  for (size_t i = 0; i < o.values.size(); i++)
                                      if (o.values[i] == current)
                                          at = i;
                                  javaEdits[o.name] = o.values[(at + 1) % o.values.size()];
                                  auto all = jshader::ReadJavaPackOptions(packPath);
                                  for (const auto& kv : javaEdits)
                                      all[kv.first] = kv.second;
                                  jshader::WriteJavaPackOptions(packPath, all);
                                  javaDirty = true;
                              }});
            if (!o.comment.empty()) {
                packUi.modRowTips.resize(m.rows.size());
                packUi.modRowTips[m.rows.size() - 1] = o.comment;
            }
        }
    }

    void Perlin::BuildShaderSettingsOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("perlin.shaders.settings.title", "Shader Pack Settings");
        const std::vector<ShaderSetting>& list = detail::PackCache().Settings(options.shaderPack);
        const std::vector<ShaderPreset>& presets = detail::PackCache().Presets(options.shaderPack);
        const auto settingValue = [&](const ShaderSetting& st) {
            const auto found = options.shaderSettings.find(st.id);
            return found == options.shaderSettings.end() ? st.def : std::clamp(found->second, st.min, st.max);
        };
        const auto engineValue = [&](const std::string& key) -> double {
            if (key == "shadowQuality")
                return options.shadowQuality;
            if (key == "shadowDistance")
                return options.shadowDistance;
            if (key == "shadowEntities")
                return options.shadowEntities ? 1.0 : 0.0;
            if (key == "bloom")
                return options.bloom;
            return 0.0;
        };
        if (!presets.empty()) {
            int current = -1;
            for (size_t p = 0; p < presets.size() && current < 0; p++) {
                bool match = true;
                for (const auto& [key, v] : presets[p].engine)
                    match = match && std::fabs(engineValue(key) - v) < 0.01;
                for (const auto& [key, v] : presets[p].settings)
                    for (const ShaderSetting& st : list)
                        if (st.id == key)
                            match =
                                match && std::fabs(settingValue(st) - std::clamp(v, st.min, st.max)) < 0.01;
                if (match)
                    current = static_cast<int>(p);
            }
            m.rows.push_back(
                {detail::TrOr("perlin.shaders.preset", "Preset"),
                 current < 0 ? detail::TrOr("perlin.shaders.preset.custom", "Custom") : presets[current].name,
                 -1.0f, [this, &presets, current](float, bool click) {
                     if (!click || presets.empty())
                         return;
                     const ShaderPreset& p = presets[(current + 1) % static_cast<int>(presets.size())];
                     for (const auto& [key, v] : p.settings)
                         options.shaderSettings[key] = v;
                     for (const auto& [key, v] : p.engine) {
                         if (key == "shadowQuality")
                             options.shadowQuality = std::clamp(static_cast<int>(std::lround(v)), 0, 4);
                         else if (key == "shadowDistance")
                             options.shadowDistance = std::clamp(static_cast<int>(std::lround(v)), 0, 256);
                         else if (key == "shadowEntities")
                             options.shadowEntities = v >= 0.5;
                         else if (key == "bloom")
                             options.bloom = std::clamp(static_cast<float>(v), 0.0f, 1.0f);
                     }
                 }});
        }
        if (!list.empty()) {
            OptRow h{detail::TrOr("perlin.shaders.settings.engine", "Rendering"), "", -1.0f,
                     [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
            static const char* const kQuality[5] = {"Off", "Low", "Medium", "High", "Ultra"};
            m.Slider("Shadow Quality", kQuality[std::clamp(options.shadowQuality, 0, 4)],
                     static_cast<float>(options.shadowQuality) / 4.0f, [this](float f) {
                         options.shadowQuality =
                             static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 4.0f));
                     });
            m.Slider("Shadow Distance",
                     options.shadowDistance == 0 ? std::string("Auto")
                                                 : std::to_string(options.shadowDistance),
                     static_cast<float>(options.shadowDistance) / 256.0f, [this](float f) {
                         options.shadowDistance =
                             static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 64.0f)) * 4;
                     });
            m.Toggle("Mob & Player Shadows", options.shadowEntities);
            m.Slider("Bloom",
                     options.bloom <= 0.001f ? detail::TrOr("options.off", "OFF") : Percent(options.bloom),
                     options.bloom, [this](float f) { options.bloom = Snap(f, 20.0f); });
        }
        std::string lastGroup;
        for (const ShaderSetting& st : list) {
            if (!st.group.empty() && st.group != lastGroup) {
                lastGroup = st.group;
                OptRow h{st.group, "", -1.0f, [](float, bool) {}};
                h.header = true;
                m.rows.push_back(h);
            }
            const double value = settingValue(st);
            if (st.toggle) {
                m.rows.push_back(
                    {st.label,
                     value >= 0.5 ? detail::TrOr("options.on", "ON") : detail::TrOr("options.off", "OFF"),
                     -1.0f, [this, id = st.id, value](float, bool click) {
                         if (click)
                             options.shaderSettings[id] = value >= 0.5 ? 0.0 : 1.0;
                     }});
                continue;
            }
            char buf[48];
            const long stepIndex = std::lround((value - st.min) / st.step);
            if (!st.labels.empty() && stepIndex >= 0 && stepIndex < static_cast<long>(st.labels.size()))
                std::snprintf(buf, sizeof(buf), "%s", st.labels[static_cast<size_t>(stepIndex)].c_str());
            else
                std::snprintf(buf, sizeof(buf),
                              st.step < 0.095  ? "%.2f"
                              : st.step < 0.95 ? "%.1f"
                                               : "%.0f",
                              value);
            m.Slider(st.label, buf, static_cast<float>((value - st.min) / std::max(1.0e-9, st.max - st.min)),
                     [this, st](float f) {
                         const double raw =
                             st.min + std::clamp(static_cast<double>(f), 0.0, 1.0) * (st.max - st.min);
                         options.shaderSettings[st.id] = std::clamp(
                             st.min + std::round((raw - st.min) / st.step) * st.step, st.min, st.max);
                     });
        }
        if (list.empty()) {
            OptRow h{detail::TrOr("perlin.shaders.settings.none", "This shader pack has no settings"), "",
                     -1.0f, [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        }
    }

    void Perlin::BuildSkinOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("options.skinCustomisation.title", "Skin Customization");
        m.Toggle(detail::TrOr("options.modelPart.cape", "Cape"), options.skinParts.cape);
        m.Choice("Cape Physics", options.capePhysics,
                 {{"noshoulder", "No Shoulder"}, {"shoulder", "Shoulder"}, {"off", "Off"}});
        m.Toggle(detail::TrOr("options.modelPart.jacket", "Jacket"), options.skinParts.jacket);
        m.Toggle(detail::TrOr("options.modelPart.left_sleeve", "Left Sleeve"), options.skinParts.leftSleeve);
        m.Toggle(detail::TrOr("options.modelPart.right_sleeve", "Right Sleeve"),
                 options.skinParts.rightSleeve);
        m.Toggle(detail::TrOr("options.modelPart.left_pants_leg", "Left Pants Leg"),
                 options.skinParts.leftPants);
        m.Toggle(detail::TrOr("options.modelPart.right_pants_leg", "Right Pants Leg"),
                 options.skinParts.rightPants);
        m.Toggle(detail::TrOr("options.modelPart.hat", "Hat"), options.skinParts.hat);
        m.Toggle("3D Skins", options.skins3d);
        m.Toggle("Bedrock Bridging", options.bedrockBridging);
        m.rows.push_back(
            {detail::TrOr("perlin.wardrobe", "Wardrobe..."), "", -1.0f, [this, &hud](float, bool click) {
                 if (click) {
                     options.Save();
                     pause.view = 5;
                     optionsUi.signature.clear();
                     hud.Overlay().ClearOverlay();
                 }
             }});
    }

    void Perlin::BuildHandOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = detail::TrOr("perlin.options.hand", "Hand & Camera");
        m.Toggle(detail::TrOr("options.showHand", "Show Hand"), options.hand.show);
        m.Toggle(detail::TrOr("options.viewBobbing", "View Bobbing"), options.hand.viewBobbing);
        m.Toggle(detail::TrOr("perlin.options.bobCamera", "Camera Bobbing"), options.hand.bobCamera);
        m.Toggle(detail::TrOr("perlin.options.bobHand", "Hand Bobbing"), options.hand.bobHand);
        m.Toggle(detail::TrOr("perlin.options.swing", "Swing Animation"), options.hand.swing);
        m.Toggle(detail::TrOr("perlin.options.equip", "Equip Animation"), options.hand.equipAnimation);
        m.Toggle(detail::TrOr("perlin.options.sway", "Hand Sway"), options.hand.sway);
        m.Toggle(detail::TrOr("options.mainHand.left", "Left Handed"), options.hand.leftHanded);
        m.Toggle(detail::TrOr("perlin.options.offhand", "Show Offhand"), options.hand.showOffhand);
        m.Toggle(detail::TrOr("perlin.options.thirdCrosshair", "Third Person Crosshair"),
                 options.thirdPersonCrosshair);
        m.Toggle(detail::TrOr("perlin.options.thirdNametag", "Third Person Nametag"),
                 options.thirdPersonNametag);
        m.Slider(detail::TrOr("perlin.options.handScale", "Hand Size"), Percent(options.hand.scale),
                 (options.hand.scale - 0.5f) / 1.5f, [this](float f) {
                     options.hand.scale = 0.5f + std::round(std::clamp(f, 0.0f, 1.0f) * 150.0f) / 100.0f;
                 });
        const auto fieldSlider = [&](const std::string& label, float& ref, float lo, float hi, float step,
                                     const char* unit) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), step < 0.5f ? "%.2f%s" : "%.0f%s", ref, unit);
            m.Slider(label, buf, (ref - lo) / (hi - lo), [&ref, lo, hi, step](float f) {
                ref = std::round((lo + std::clamp(f, 0.0f, 1.0f) * (hi - lo)) / step) * step;
                if (std::fabs(ref) < step * 0.5f && lo < 0.0f)
                    ref = 0.0f;
            });
        };
        {
            OptRow h{detail::TrOr("perlin.options.handCustom", "Hand Position"), "", -1.0f,
                     [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        }
        auto& hs = options.hand;
        fieldSlider("Hand X", hs.armX, -1.0f, 1.0f, 0.01f, "");
        fieldSlider("Hand Y", hs.armY, -1.0f, 1.0f, 0.01f, "");
        fieldSlider("Hand Z", hs.armZ, -1.0f, 1.0f, 0.01f, "");
        fieldSlider("Hand Pitch", hs.armRotX, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
        fieldSlider("Hand Yaw", hs.armRotY, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
        fieldSlider("Hand Roll", hs.armRotZ, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
        fieldSlider("Hand Scale", hs.armScale, 0.25f, 2.0f, 0.01f, "");
        {
            OptRow h{detail::TrOr("perlin.options.itemCustom", "Held Item Position"), "", -1.0f,
                     [](float, bool) {}};
            h.header = true;
            m.rows.push_back(h);
        }
        fieldSlider("Item X", hs.itemX, -1.0f, 1.0f, 0.01f, "");
        fieldSlider("Item Y", hs.itemY, -1.0f, 1.0f, 0.01f, "");
        fieldSlider("Item Z", hs.itemZ, -1.0f, 1.0f, 0.01f, "");
        fieldSlider("Item Pitch", hs.itemRotX, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
        fieldSlider("Item Yaw", hs.itemRotY, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
        fieldSlider("Item Roll", hs.itemRotZ, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
        fieldSlider("Item Scale", hs.itemScale, 0.25f, 2.0f, 0.01f, "");
        m.rows.push_back({detail::TrOr("perlin.options.handReset", "Reset Hand Position"), "", -1.0f,
                          [this](float, bool click) {
                              if (click) {
                                  HandSettings d;
                                  auto& h = options.hand;
                                  h.armX = d.armX;
                                  h.armY = d.armY;
                                  h.armZ = d.armZ;
                                  h.armRotX = d.armRotX;
                                  h.armRotY = d.armRotY;
                                  h.armRotZ = d.armRotZ;
                                  h.armScale = d.armScale;
                                  h.itemX = d.itemX;
                                  h.itemY = d.itemY;
                                  h.itemZ = d.itemZ;
                                  h.itemRotX = d.itemRotX;
                                  h.itemRotY = d.itemRotY;
                                  h.itemRotZ = d.itemRotZ;
                                  h.itemScale = d.itemScale;
                                  options.Save();
                              }
                          }});
    }

    void Perlin::BuildKeyBindsOptions(OptionsModel& m, PauseFrame& f) {
        [[maybe_unused]] auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down,
                                clickReady] = f;
        m.title = optionsUi.bindDevice == 1 ? std::string("Controller Binds")
                                            : detail::TrOr("controls.keybinds.title", "Key Binds");
        static const std::pair<Action, const char*> kBinds[] = {{Action::Forward, "Forward"},
                                                                {Action::Left, "Left"},
                                                                {Action::Back, "Back"},
                                                                {Action::Right, "Right"},
                                                                {Action::Jump, "Jump"},
                                                                {Action::Sneak, "Sneak"},
                                                                {Action::Sprint, "Sprint"},
                                                                {Action::Attack, "Attack"},
                                                                {Action::Use, "Use Item"},
                                                                {Action::PickBlock, "Pick Block"},
                                                                {Action::Inventory, "Inventory"},
                                                                {Action::Drop, "Drop Item"},
                                                                {Action::Hotbar1, "Hotbar 1"},
                                                                {Action::Hotbar2, "Hotbar 2"},
                                                                {Action::Hotbar3, "Hotbar 3"},
                                                                {Action::Hotbar4, "Hotbar 4"},
                                                                {Action::Hotbar5, "Hotbar 5"},
                                                                {Action::Hotbar6, "Hotbar 6"},
                                                                {Action::Hotbar7, "Hotbar 7"},
                                                                {Action::Hotbar8, "Hotbar 8"},
                                                                {Action::Hotbar9, "Hotbar 9"},
                                                                {Action::Chat, "Chat"},
                                                                {Action::Command, "Command"},
                                                                {Action::PlayerList, "Player List"},
                                                                {Action::Screenshot, "Screenshot"},
                                                                {Action::CyclePerspective, "Perspective"},
                                                                {Action::ToggleHud, "Hide GUI"},
                                                                {Action::DebugScreen, "Debug Screen"},
                                                                {Action::Zoom, "Zoom"}};
        constexpr int kPerPage = 12;
        const int total = static_cast<int>(std::size(kBinds));
        const int pages = (total + kPerPage - 1) / kPerPage;
        optionsUi.sub = std::clamp(optionsUi.sub, 0, pages - 1);
        for (int i = optionsUi.sub * kPerPage; i < std::min(total, (optionsUi.sub + 1) * kPerPage); i++) {
            const Action a = kBinds[i].first;
            const std::string name = kBinds[i].second;
            const bool waiting = optionsUi.bindCapture == static_cast<int>(a);
            std::string value = waiting ? "> ? <" : BindingOf(a).Serialize();
            for (const char* prefix : {"key.keyboard.", "key.mouse.", "pad."}) {
                const std::string pre = prefix;
                if (value.starts_with(pre))
                    value = value.substr(pre.size());
            }
            if (value == "left" || value == "right" || value == "middle")
                value += " button";
            if (!waiting && !value.empty()) {
                bool word = true;
                for (char& c : value) {
                    if (word && c >= 'a' && c <= 'z')
                        c = static_cast<char>(c - 32);
                    word = c == ' ' || c == '.' || c == '_';
                    if (c == '_')
                        c = ' ';
                }
            }
            if (value.empty())
                value = "Not bound";
            m.rows.push_back({name, value, -1.0f, [this, a](float, bool click) {
                                  if (click) {
                                      optionsUi.bindCapture = static_cast<int>(a);
                                      optionsUi.bindStartedAt = Clock::now();
                                      optionsUi.signature.clear();
                                  }
                              }});
        }
        m.rows.push_back(
            {detail::TrOr("controls.resetAll", "Reset Keys"), "", -1.0f, [this](float, bool click) {
                 if (click) {
                     if (optionsUi.bindDevice == 1)
                         options.padKeys = DefaultPadBindings();
                     else
                         options.keys = DefaultBindings();
                     optionsUi.bindCapture = -1;
                 }
             }});
        m.rows.push_back({detail::TrOr("perlin.options.page", "Page") + " " +
                              std::to_string(optionsUi.sub + 1) + "/" + std::to_string(pages),
                          "", -1.0f, [this, pages](float, bool click) {
                              if (click) {
                                  optionsUi.sub = (optionsUi.sub + 1) % pages;
                                  optionsUi.bindCapture = -1;
                                  optionsUi.signature.clear();
                              }
                          }});
    }
}
