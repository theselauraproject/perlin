#include "client/PerlinPause.hpp"
#include "util/Utf8.hpp"
#include "util/Log.hpp"

namespace perlin {
    void Perlin::UpdatePackMenu(PauseFrame& f) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        const bool click = down && !pause.leftDown && clickReady;
        const float wheel = inputManager.Scroll() * 20.0f;
        const auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return s;
        };
        const auto openFolder = [&](const std::string& dir) {
            if (platform->PickFile("pack"))
                return;
            std::error_code e;
            std::filesystem::create_directories(dir, e);
            platform->OpenUrl("file://" + std::filesystem::absolute(dir, e).generic_string());
        };
        const auto leave = [&]() {
            CommitTexturePacks();
            options.Save();
            packUi.shaderPendingInit = false;
            if (pause.view == 4)
                pause.view = 0;
            else
                optionsUi.page = 0;
            optionsUi.signature.clear();
            pause.signature.clear();
            hud.Overlay().ClearOverlay();
        };
        const auto submit = [&](const std::string& rml, const char* label) {
            std::string error;
            if (!hud.Overlay().SetOverlay(rml, error))
                Log("ui", "{} screen: {}", label, error);
        };
        const auto listPacks = [&](const std::string& dir, bool allowMcpack) {
            if (now - packUi.foundAt < std::chrono::milliseconds(1000) && packUi.metaStampDir == dir)
                return;
            packUi.foundAt = now;
            packUi.metaStampDir = dir;
            packUi.foundCache.clear();
            std::error_code e;
            std::filesystem::create_directories(dir, e);
            for (const auto& entry : std::filesystem::directory_iterator(dir, e)) {
                const std::string ext = lower(entry.path().extension().string());
                if (entry.is_directory(e) || ext == ".zip" || (allowMcpack && ext == ".mcpack"))
                    packUi.foundCache.push_back(entry.path().filename().string());
            }
            std::sort(packUi.foundCache.begin(), packUi.foundCache.end());
        };
        const auto packMeta = [&](const std::string& dir, const std::string& name) -> const PackMeta& {
            const std::string path = dir + "/" + name;
            std::error_code e;
            std::string stamp =
                std::to_string(std::filesystem::is_directory(path, e) ? 0
                                                                      : std::filesystem::file_size(path, e)) +
                ":" +
                std::to_string(static_cast<long long>(
                    std::filesystem::last_write_time(path, e).time_since_epoch().count()));
            auto& slot = packUi.metaCache[path];
            if (slot.stamp != stamp) {
                slot.stamp = stamp;
                slot.meta = ReadPackMeta(path);
                if (slot.meta.hasIcon)
                    ui::UiAssets::Instance().SetHead("pack:" + path, slot.meta.icon);
            }
            return slot.meta;
        };
        const auto packIcon = [&](const std::string& dir, const std::string& name, const PackMeta& meta,
                                  const char* fallback) {
            return meta.hasIcon ? "mchead:pack:" + dir + "/" + name : std::string(fallback);
        };
        if (pause.view == 1 && optionsUi.page == 7) {
            const std::string dir = paths::DataDir() + "/texturepacks";
            listPacks(dir, true);
            if (!packUi.pendingInit) {
                packUi.pendingInit = true;
                packUi.pending = options.texturePacks;
            }
            std::vector<std::string> existing;
            for (const std::string& name : packUi.pending)
                if (std::find(packUi.foundCache.begin(), packUi.foundCache.end(), name) !=
                    packUi.foundCache.end())
                    existing.push_back(name);
            packUi.pending = existing;
            std::vector<ui::PackEntry> avail, sel;
            std::vector<std::string> availNames;
            for (size_t i = 0; i < packUi.pending.size(); i++) {
                const std::string& name = packUi.pending[i];
                const PackMeta& m = packMeta(dir, name);
                ui::PackEntry e;
                e.name = EnsureUtf8(m.name.empty() ? name : m.name);
                e.desc = m.description;
                e.icon = packIcon(dir, name, m, "textures/blocks/stone");
                e.canUnselect = true;
                e.canUp = i > 0;
                e.canDown = true;
                sel.push_back(e);
            }
            {
                ui::PackEntry e;
                e.name = detail::TrOr("perlin.packs.defaultName", "Default");
                e.desc = detail::TrOr("perlin.packs.defaultDesc",
                                      "The default look and feel of Minecraft (built-in)");
                e.icon = "textures/blocks/grass_side_carried";
                sel.push_back(e);
            }
            for (const std::string& name : packUi.foundCache) {
                if (std::find(packUi.pending.begin(), packUi.pending.end(), name) != packUi.pending.end())
                    continue;
                const PackMeta& m = packMeta(dir, name);
                ui::PackEntry e;
                e.name = EnsureUtf8(m.name.empty() ? name : m.name);
                e.desc = m.description;
                e.icon = packIcon(dir, name, m, "textures/blocks/stone");
                e.canSelect = true;
                avail.push_back(e);
                availNames.push_back(name);
            }
            const ui::PackScreenLayout l = ui::ComputePackScreenLayout(
                static_cast<int>(avail.size()), static_cast<int>(sel.size()), guiW, guiH);
            const bool overLeft = inside(l.leftX, l.top, l.panelW, l.bottom - l.top);
            const bool overRight = inside(l.rightX, l.top, l.panelW, l.bottom - l.top);
            if (overLeft)
                packUi.scrollActive -= wheel;
            else if (overRight)
                packUi.scrollAvailable -= wheel;
            packUi.scrollActive = std::clamp(packUi.scrollActive, 0.0f, l.maxScrollA);
            packUi.scrollAvailable = std::clamp(packUi.scrollAvailable, 0.0f, l.maxScrollB);
            std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "p" +
                                    std::to_string(static_cast<int>(packUi.scrollActive)) + "," +
                                    std::to_string(static_cast<int>(packUi.scrollAvailable));
            for (const auto& e : avail)
                signature += "|" + e.name + e.desc + e.icon;
            signature += "#";
            for (const auto& e : sel)
                signature += "|" + e.name + e.desc + e.icon + (e.canUp ? "u" : "");
            if (signature != optionsUi.signature || !hud.Overlay().HasOverlay()) {
                optionsUi.signature = signature;
                submit(
                    ui::BuildPackScreenRml(avail, sel, detail::TrOr("perlin.packs.title", "Resource Packs"),
                                           detail::TrOr("perlin.packs.hint",
                                                        "Drag and drop files into this window to add packs"),
                                           detail::TrOr("perlin.packs.available", "Available"),
                                           detail::TrOr("perlin.packs.selected", "Selected"),
                                           detail::TrOr("perlin.packs.openFolder", "Open Pack Folder"),
                                           detail::TrOr("gui.done", "Done"), packUi.scrollActive,
                                           packUi.scrollAvailable, guiW, guiH),
                    "resource packs");
            }
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            if (click) {
                const float sbA = l.maxScrollA > 0 ? 6.0f : 0.0f, sbS = l.maxScrollB > 0 ? 6.0f : 0.0f;
                bool changed = false;
                if (inside(l.openX, l.btnY, l.btnW, 20.0f)) {
                    openFolder(dir);
                } else if (inside(l.doneX, l.btnY, l.btnW, 20.0f)) {
                    leave();
                    pause.leftDown = down;
                    return;
                } else if (gy >= l.top && gy < l.bottom) {
                    for (int i = 0; i < static_cast<int>(packUi.pending.size()) && !changed; i++) {
                        const float y = l.RowY(i, packUi.scrollAvailable);
                        const float ix = l.rightX + 4.0f, iy = y + 1.0f;
                        if (!inside(ix, iy, 32.0f, 32.0f) ||
                            !inside(l.rightX, l.top, l.panelW - sbS, l.bottom - l.top))
                            continue;
                        const float jx = gx - ix, jy = gy - iy;
                        if (jx < 16.0f) {
                            packUi.pending.erase(packUi.pending.begin() + i);
                        } else if (jy < 16.0f) {
                            if (i > 0)
                                std::swap(packUi.pending[static_cast<size_t>(i)],
                                          packUi.pending[static_cast<size_t>(i - 1)]);
                        } else if (i + 1 < static_cast<int>(packUi.pending.size())) {
                            std::swap(packUi.pending[static_cast<size_t>(i)],
                                      packUi.pending[static_cast<size_t>(i + 1)]);
                        }
                        changed = true;
                    }
                    for (int i = 0; i < static_cast<int>(avail.size()) && !changed; i++)
                        if (inside(l.leftX + 4.0f, l.RowY(i, packUi.scrollActive) + 1.0f, 32.0f, 32.0f) &&
                            inside(l.leftX, l.top, l.panelW - sbA, l.bottom - l.top)) {
                            packUi.pending.insert(packUi.pending.begin(), availNames[static_cast<size_t>(i)]);
                            changed = true;
                        }
                }
                if (changed)
                    optionsUi.signature.clear();
            }
            pause.leftDown = down;
            return;
        }
        if (pause.view == 1 && optionsUi.page == 6) {
            const auto langs = i18n::Lang::Get().Languages();
            std::vector<std::string> rows;
            int selectedRow = -1;
            for (size_t i = 0; i < langs.size(); i++) {
                rows.push_back(langs[i].name.empty() ? langs[i].code : langs[i].name);
                if (langs[i].code == options.language)
                    selectedRow = static_cast<int>(i);
            }
            const ui::ShaderScreenLayout l =
                ui::ComputeShaderScreenLayout(static_cast<int>(rows.size()), guiW, guiH);
            packUi.shaderScroll = std::clamp(packUi.shaderScroll - wheel, 0.0f, l.maxScroll);
            std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "l" +
                                    std::to_string(static_cast<int>(packUi.shaderScroll)) + "|" +
                                    std::to_string(selectedRow) + "|" +
                                    std::to_string(ui::McFont::Instance().Version()) + "|";
            for (const auto& r : rows)
                signature += r + ";";
            if (signature != optionsUi.signature || !hud.Overlay().HasOverlay()) {
                optionsUi.signature = signature;
                submit(ui::BuildLanguageScreenRml(
                           rows, selectedRow, detail::TrOr("options.language", "Language"),
                           detail::TrOr("perlin.language.subtitle",
                                        "Language translations may not be 100% accurate"),
                           detail::TrOr("gui.done", "Done"), packUi.shaderScroll, guiW, guiH),
                       "language");
            }
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            if (click) {
                const float doneX = l.colX + (l.colW - 200.0f) / 2.0f;
                if (inside(doneX, l.row1Y + 12.0f, 200.0f, 20.0f)) {
                    leave();
                    pause.leftDown = down;
                    return;
                }
                if (gy >= l.listTop && gy < l.listBottom && gx >= l.colX && gx < l.colX + l.colW)
                    for (int i = 0; i < static_cast<int>(rows.size()); i++) {
                        if (!inside(l.colX, l.RowY(i, packUi.shaderScroll), l.colW, 18.0f))
                            continue;
                        options.language = langs[static_cast<size_t>(i)].code;
                        i18n::Lang::Get().Configure(ui::UiAssets::Instance().VanillaRoot(), paths::DataDir(),
                                                    options.language);
                        options.Save();
                        pause.signature.clear();
                        optionsUi.signature.clear();
                        break;
                    }
            }
            pause.leftDown = down;
            return;
        }
        if (pause.view == 1 && optionsUi.page == 8) {
            const std::string dir = paths::DataDir() + "/shaderpacks";
            listPacks(dir, false);
            if (std::find(packUi.foundCache.begin(), packUi.foundCache.end(), kBundledPackName) ==
                packUi.foundCache.end())
                packUi.foundCache.insert(packUi.foundCache.begin(), kBundledPackName);
            if (!packUi.shaderPendingInit) {
                packUi.shaderPendingInit = true;
                packUi.shaderPending = options.shaderPack;
                packUi.shaderPendingEnabled = !options.shaderPack.empty();
            }
            std::vector<std::string> rows;
            rows.push_back(detail::TrOr("perlin.shaders.title", "Shaders") + ": " +
                           (packUi.shaderPendingEnabled
                                ? detail::TrOr("perlin.shaders.enabled", "Enabled")
                                : detail::TrOr("perlin.shaders.disabled", "Disabled")));
            int selectedRow = -1;
            for (size_t i = 0; i < packUi.foundCache.size(); i++) {
                rows.push_back(packUi.foundCache[i]);
                if (packUi.shaderPendingEnabled && packUi.foundCache[i] == packUi.shaderPending)
                    selectedRow = static_cast<int>(i) + 1;
            }
            const ui::ShaderScreenLayout l =
                ui::ComputeShaderScreenLayout(static_cast<int>(rows.size()) + 1, guiW, guiH);
            packUi.shaderScroll = std::clamp(packUi.shaderScroll - wheel, 0.0f, l.maxScroll);
            // a java pack getting ready shows its progress over this screen; Done waits for it
            std::string progressText;
            float progress = -1.0f;
            {
                const RenderContext::JavaPackLoad load =
                    renderer ? renderer->JavaShaderPackLoad() : RenderContext::JavaPackLoad{};
                if (!javaPackPending.empty()) {
                    progress = 0.0f;
                    progressText =
                        live ? detail::TrOr("perlin.shaders.loadingTextures", "Loading the block textures...")
                             : detail::TrOr("perlin.shaders.loadsInWorld",
                                            "The shader pack loads when you join a world");
                    if (!live)
                        progress = -1.0f;
                } else if (load.active) {
                    progress = load.total > 0 ? static_cast<float>(load.done) / static_cast<float>(load.total)
                                              : 0.0f;
                    progressText = detail::TrOr("perlin.shaders.compiling", "Compiling shaders") + " " +
                                   std::to_string(load.done) + "/" + std::to_string(load.total) +
                                   (load.current.empty() ? std::string() : ": " + load.current);
                } else if (renderer && !renderer->JavaShaderPackError().empty() && packUi.shaderLoadWatch) {
                    progressText = renderer->JavaShaderPackError();
                }
                if (progress < 0.0f && progressText.empty() && packUi.closeWhenLoaded) {
                    packUi.closeWhenLoaded = false;
                    packUi.shaderLoadWatch = false;
                    leave();
                    pause.leftDown = down;
                    return;
                }
                if (progress < 0.0f && progressText.empty())
                    packUi.shaderLoadWatch = false;
            }
            const std::string signature =
                std::to_string(guiW) + "x" + std::to_string(guiH) + "s" +
                std::to_string(static_cast<int>(packUi.shaderScroll)) + "|" + std::to_string(selectedRow) +
                "|" + std::to_string(packUi.shaderPendingEnabled) + "|" + progressText + "|" +
                std::to_string(static_cast<int>(progress * 200.0f)) + "|" + [&] {
                    std::string s;
                    for (const auto& r : rows)
                        s += r + ";";
                    return s;
                }();
            if (signature != optionsUi.signature || !hud.Overlay().HasOverlay()) {
                optionsUi.signature = signature;
                submit(ui::BuildShaderScreenRml(
                           rows, selectedRow, 0, detail::TrOr("perlin.shaderPacks", "Shader Packs"),
                           detail::TrOr("perlin.shaders.select", "Select"),
                           "+ " + detail::TrOr("perlin.shaders.drag", "Drag and Drop Shader Packs to add"),
                           detail::TrOr("perlin.shaders.openFolder", "Open Shader Pack Folder..."),
                           detail::TrOr("perlin.shaders.settings", "Shader Pack Settings..."),
                           detail::TrOr("perlin.shaders.cancel", "Cancel"),
                           detail::TrOr("perlin.shaders.apply", "Apply"), detail::TrOr("gui.done", "Done"),
                           packUi.shaderScroll, guiW, guiH, progressText, progress),
                       "shader packs");
            }
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            const auto apply = [&]() {
                const std::string wanted = packUi.shaderPendingEnabled ? packUi.shaderPending : std::string();
                // applying the pack already running changes nothing: no reload
                if (wanted == options.shaderPack &&
                    (wanted.empty() || renderer->JavaShaderPackActive() || !javaPackPending.empty()))
                    return;
                options.shaderPack = wanted;
                options.Save();
                ApplyShaderPack();
                packUi.shaderLoadWatch = true;
            };

            if (click) {
                if (inside(l.openX, l.row1Y, l.btnW1, 20.0f)) {
                    openFolder(dir);
                } else if (inside(l.settingsX, l.row1Y, l.btnW1, 20.0f)) {
                    apply();
                    packUi.shaderPendingInit = false;
                    optionsUi.page = 10;
                    optionsUi.sub = 0;
                    optionsUi.signature.clear();
                    hud.Overlay().ClearOverlay();
                } else if (inside(l.cancelX, l.row2Y, l.thirdW, 20.0f)) {
                    leave();
                    pause.leftDown = down;
                    return;
                } else if (inside(l.applyX, l.row2Y, l.thirdW, 20.0f)) {
                    apply();
                } else if (inside(l.doneX, l.row2Y, l.thirdW, 20.0f)) {
                    apply();
                    // stay here with the progress bar until the pack is ready, then close
                    const bool loading = (!javaPackPending.empty() && live) ||
                                         (renderer && renderer->JavaShaderPackLoad().active);
                    if (loading) {
                        packUi.closeWhenLoaded = true;
                        optionsUi.signature.clear();
                    } else {
                        leave();
                        pause.leftDown = down;
                        return;
                    }
                } else if (gy >= l.listTop && gy < l.listBottom && gx >= l.colX && gx < l.colX + l.colW) {
                    for (int i = 0; i < static_cast<int>(rows.size()); i++) {
                        if (!inside(l.colX, l.RowY(i, packUi.shaderScroll), l.colW, 18.0f))
                            continue;
                        if (i == 0) {
                            packUi.shaderPendingEnabled = !packUi.shaderPendingEnabled;
                            if (packUi.shaderPendingEnabled && packUi.shaderPending.empty() &&
                                !packUi.foundCache.empty())
                                packUi.shaderPending = packUi.foundCache.front();
                            if (packUi.shaderPendingEnabled && packUi.shaderPending.empty())
                                packUi.shaderPendingEnabled = false;
                        } else {
                            packUi.shaderPending = packUi.foundCache[static_cast<size_t>(i - 1)];
                            packUi.shaderPendingEnabled = true;
                        }
                        optionsUi.signature.clear();
                        break;
                    }
                }
            }
            pause.leftDown = down;
            return;
        }
        const std::string modDir = paths::DataDir() + "/mods";
        auto& mm = mods::ModManager::Instance();
        const std::vector<mods::ModInfo> all = mm.List();
        ui::ModEntry unused;
        (void)unused;
        if (packUi.modIconStamp.find("*default") == packUi.modIconStamp.end()) {
            packUi.modIconStamp["*default"] = "1";
            Texture t;
            t.width = t.height = 16;
            t.pixels.assign(16 * 16 * 4, 0);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    unsigned char* p = &t.pixels[static_cast<size_t>((y * 16 + x) * 4)];
                    const bool bar =
                        (y == 4 || y == 5 || y == 8 || y == 9 || y == 12 || y == 13) && x >= 3 && x <= 12;
                    const bool dot =
                        (x == 2 || x == 3) && (y == 4 || y == 5 || y == 8 || y == 9 || y == 12 || y == 13);
                    p[0] = bar || dot ? 255 : 14;
                    p[1] = bar || dot ? 255 : 82;
                    p[2] = bar || dot ? 255 : 255;
                    p[3] = 255;
                }
            ui::UiAssets::Instance().SetHead("mod:default", t);
        }
        const std::string needle = lower(packUi.modSearch);
        std::vector<const mods::ModInfo*> shown;
        for (const mods::ModInfo& info : all) {
            if (!needle.empty() && lower(info.displayName).find(needle) == std::string::npos &&
                lower(info.name).find(needle) == std::string::npos &&
                lower(info.description).find(needle) == std::string::npos)
                continue;
            shown.push_back(&info);
        }
        const auto badgeFor = [&](const mods::ModInfo& info, int& color) {
            const std::string state = info.needsApproval ? std::string("approval")
                                      : info.enabled     ? info.state
                                                         : std::string("disabled");
            color = state == "running" ? 2 : state == "disabled" ? 3 : state == "loading" ? 0 : 1;
            if (state == "running")
                return detail::TrOr("perlin.mods.running", "Running");
            if (state == "disabled")
                return detail::TrOr("perlin.mods.disabled", "Disabled");
            if (state == "approval")
                return detail::TrOr("perlin.mods.approval", "Needs approval");
            if (state == "loading")
                return detail::TrOr("perlin.mods.loading", "Loading");
            if (state == "unresponsive")
                return detail::TrOr("perlin.mods.stuck", "Stuck");
            return detail::TrOr("perlin.mods.error", "Error");
        };
        const auto iconFor = [&](const mods::ModInfo& info) {
            if (info.iconPath.empty())
                return std::string("mchead:mod:default");
            std::error_code e;
            const std::string stamp = std::to_string(static_cast<long long>(
                std::filesystem::last_write_time(info.iconPath, e).time_since_epoch().count()));
            std::string& seen = packUi.modIconStamp["mod:" + info.name];
            if (seen != stamp) {
                seen = stamp;
                Texture t;
                if (LoadTexture(info.iconPath, t))
                    ui::UiAssets::Instance().SetHead("mod:" + info.name, t);
            }
            return "mchead:mod:" + info.name;
        };
        int selectedIndex = -1;
        for (size_t i = 0; i < shown.size(); i++)
            if (shown[i]->name == packUi.modSelected)
                selectedIndex = static_cast<int>(i);
        if (selectedIndex < 0 && !shown.empty()) {
            selectedIndex = 0;
            packUi.modSelected = shown[0]->name;
        }
        std::vector<ui::ModEntry> entries;
        for (const mods::ModInfo* info : shown) {
            ui::ModEntry e;
            e.name = info->displayName.empty() ? info->name : info->displayName;
            e.desc = info->description.empty()
                         ? detail::TrOr("perlin.mods.noDescription", "A WebAssembly mod.")
                         : info->description;
            e.badge = badgeFor(*info, e.badgeColor);
            e.icon = iconFor(*info);
            entries.push_back(e);
        }
        ui::ModDetail detail;
        if (selectedIndex >= 0) {
            const mods::ModInfo& info = *shown[static_cast<size_t>(selectedIndex)];
            detail.valid = true;
            detail.name = entries[static_cast<size_t>(selectedIndex)].name;
            detail.badge = entries[static_cast<size_t>(selectedIndex)].badge;
            detail.badgeColor = entries[static_cast<size_t>(selectedIndex)].badgeColor;
            detail.icon = entries[static_cast<size_t>(selectedIndex)].icon;
            detail.version = info.version.empty() ? std::string() : "v" + info.version;
            detail.author =
                info.author.empty() ? std::string() : detail::TrOr("perlin.mods.by", "By ") + info.author;
            detail.toggleEnabled = info.enabled;
            detail.toggleLabel = info.enabled         ? detail::TrOr("perlin.mods.disable", "Disable")
                                 : info.needsApproval ? detail::TrOr("perlin.mods.allow", "Allow & Enable")
                                                      : detail::TrOr("perlin.mods.enable", "Enable");
            detail.reloadLabel = detail::TrOr("perlin.mods.reload", "Reload");
            detail.hasSettings = info.hasSettings && info.enabled;
            detail.settingsLabel = detail::TrOr("perlin.mods.settings", "Settings");
            std::string body = info.description.empty()
                                   ? detail::TrOr("perlin.mods.noDescription", "A WebAssembly mod.")
                                   : info.description;
            if (info.requested) {
                std::string asks;
                const auto add = [&](abi::Permission p, const char* text) {
                    if (info.requested & static_cast<uint32_t>(p))
                        asks += std::string(asks.empty() ? "" : ", ") + text;
                };
                add(abi::Permission::Network, "use the internet");
                add(abi::Permission::Files, "copy, move and download files");
                add(abi::Permission::Control, "act for you (chat, commands, movement, options)");
                add(abi::Permission::Packets, "send raw game packets");
                add(abi::Permission::Native, "read the game window and graphics-device handles");
                body += "\n\n" + std::string(info.needsApproval ? "This mod asks to: " : "Allowed to: ") +
                        asks + ".";
                if (info.needsApproval)
                    body += " It stays off until you press Allow & Enable.";
            }
            if (!info.error.empty())
                body += "\n\n" + detail::TrOr("perlin.mods.errorLabel", "Error") + ": " + info.error;
            body += "\n\n" + detail::TrOr("perlin.mods.file", "File") + ": " + info.name + ".wasm (" +
                    std::to_string(info.size) + " bytes" +
                    (info.arch.empty()       ? std::string()
                     : info.arch == "wasm64" ? ", 64-bit"
                                             : ", 32-bit") +
                    ")";
            if (!info.hooks.empty()) {
                body += "\n" + detail::TrOr("perlin.mods.hooks", "Hooks") + ": ";
                for (size_t i = 0; i < info.hooks.size(); i++)
                    body += (i ? ", " : "") + info.hooks[i];
            }
            if (!info.permissions.empty()) {
                body += "\n" + detail::TrOr("perlin.mods.permissions", "Permissions") + ": ";
                for (size_t i = 0; i < info.permissions.size(); i++)
                    body += (i ? ", " : "") + info.permissions[i];
            }
            {
                std::string commandsText;
                for (const mods::PluginCommand& c : mm.Commands())
                    if (c.plugin == info.name)
                        commandsText += (commandsText.empty() ? "/" : ", /") + c.name;
                if (!commandsText.empty())
                    body += "\n" + detail::TrOr("perlin.mods.commands", "Commands") + ": " + commandsText;
                std::string keysText;
                for (const mods::PluginKey& k : mm.Keys())
                    if (k.plugin == info.name)
                        keysText += (keysText.empty() ? "" : ", ") + k.label + " [" +
                                    Binding::Parse(k.binding).Serialize().substr(
                                        Binding::Parse(k.binding).Serialize().rfind('.') + 1) +
                                    "]";
                if (!keysText.empty())
                    body += "\n" + detail::TrOr("perlin.mods.keys", "Keys") + ": " + keysText;
            }
            body += "\n" + detail::TrOr("perlin.mods.loads", "Loads") + ": " + std::to_string(info.loads);
            if (!info.website.empty())
                body += "\n\n" + info.website;
            detail.body = body;
        }
        const ui::ModScreenLayout l =
            ui::ComputeModScreenLayout(static_cast<int>(entries.size()), detail, guiW, guiH);
        if (inside(l.rightX, l.detailTextTop, l.rightW, l.detailTextBottom - l.detailTextTop))
            packUi.modDetailScroll -= wheel;
        else if (inside(l.leftX, l.listTop, l.leftW, l.listBottom - l.listTop))
            packUi.modScroll -= wheel;
        packUi.modScroll = std::clamp(packUi.modScroll, 0.0f, l.maxScroll);
        packUi.modDetailScroll = std::clamp(packUi.modDetailScroll, 0.0f, l.maxDetailScroll);
        const bool caret =
            (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() / 500) %
                2 ==
            0;
        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "m" +
                                std::to_string(static_cast<int>(packUi.modScroll)) + "," +
                                std::to_string(static_cast<int>(packUi.modDetailScroll)) + "|" +
                                packUi.modSearch + (caret ? "_" : "") + "|" + packUi.modSelected + "|" +
                                std::to_string(std::hash<std::string>()(detail.body));
        for (const auto& e : entries)
            signature += "|" + e.name + e.badge + e.desc;
        signature += "#" + detail.body + detail.toggleLabel;
        if (signature != optionsUi.signature || !hud.Overlay().HasOverlay()) {
            optionsUi.signature = signature;
            const std::string showing = detail::TrOr("perlin.mods.showing", "Showing ") +
                                        std::to_string(entries.size()) +
                                        (entries.size() == 1 ? detail::TrOr("perlin.mods.mod", " mod")
                                                             : detail::TrOr("perlin.mods.mods", " mods"));
            submit(ui::BuildModScreenRml(entries, selectedIndex, detail,
                                         detail::TrOr("perlin.mods.title", "Mods"), packUi.modSearch, caret,
                                         showing, detail::TrOr("perlin.mods.search", "Search..."),
                                         detail::TrOr("perlin.mods.openFolder", "Open Mods Folder"),
                                         detail::TrOr("gui.done", "Done"), packUi.modScroll,
                                         packUi.modDetailScroll, guiW, guiH),
                   "mods");
        }
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        if (click) {
            if (inside(l.doneX, l.btnY, l.btnW, 20.0f)) {
                leave();
                pause.leftDown = down;
                return;
            }
            if (inside(l.openX, l.btnY, l.btnW, 20.0f)) {
                openFolder(modDir);
            } else if (detail.valid && inside(l.toggleX, l.detailBtnY - 1.0f, l.toggleW, 20.0f)) {
                ToggleMod(packUi.modSelected);
                optionsUi.signature.clear();
            } else if (detail.valid && detail.toggleEnabled &&
                       inside(l.reloadX, l.detailBtnY - 1.0f, l.reloadW, 20.0f)) {
                mm.ReloadMod(packUi.modSelected);
                optionsUi.signature.clear();
            } else if (detail.valid && detail.hasSettings &&
                       inside(l.settingsX, l.detailBtnY - 1.0f, l.settingsW, 20.0f)) {
                modSettingsFor = packUi.modSelected;
                pause.view = 1;
                optionsUi.page = 11;
                optionsUi.sub = 0;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (gy >= l.listTop && gy < l.listBottom && gx >= l.leftX && gx < l.leftX + l.leftW) {
                for (int i = 0; i < static_cast<int>(shown.size()); i++)
                    if (inside(l.leftX, l.RowY(i, packUi.modScroll),
                               l.leftW - (l.maxScroll > 0 ? 8.0f : 0.0f), l.rowH - 2.0f)) {
                        packUi.modSelected = shown[static_cast<size_t>(i)]->name;
                        packUi.modDetailScroll = 0;
                        optionsUi.signature.clear();
                        break;
                    }
            }
        }
        pause.leftDown = down;
    }
}
