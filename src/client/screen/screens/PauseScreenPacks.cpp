#include "client/screen/screens/ScreenInternal.hpp"
#include "client/screen/screens/PauseFrame.hpp"
#include <algorithm>
#include "util/Strings.hpp"
#include "util/Utf8.hpp"
#include "util/Log.hpp"

namespace perlin::screen {
    namespace {
        std::string PackIconSource(const std::string& dir, const std::string& name, const PackMeta& meta,
                                   const char* fallback) {
            return meta.hasIcon ? "mchead:pack:" + dir + "/" + name : std::string(fallback);
        }

        void SubmitPackOverlay(ui::Hud& hud, const ui::SceneDoc& doc, const char* label) {
            std::string error;
            if (!hud.Overlay().SetOverlay(doc, error))
                Log("ui", "{} screen: {}", label, error);
        }
    }

    void PauseScreen::OpenPackFolder(const std::string& dir) {
        if (services.pickFile("pack"))
            return;
        std::error_code e;
        std::filesystem::create_directories(dir, e);
        services.openUrl("file://" + std::filesystem::absolute(dir, e).generic_string());
    }

    void PauseScreen::LeavePackMenu(PauseFrame& f) {
        services.commitTexturePacks();
        services.options.Save();
        packUi.shaderPendingInit = false;
        if (this->view == 4)
            this->view = 0;
        else
            optionsUi.page = 0;
        optionsUi.signature.clear();
        this->signature.clear();
        f.hud.Overlay().ClearOverlay();
    }

    void PauseScreen::ListPacks(TimePoint now, const std::string& dir, bool allowMcpack) {
        if (now - packUi.foundAt < std::chrono::milliseconds(1000) && packUi.metaStampDir == dir)
            return;
        packUi.foundAt = now;
        packUi.metaStampDir = dir;
        packUi.foundCache.clear();
        std::error_code e;
        std::filesystem::create_directories(dir, e);
        for (const auto& entry : std::filesystem::directory_iterator(dir, e)) {
            const std::string ext = str::Lower(entry.path().extension().string());
            if (entry.is_directory(e) || ext == ".zip" || (allowMcpack && ext == ".mcpack"))
                packUi.foundCache.push_back(entry.path().filename().string());
        }
        std::ranges::sort(packUi.foundCache);
    }

    const PackMeta& PauseScreen::PackMetaFor(const std::string& dir, const std::string& name) {
        const std::string path = dir + "/" + name;
        std::error_code e;
        const std::string stamp =
            std::to_string(std::filesystem::is_directory(path, e) ? 0 : std::filesystem::file_size(path, e)) + ":" +
            std::to_string(static_cast<long long>(std::filesystem::last_write_time(path, e).time_since_epoch().count()));
        auto& slot = packUi.metaCache[path];
        if (slot.stamp != stamp) {
            slot.stamp = stamp;
            slot.meta = ReadPackMeta(path);
            if (slot.meta.hasIcon)
                ui::UiAssets::Instance().SetHead("pack:" + path, slot.meta.icon);
        }
        return slot.meta;
    }

    void PauseScreen::UpdatePackMenu(PauseFrame& f) {
        const float scale = static_cast<float>(f.scale);
        PackMenuInput in;
        in.gx = f.mx / scale;
        in.gy = f.my / scale;
        in.click = f.down && !this->leftDown && f.clickReady;
        in.wheel = host.inputManager.Scroll() * 20.0f;
        if (this->view == 1 && optionsUi.page == 7)
            UpdateResourcePackPage(f, in);
        else if (this->view == 1 && optionsUi.page == 6)
            UpdateLanguagePage(f, in);
        else if (this->view == 1 && optionsUi.page == 8)
            UpdateShaderPackPage(f, in);
        else
            UpdateModsPage(f, in);
    }


