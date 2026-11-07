#include "client/PerlinPause.hpp"
#include "util/Log.hpp"

namespace perlin {
    void Perlin::UpdateWardrobe(PauseFrame& f) {
        auto& [hud, live, fbW, fbH, now, preview, scale, guiW, guiH, mx, my, down, clickReady] = f;
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        const auto inside = [&](float x, float y, float w, float h) {
            return gx >= x && gx < x + w && gy >= y && gy < y + h;
        };
        const bool click = down && !pause.leftDown && clickReady;
        if (!wardrobe.init) {
            wardrobe.init = true;
            wardrobe.skin = options.skin;
            wardrobe.cape = options.cape;
            wardrobe.slim = options.skinSlim;
            wardrobe.previewKey.clear();
            wardrobe.skinOpen = true;
            wardrobe.capeOpen = true;
        }
        namespace fsys = std::filesystem;
        const std::string skinDir = paths::DataDir() + "/skins", capeDir = paths::DataDir() + "/capes";
        std::error_code ec;
        fsys::create_directories(skinDir, ec);
        fsys::create_directories(capeDir, ec);
        if (now - wardrobe.listedAt > std::chrono::milliseconds(1000)) {
            wardrobe.listedAt = now;
            const auto pngs = [](const std::string& dir) {
                std::vector<std::string> out;
                std::error_code e;
                for (const auto& entry : fsys::directory_iterator(dir, e))
                    if (entry.is_regular_file(e) && entry.path().extension() == ".png")
                        out.push_back(entry.path().generic_string());
                std::sort(out.begin(), out.end());
                return out;
            };
            wardrobe.skins = pngs(skinDir);
            wardrobe.capes = pngs(capeDir);
        }
        const auto stampOf = [](const std::string& path) {
            std::error_code e;
            return std::to_string(static_cast<unsigned long long>(fsys::file_size(path, e))) + ":" +
                   std::to_string(
                       static_cast<long long>(fsys::last_write_time(path, e).time_since_epoch().count()));
        };
        const auto stem = [](const std::string& path) { return fsys::path(path).stem().string(); };
        const auto slimOf = [&](const std::string& path) {
            if (const auto it = wardrobe.slim.find(path); it != wardrobe.slim.end())
                return it->second;
            if (const auto it = wardrobe.slimGuess.find(path); it != wardrobe.slimGuess.end())
                return it->second;
            return false;
        };
        const auto defaultSkin = [&](Texture& t) {
            return ui::UiAssets::Instance().Image("textures/entity/steve.png", t);
        };
        const auto renderCard = [&](const std::string& key, const std::vector<unsigned char>* skin, int sw,
                                    int sh, bool slim, const std::vector<unsigned char>* cape, int cw, int ch,
                                    float yaw) {
            ui::SkinView v;
            v.skin = skin;
            v.skinW = sw;
            v.skinH = sh;
            v.cape = cape;
            v.capeW = cw;
            v.capeH = ch;
            v.slim = slim;
            v.yaw = yaw;
            ui::UiAssets::Instance().SetHead(key, ui::RenderSkin3D(v, 108, 156));
        };
        const auto ensureSkin = [&](const std::string& path) {
            const std::string stamp = stampOf(path);
            auto& slot = wardrobe.stamp["skin:" + path];
            if (slot == stamp)
                return;
            slot = stamp;
            SkinConfig cfg;
            cfg.skinPath = path;
            LoadedSkin loaded;
            std::string err;
            if (!LoadSkinConfig(cfg, loaded, err))
                return;
            wardrobe.slimGuess[path] = DetectSlimSkin(loaded.skinRgba, loaded.skinWidth, loaded.skinHeight);
            renderCard("skin:" + path, &loaded.skinRgba, loaded.skinWidth, loaded.skinHeight, slimOf(path),
                       nullptr, 0, 0, -32.0f);
        };
        Texture steve;
        const bool haveSteve = defaultSkin(steve);
        const auto ensureCape = [&](const std::string& path) {
            const std::string stamp = stampOf(path);
            auto& slot = wardrobe.stamp["cape:" + path];
            if (slot == stamp || !haveSteve)
                return;
            slot = stamp;
            Texture t;
            if (LoadTexture(path, t))
                renderCard("cape:" + path, &steve.pixels, steve.width, steve.height, false, &t.pixels,
                           t.width, t.height, 180.0f);
        };
        if (wardrobe.stamp.find("default") == wardrobe.stamp.end() && haveSteve) {
            wardrobe.stamp["default"] = "1";
            renderCard("skin:default", &steve.pixels, steve.width, steve.height, false, nullptr, 0, 0,
                       -32.0f);
            renderCard("cape:none", &steve.pixels, steve.width, steve.height, false, nullptr, 0, 0, 180.0f);
        }
        const auto commit = [this, live]() {
            options.skin = wardrobe.skin;
            options.cape = wardrobe.cape;
            options.skinSlim = wardrobe.slim;
            accounts.SetSkin(accounts.ActiveId(), options.skin, options.cape);
            SkinConfig cfg = MakeSkinConfig(options.skin, options.cape);
            LoadedSkin loaded;
            std::string err;
            if (!LoadSkinConfig(cfg, loaded, err)) {
                Log("wardrobe", "{}", err);
                options.Save();
                return;
            }
            options.slimArms = loaded.slimArms;
            options.Save();
            if (live) {
                live->SetSkin(std::move(loaded));
                live->SendSkinUpdate();
            }
        };
        std::vector<ui::WardrobeCard> skinCards, capeCards;
        std::vector<std::string> skinPaths, capePaths;
        const std::string defaultName = detail::TrOr("perlin.wardrobe.default", "Default");
        skinCards.push_back({defaultName, "mchead:skin:default", wardrobe.skin.empty()});
        skinPaths.push_back("");
        for (const std::string& path : wardrobe.skins) {
            ensureSkin(path);
            skinCards.push_back({stem(path), "mchead:skin:" + path, wardrobe.skin == path});
            skinPaths.push_back(path);
        }
        capeCards.push_back(
            {detail::TrOr("perlin.wardrobe.noCape", "No Cape"), "mchead:cape:none", wardrobe.cape.empty()});
        capePaths.push_back("");
        for (const std::string& path : wardrobe.capes) {
            ensureCape(path);
            capeCards.push_back({stem(path), "mchead:cape:" + path, wardrobe.cape == path});
            capePaths.push_back(path);
        }
        if (!wardrobe.skin.empty())
            ensureSkin(wardrobe.skin);
        ui::WardrobeLayout l =
            ui::ComputeWardrobeLayout(static_cast<int>(skinCards.size()), static_cast<int>(capeCards.size()),
                                      wardrobe.skinOpen, wardrobe.capeOpen, wardrobe.line, guiW, guiH);
        if (gx >= l.listX && gx < l.listX + l.listW + 6.0f && gy >= l.listTop && gy < l.listBottom) {
            const float wheel = inputManager.Scroll();
            if (wheel > 0.0f)
                wardrobe.line--;
            else if (wheel < 0.0f)
                wardrobe.line++;
        }
        wardrobe.line = std::clamp(wardrobe.line, 0, l.maxFirst);
        if (wardrobe.line != l.firstLine)
            l = ui::ComputeWardrobeLayout(static_cast<int>(skinCards.size()),
                                          static_cast<int>(capeCards.size()), wardrobe.skinOpen,
                                          wardrobe.capeOpen, wardrobe.line, guiW, guiH);
        const bool overPreview = inside(l.prevX, l.prevY, l.prevW, l.prevH);
        if (click && overPreview) {
            wardrobe.drag = true;
            wardrobe.dragX = gx;
            wardrobe.dragY = gy;
        }
        if (!down)
            wardrobe.drag = false;
        if (wardrobe.drag) {
            wardrobe.yaw += (gx - wardrobe.dragX) * 1.6f;
            wardrobe.pitch = std::clamp(wardrobe.pitch + (gy - wardrobe.dragY) * 1.2f, -85.0f, 85.0f);
            wardrobe.dragX = gx;
            wardrobe.dragY = gy;
        }
        wardrobe.yaw = std::fmod(wardrobe.yaw, 360.0f);
        const bool slimNow = slimOf(wardrobe.skin);
        const std::string previewKey =
            wardrobe.skin + "|" + wardrobe.cape + "|" + std::to_string(slimNow) + "|" +
            std::to_string(static_cast<int>(wardrobe.yaw)) + "|" +
            std::to_string(static_cast<int>(wardrobe.pitch)) + "|" + wardrobe.stamp["skin:" + wardrobe.skin] +
            "|" + wardrobe.stamp["cape:" + wardrobe.cape] + "|" + std::to_string(options.skinParts.cape) +
            std::to_string(options.skinParts.jacket) + std::to_string(options.skinParts.leftSleeve) +
            std::to_string(options.skinParts.rightSleeve) + std::to_string(options.skinParts.leftPants) +
            std::to_string(options.skinParts.rightPants) + std::to_string(options.skinParts.hat) +
            (wardrobe.drag ? "d" : "s");
        if (previewKey != wardrobe.previewKey) {
            wardrobe.previewKey = previewKey;
            SkinConfig cfg;
            cfg.skinPath = wardrobe.skin;
            cfg.capePath = wardrobe.cape;
            const bool changedSource = wardrobe.previewSource != wardrobe.skin + "|" + wardrobe.cape + "|" +
                                                                     wardrobe.stamp["skin:" + wardrobe.skin] +
                                                                     "|" +
                                                                     wardrobe.stamp["cape:" + wardrobe.cape];
            if (changedSource) {
                wardrobe.previewSource = wardrobe.skin + "|" + wardrobe.cape + "|" +
                                         wardrobe.stamp["skin:" + wardrobe.skin] + "|" +
                                         wardrobe.stamp["cape:" + wardrobe.cape];
                wardrobe.previewLoaded = {};
                std::string err;
                if (!LoadSkinConfig(cfg, wardrobe.previewLoaded, err))
                    wardrobe.previewLoaded = {};
            }
            {
                const LoadedSkin& loaded = wardrobe.previewLoaded;
                ui::UiAssets::Instance().EraseHead("ward:cur" + std::to_string(wardrobe.previewSerial));
                wardrobe.previewSerial++;
                ui::SkinView v;
                v.skin = wardrobe.skin.empty() ? &steve.pixels : &loaded.skinRgba;
                v.skinW = wardrobe.skin.empty() ? steve.width : loaded.skinWidth;
                v.skinH = wardrobe.skin.empty() ? steve.height : loaded.skinHeight;
                v.cape = loaded.capeRgba.empty() || !options.skinParts.cape ? nullptr : &loaded.capeRgba;
                v.capeW = loaded.capeWidth;
                v.capeH = loaded.capeHeight;
                v.slim = slimNow;
                v.yaw = wardrobe.yaw;
                v.pitch = wardrobe.pitch;
                v.jacket = options.skinParts.jacket;
                v.leftSleeve = options.skinParts.leftSleeve;
                v.rightSleeve = options.skinParts.rightSleeve;
                v.leftPants = options.skinParts.leftPants;
                v.rightPants = options.skinParts.rightPants;
                v.hat = options.skinParts.hat;
                const float q = wardrobe.drag ? 1.5f : 3.0f;
                ui::UiAssets::Instance().SetHead(
                    "ward:cur" + std::to_string(wardrobe.previewSerial),
                    ui::RenderSkin3D(v, static_cast<int>(l.prevW * q), static_cast<int>(l.prevH * q)));
            }
        }
        ui::WardrobeText wt;
        wt.title = detail::TrOr("perlin.wardrobe", "Wardrobe");
        wt.skins = detail::TrOr("perlin.wardrobe.skins", "Skins");
        wt.capes = detail::TrOr("perlin.wardrobe.capes", "Capes");
        wt.addSkin = "    " + detail::TrOr("perlin.wardrobe.addSkin", "Add Skin");
        wt.addCape = "    " + detail::TrOr("perlin.wardrobe.addCape", "Add Cape");
        wt.slim = detail::TrOr("perlin.wardrobe.slim", "Slim Arms");
        wt.slimOn = detail::TrOr("options.on", "ON");
        wt.slimOff = detail::TrOr("options.off", "OFF");
        wt.done = detail::TrOr("gui.done", "Done");
        wt.dropHint = detail::TrOr("perlin.wardrobe.drop", "Drop .png files here to add skins and capes");
        wt.dragHint = detail::TrOr("perlin.wardrobe.drag", "Drag to rotate");
        const std::string previewName = wardrobe.skin.empty() ? defaultName : stem(wardrobe.skin);
        const std::string signature =
            std::to_string(guiW) + "x" + std::to_string(guiH) + "w" + std::to_string(l.firstLine) + "|" +
            previewKey + "|" + std::to_string(wardrobe.previewSerial) + "|" +
            std::to_string(skinCards.size()) + "," + std::to_string(capeCards.size()) + "|" + wardrobe.skin +
            wardrobe.cape + std::to_string(wardrobe.skinOpen) + std::to_string(wardrobe.capeOpen);
        if (signature != optionsUi.signature || !hud.Overlay().HasOverlay()) {
            optionsUi.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(
                    ui::BuildWardrobeRml(
                        skinCards, capeCards, wardrobe.skinOpen, wardrobe.capeOpen, wardrobe.line,
                        "mchead:ward:cur" + std::to_string(wardrobe.previewSerial), previewName, slimNow,
                        !wardrobe.skin.empty(), !wardrobe.cape.empty(), wt, guiW, guiH),
                    error))
                Log("ui", "wardrobe screen: {}", error);
        }
        hud.Overlay().MouseMove(gx, gy);
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        if (click && !overPreview) {
            const auto folder = [&](const std::string& dir) {
                std::error_code e;
                platform->OpenUrl("file://" + fsys::absolute(dir, e).generic_string());
            };
            const auto removeFile = [&](std::string& current) {
                std::error_code e;
                fsys::rename(current, current + ".removed", e);
                if (options.skin == current)
                    options.skin.clear();
                if (options.cape == current)
                    options.cape.clear();
                wardrobe.slim.erase(current);
                options.skinSlim.erase(current);
                options.Save();
                current.clear();
                wardrobe.listedAt = {};
                wardrobe.previewKey.clear();
            };
            if (inside(l.doneX, l.doneY, l.doneW, 20.0f)) {
                commit();
                wardrobe.init = false;
                pause.view = 0;
                pause.signature.clear();
                optionsUi.signature.clear();
                hud.Overlay().ClearOverlay();
            } else if (inside(l.slimX, l.slimY, l.slimW, 20.0f)) {
                wardrobe.slim[wardrobe.skin] = !slimNow;
                wardrobe.previewKey.clear();
            } else {
                for (const ui::WardrobeLine& line : l.lines) {
                    const bool isSkin = line.kind < 2;
                    if (line.kind % 2 == 0) {
                        if (inside(l.AddX(), line.y, l.addW, l.headH)) {
                            if (!platform->PickFile(isSkin ? "skin" : "cape"))
                                folder(isSkin ? skinDir : capeDir);
                        } else if (inside(l.TrashX(), line.y, l.trashW, l.headH)) {
                            if (isSkin && !wardrobe.skin.empty())
                                removeFile(wardrobe.skin);
                            else if (!isSkin && !wardrobe.cape.empty())
                                removeFile(wardrobe.cape);
                        } else if (inside(l.listX, line.y, l.ToggleW(), l.headH)) {
                            bool& open = isSkin ? wardrobe.skinOpen : wardrobe.capeOpen;
                            open = !open;
                        }
                    } else {
                        const int count = static_cast<int>(isSkin ? skinCards.size() : capeCards.size());
                        for (int c = 0; c < l.cols; c++) {
                            const int i = line.row * l.cols + c;
                            if (i >= count)
                                break;
                            if (!inside(l.CardX(i), line.y, l.cardW, l.cardH))
                                continue;
                            if (isSkin) {
                                wardrobe.skin = skinPaths[static_cast<size_t>(i)];
                            } else {
                                wardrobe.cape = capePaths[static_cast<size_t>(i)];
                            }
                            wardrobe.previewKey.clear();
                            break;
                        }
                    }
                }
            }
        }
        pause.leftDown = down;
    }
}