    void PauseScreen::UpdateResourcePackPage(PauseFrame& f, const PackMenuInput& in) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        [[maybe_unused]] const float gx = in.gx, gy = in.gy, wheel = in.wheel;
        [[maybe_unused]] const bool click = in.click;
        [[maybe_unused]] const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        const std::string dir = paths::DataDir() + "/texturepacks";
        ListPacks(now, dir, true);
        if (!packUi.pendingInit) {
            packUi.pendingInit = true;
            packUi.pending = services.options.texturePacks;
        }
        std::vector<std::string> existing;
        for (const std::string& name : packUi.pending)
            if (std::ranges::find(packUi.foundCache, name) != packUi.foundCache.end())
                existing.push_back(name);
        packUi.pending = existing;
        std::vector<ui::PackEntry> avail, sel;
        std::vector<std::string> availNames;
        for (size_t i = 0; i < packUi.pending.size(); i++) {
            const std::string& name = packUi.pending[i];
            const PackMeta& m = PackMetaFor(dir, name);
            ui::PackEntry e;
            e.name = EnsureUtf8(m.name.empty() ? name : m.name);
            e.desc = m.description;
            e.icon = PackIconSource(dir, name, m, "textures/blocks/stone");
            e.canUnselect = true;
            e.canUp = i > 0;
            e.canDown = true;
            sel.push_back(e);
        }
        {
            ui::PackEntry e;
            e.name = i18n::TrOr("perlin.packs.defaultName", "Default");
            e.desc = i18n::TrOr("perlin.packs.defaultDesc",
                                  "The default look and feel of Minecraft (built-in)");
            e.icon = "textures/blocks/grass_side_carried";
            sel.push_back(e);
        }
        for (const std::string& name : packUi.foundCache) {
            if (std::ranges::find(packUi.pending, name) != packUi.pending.end())
                continue;
            const PackMeta& m = PackMetaFor(dir, name);
            ui::PackEntry e;
            e.name = EnsureUtf8(m.name.empty() ? name : m.name);
            e.desc = m.description;
            e.icon = PackIconSource(dir, name, m, "textures/blocks/stone");
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
            SubmitPackOverlay(hud, 
                ui::BuildPackScreen(avail, sel, i18n::TrOr("perlin.packs.title", "Resource Packs"),
                                       i18n::TrOr("perlin.packs.hint",
                                                    "Drag and drop files into this window to add packs"),
                                       i18n::TrOr("perlin.packs.available", "Available"),
                                       i18n::TrOr("perlin.packs.selected", "Selected"),
                                       i18n::TrOr("perlin.packs.openFolder", "Open Pack Folder"),
                                       i18n::TrOr("gui.done", "Done"), packUi.scrollActive,
                                       packUi.scrollAvailable, guiW, guiH),
                "resource packs");
        }
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, services.options.guiScale);
        if (click) {
            const float sbA = l.maxScrollA > 0 ? 6.0f : 0.0f, sbS = l.maxScrollB > 0 ? 6.0f : 0.0f;
            bool changed = false;
            if (inside(l.openX, l.btnY, l.btnW, 20.0f)) {
                OpenPackFolder(dir);
            } else if (inside(l.doneX, l.btnY, l.btnW, 20.0f)) {
                LeavePackMenu(f);
                this->leftDown = down;
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
        this->leftDown = down;
    }

    void PauseScreen::UpdateLanguagePage(PauseFrame& f, const PackMenuInput& in) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        [[maybe_unused]] const float gx = in.gx, gy = in.gy, wheel = in.wheel;
        [[maybe_unused]] const bool click = in.click;
        [[maybe_unused]] const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        const auto langs = i18n::Lang::Get().Languages();
        std::vector<std::string> rows;
        int selectedRow = -1;
        for (size_t i = 0; i < langs.size(); i++) {
            rows.push_back(langs[i].name.empty() ? langs[i].code : langs[i].name);
            if (langs[i].code == services.options.language)
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
            SubmitPackOverlay(hud, ui::BuildLanguageScreen(
                       rows, selectedRow, i18n::TrOr("options.language", "Language"),
                       i18n::TrOr("perlin.language.subtitle",
                                    "Language translations may not be 100% accurate"),
                       i18n::TrOr("gui.done", "Done"), packUi.shaderScroll, guiW, guiH),
                   "language");
        }
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, services.options.guiScale);
        if (click) {
            const float doneX = l.colX + (l.colW - 200.0f) / 2.0f;
            if (inside(doneX, l.row1Y + 12.0f, 200.0f, 20.0f)) {
                LeavePackMenu(f);
                this->leftDown = down;
                return;
            }
            if (gy >= l.listTop && gy < l.listBottom && gx >= l.colX && gx < l.colX + l.colW)
                for (int i = 0; i < static_cast<int>(rows.size()); i++) {
                    if (!inside(l.colX, l.RowY(i, packUi.shaderScroll), l.colW, 18.0f))
                        continue;
                    services.options.language = langs[static_cast<size_t>(i)].code;
                    i18n::Lang::Get().Configure(ui::UiAssets::Instance().VanillaRoot(), paths::DataDir(),
                                                services.options.language);
                    services.options.Save();
                    this->signature.clear();
                    optionsUi.signature.clear();
                    break;
                }
        }
        this->leftDown = down;
    }

    void PauseScreen::UpdateShaderPackPage(PauseFrame& f, const PackMenuInput& in) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        [[maybe_unused]] const float gx = in.gx, gy = in.gy, wheel = in.wheel;
        [[maybe_unused]] const bool click = in.click;
        [[maybe_unused]] const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        const std::string dir = paths::DataDir() + "/shaderpacks";
        ListPacks(now, dir, false);
        if (std::ranges::find(packUi.foundCache, kBundledPackName) == packUi.foundCache.end())
            packUi.foundCache.insert(packUi.foundCache.begin(), kBundledPackName);
        if (!packUi.shaderPendingInit) {
            packUi.shaderPendingInit = true;
            packUi.shaderPending = services.options.shaderPack;
            packUi.shaderPendingEnabled = !services.options.shaderPack.empty();
        }
        std::vector<std::string> rows;
        rows.push_back(i18n::TrOr("perlin.shaders.title", "Shaders") + ": " +
                       (packUi.shaderPendingEnabled
                            ? i18n::TrOr("perlin.shaders.enabled", "Enabled")
                            : i18n::TrOr("perlin.shaders.disabled", "Disabled")));
        int selectedRow = -1;
        for (size_t i = 0; i < packUi.foundCache.size(); i++) {
            rows.push_back(packUi.foundCache[i]);
            if (packUi.shaderPendingEnabled && packUi.foundCache[i] == packUi.shaderPending)
                selectedRow = static_cast<int>(i) + 1;
        }
        const ui::ShaderScreenLayout l =
            ui::ComputeShaderScreenLayout(static_cast<int>(rows.size()) + 1, guiW, guiH);
        packUi.shaderScroll = std::clamp(packUi.shaderScroll - wheel, 0.0f, l.maxScroll);

        std::string progressText;
        float progress = -1.0f;
        {
            const RenderContext::JavaPackLoad load =
                services.renderer ? services.renderer->JavaShaderPackLoad() : RenderContext::JavaPackLoad{};
            if (!services.javaPackPending.empty()) {
                progress = 0.0f;
                progressText =
                    live ? i18n::TrOr("perlin.shaders.loadingTextures", "Loading the block textures...")
                         : i18n::TrOr("perlin.shaders.loadsInWorld",
                                        "The shader pack loads when you join a world");
                if (!live)
                    progress = -1.0f;
            } else if (load.active) {
                progress = load.total > 0 ? static_cast<float>(load.done) / static_cast<float>(load.total)
                                          : 0.0f;
                progressText = i18n::TrOr("perlin.shaders.compiling", "Compiling shaders") + " " +
                               std::to_string(load.done) + "/" + std::to_string(load.total) +
                               (load.current.empty() ? std::string() : ": " + load.current);
            } else if (services.renderer && !services.renderer->JavaShaderPackError().empty() && packUi.shaderLoadWatch) {
                progressText = services.renderer->JavaShaderPackError();
            }
            if (progress < 0.0f && progressText.empty() && packUi.closeWhenLoaded) {
                packUi.closeWhenLoaded = false;
                packUi.shaderLoadWatch = false;
                LeavePackMenu(f);
                this->leftDown = down;
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
            SubmitPackOverlay(hud, ui::BuildShaderScreen(
                       rows, selectedRow, 0, i18n::TrOr("perlin.shaderPacks", "Shader Packs"),
                       i18n::TrOr("perlin.shaders.select", "Select"),
                       "+ " + i18n::TrOr("perlin.shaders.drag", "Drag and Drop Shader Packs to add"),
                       i18n::TrOr("perlin.shaders.openFolder", "Open Shader Pack Folder..."),
                       i18n::TrOr("perlin.shaders.settings", "Shader Pack Settings..."),
                       i18n::TrOr("perlin.shaders.cancel", "Cancel"),
                       i18n::TrOr("perlin.shaders.apply", "Apply"), i18n::TrOr("gui.done", "Done"),
                       packUi.shaderScroll, guiW, guiH, progressText, progress),
                   "shader packs");
        }
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, services.options.guiScale);
        const auto apply = [&]() {
            const std::string wanted = packUi.shaderPendingEnabled ? packUi.shaderPending : std::string();

            if (wanted == services.options.shaderPack &&
                (wanted.empty() || services.renderer->JavaShaderPackActive() || !services.javaPackPending.empty()))
                return;
            services.options.shaderPack = wanted;
            services.options.Save();
            services.applyShaderPack();
            packUi.shaderLoadWatch = true;
        };

        if (click) {
            if (inside(l.openX, l.row1Y, l.btnW1, 20.0f)) {
                OpenPackFolder(dir);
            } else if (inside(l.settingsX, l.row1Y, l.btnW1, 20.0f)) {
                apply();
                packUi.shaderPendingInit = false;
                optionsUi.page = 10;
                optionsUi.sub = 0;
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (inside(l.cancelX, l.row2Y, l.thirdW, 20.0f)) {
                LeavePackMenu(f);
                this->leftDown = down;
                return;
            } else if (inside(l.applyX, l.row2Y, l.thirdW, 20.0f)) {
                apply();
            } else if (inside(l.doneX, l.row2Y, l.thirdW, 20.0f)) {
                apply();

                const bool loading = (!services.javaPackPending.empty() && live) ||
                                     (services.renderer && services.renderer->JavaShaderPackLoad().active);
                if (loading) {
                    packUi.closeWhenLoaded = true;
                    optionsUi.signature.clear();
                } else {
                    LeavePackMenu(f);
                    this->leftDown = down;
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
        this->leftDown = down;
    }

    void PauseScreen::UpdateModsPage(PauseFrame& f, const PackMenuInput& in) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        [[maybe_unused]] const float gx = in.gx, gy = in.gy, wheel = in.wheel;
        [[maybe_unused]] const bool click = in.click;
        [[maybe_unused]] const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        const std::string modDir = paths::DataDir() + "/mods";
        auto& mm = services.manager;
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
        const std::string needle = str::Lower(packUi.modSearch);
        std::vector<const mods::ModInfo*> shown;
        for (const mods::ModInfo& info : all) {
            if (!needle.empty() && !str::Lower(info.displayName).contains(needle) &&
                !str::Lower(info.name).contains(needle) && !str::Lower(info.description).contains(needle))
                continue;
            shown.push_back(&info);
        }
        const auto badgeFor = [&](const mods::ModInfo& info, int& color) {
            const std::string state = info.needsApproval ? std::string("approval")
                                      : info.enabled     ? info.state
                                                         : std::string("disabled");
            color = state == "running" ? 2 : state == "disabled" ? 3 : state == "loading" ? 0 : 1;
            if (state == "running")
                return i18n::TrOr("perlin.mods.running", "Running");
            if (state == "disabled")
                return i18n::TrOr("perlin.mods.disabled", "Disabled");
            if (state == "approval")
                return i18n::TrOr("perlin.mods.approval", "Needs approval");
            if (state == "loading")
                return i18n::TrOr("perlin.mods.loading", "Loading");
            if (state == "unresponsive")
                return i18n::TrOr("perlin.mods.stuck", "Stuck");
            return i18n::TrOr("perlin.mods.error", "Error");
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
                         ? i18n::TrOr("perlin.mods.noDescription", "A WebAssembly mod.")
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
                info.author.empty() ? std::string() : i18n::TrOr("perlin.mods.by", "By ") + info.author;
            detail.toggleEnabled = info.enabled;
            detail.toggleLabel = info.enabled         ? i18n::TrOr("perlin.mods.disable", "Disable")
                                 : info.needsApproval ? i18n::TrOr("perlin.mods.allow", "Allow & Enable")
                                                      : i18n::TrOr("perlin.mods.enable", "Enable");
            detail.reloadLabel = i18n::TrOr("perlin.mods.reload", "Reload");
            detail.hasSettings = info.hasSettings && info.enabled;
            detail.settingsLabel = i18n::TrOr("perlin.mods.settings", "Settings");
            std::string body = info.description.empty()
                                   ? i18n::TrOr("perlin.mods.noDescription", "A WebAssembly mod.")
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
                body += "\n\n" + i18n::TrOr("perlin.mods.errorLabel", "Error") + ": " + info.error;
            body += "\n\n" + i18n::TrOr("perlin.mods.file", "File") + ": " + info.name + ".wasm (" +
                    std::to_string(info.size) + " bytes" +
                    (info.arch.empty()       ? std::string()
                     : info.arch == "wasm64" ? ", 64-bit"
                                             : ", 32-bit") +
                    ")";
            if (!info.hooks.empty()) {
                body += "\n" + i18n::TrOr("perlin.mods.hooks", "Hooks") + ": ";
                for (size_t i = 0; i < info.hooks.size(); i++)
                    body += (i ? ", " : "") + info.hooks[i];
            }
            if (!info.permissions.empty()) {
                body += "\n" + i18n::TrOr("perlin.mods.permissions", "Permissions") + ": ";
                for (size_t i = 0; i < info.permissions.size(); i++)
                    body += (i ? ", " : "") + info.permissions[i];
            }
            {
                std::string commandsText;
                for (const mods::PluginCommand& c : mm.Commands())
                    if (c.plugin == info.name)
                        commandsText += (commandsText.empty() ? "/" : ", /") + c.name;
                if (!commandsText.empty())
                    body += "\n" + i18n::TrOr("perlin.mods.commands", "Commands") + ": " + commandsText;
                std::string keysText;
                for (const mods::PluginKey& k : mm.Keys())
                    if (k.plugin == info.name)
                        keysText += (keysText.empty() ? "" : ", ") + k.label + " [" +
                                    Binding::Parse(k.binding).Serialize().substr(
                                        Binding::Parse(k.binding).Serialize().rfind('.') + 1) +
                                    "]";
                if (!keysText.empty())
                    body += "\n" + i18n::TrOr("perlin.mods.keys", "Keys") + ": " + keysText;
            }
            body += "\n" + i18n::TrOr("perlin.mods.loads", "Loads") + ": " + std::to_string(info.loads);
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
            const std::string showing = i18n::TrOr("perlin.mods.showing", "Showing ") +
                                        std::to_string(entries.size()) +
                                        (entries.size() == 1 ? i18n::TrOr("perlin.mods.mod", " mod")
                                                             : i18n::TrOr("perlin.mods.mods", " mods"));
            SubmitPackOverlay(hud, ui::BuildModScreen(entries, selectedIndex, detail,
                                         i18n::TrOr("perlin.mods.title", "Mods"), packUi.modSearch, caret,
                                         showing, i18n::TrOr("perlin.mods.search", "Search..."),
                                         i18n::TrOr("perlin.mods.openFolder", "Open Mods Folder"),
                                         i18n::TrOr("gui.done", "Done"), packUi.modScroll,
                                         packUi.modDetailScroll, guiW, guiH),
                   "mods");
        }
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, services.options.guiScale);
        if (click) {
            if (inside(l.doneX, l.btnY, l.btnW, 20.0f)) {
                LeavePackMenu(f);
                this->leftDown = down;
                return;
            }
            if (inside(l.openX, l.btnY, l.btnW, 20.0f)) {
                OpenPackFolder(modDir);
            } else if (detail.valid && inside(l.toggleX, l.detailBtnY - 1.0f, l.toggleW, 20.0f)) {
                services.toggleMod(packUi.modSelected);
                optionsUi.signature.clear();
            } else if (detail.valid && detail.toggleEnabled &&
                       inside(l.reloadX, l.detailBtnY - 1.0f, l.reloadW, 20.0f)) {
                mm.ReloadMod(packUi.modSelected);
                optionsUi.signature.clear();
            } else if (detail.valid && detail.hasSettings &&
                       inside(l.settingsX, l.detailBtnY - 1.0f, l.settingsW, 20.0f)) {
                services.modRequest.settingsFor = packUi.modSelected;
                this->view = 1;
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
        this->leftDown = down;
    }

}
