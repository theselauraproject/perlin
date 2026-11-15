#include "client/screen/screens/WardrobeController.hpp"

#include "client/systems/i18n/Lang.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/ui/SkinPreview.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "util/Log.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace perlin::screen {
    namespace fsys = std::filesystem;

    namespace {
        using i18n::TrOr;

        std::string StampOf(const std::string& path) {
            std::error_code e;
            return std::to_string(static_cast<unsigned long long>(fsys::file_size(path, e))) + ":" +
                   std::to_string(static_cast<long long>(fsys::last_write_time(path, e).time_since_epoch().count()));
        }

        std::string Stem(const std::string& path) { return fsys::path(path).stem().string(); }

        std::vector<std::string> ListPngs(const std::string& dir) {
            std::vector<std::string> out;
            std::error_code e;
            for (const auto& entry : fsys::directory_iterator(dir, e))
                if (entry.is_regular_file(e) && entry.path().extension() == ".png")
                    out.push_back(entry.path().generic_string());
            std::ranges::sort(out);
            return out;
        }
    }

    bool WardrobeController::Inside(float x, float y, float w, float h) const {
        return in.gx >= x && in.gx < x + w && in.gy >= y && in.gy < y + h;
    }

    void WardrobeController::InitState() {
        if (state.init)
            return;
        state.init = true;
        state.skin = deps.options.skin;
        state.cape = deps.options.cape;
        state.slim = deps.options.skinSlim;
        state.previewKey.clear();
        state.skinOpen = true;
        state.capeOpen = true;
    }

    void WardrobeController::RefreshListing() {
        std::error_code ec;
        fsys::create_directories(skinDir, ec);
        fsys::create_directories(capeDir, ec);
        if (in.now - state.listedAt > std::chrono::milliseconds(1000)) {
            state.listedAt = in.now;
            state.skins = ListPngs(skinDir);
            state.capes = ListPngs(capeDir);
        }
    }

    bool WardrobeController::SlimOf(const std::string& path) const {
        if (const auto it = state.slim.find(path); it != state.slim.end())
            return it->second;
        if (const auto it = state.slimGuess.find(path); it != state.slimGuess.end())
            return it->second;
        return false;
    }

    void WardrobeController::RenderCard(const std::string& key, const std::vector<unsigned char>* skin, int sw,
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
    }

    void WardrobeController::EnsureSkin(const std::string& path) {
        const std::string stamp = StampOf(path);
        auto& slot = state.stamp["skin:" + path];
        if (slot == stamp)
            return;
        slot = stamp;
        SkinConfig cfg;
        cfg.skinPath = path;
        LoadedSkin loaded;
        std::string err;
        if (!LoadSkinConfig(cfg, loaded, err))
            return;
        state.slimGuess[path] = DetectSlimSkin(loaded.skinRgba, loaded.skinWidth, loaded.skinHeight);
        RenderCard("skin:" + path, &loaded.skinRgba, loaded.skinWidth, loaded.skinHeight, SlimOf(path), nullptr, 0, 0,
                   -32.0f);
    }

    void WardrobeController::EnsureCape(const std::string& path) {
        const std::string stamp = StampOf(path);
        auto& slot = state.stamp["cape:" + path];
        if (slot == stamp || !haveSteve)
            return;
        slot = stamp;
        Texture t;
        if (LoadTexture(path, t))
            RenderCard("cape:" + path, &steve.pixels, steve.width, steve.height, false, &t.pixels, t.width, t.height,
                       180.0f);
    }

    void WardrobeController::EnsureDefaultCards() {
        if (state.stamp.find("default") != state.stamp.end() || !haveSteve)
            return;
        state.stamp["default"] = "1";
        RenderCard("skin:default", &steve.pixels, steve.width, steve.height, false, nullptr, 0, 0, -32.0f);
        RenderCard("cape:none", &steve.pixels, steve.width, steve.height, false, nullptr, 0, 0, 180.0f);
    }

    void WardrobeController::Commit() {
        Options& options = deps.options;
        options.skin = state.skin;
        options.cape = state.cape;
        options.skinSlim = state.slim;
        deps.setSkin(deps.activeAccountId(), options.skin, options.cape);
        SkinConfig cfg = MakeSkinConfig(options.skin, options.cape, options.skinSlim);
        LoadedSkin loaded;
        std::string err;
        if (!LoadSkinConfig(cfg, loaded, err)) {
            Log("wardrobe", "{}", err);
            options.Save();
            return;
        }
        options.slimArms = loaded.slimArms;
        options.Save();
        if (deps.live) {
            deps.live->SetSkin(std::move(loaded));
            deps.live->SendSkinUpdate();
        }
    }

    WardrobeController::Cards WardrobeController::BuildCards(const std::string& defaultName) {
        Cards cards;
        cards.skins.push_back({defaultName, "mchead:skin:default", state.skin.empty()});
        cards.skinPaths.push_back("");
        for (const std::string& path : state.skins) {
            EnsureSkin(path);
            cards.skins.push_back({Stem(path), "mchead:skin:" + path, state.skin == path});
            cards.skinPaths.push_back(path);
        }
        cards.capes.push_back({TrOr("perlin.wardrobe.noCape", "No Cape"), "mchead:cape:none", state.cape.empty()});
        cards.capePaths.push_back("");
        for (const std::string& path : state.capes) {
            EnsureCape(path);
            cards.capes.push_back({Stem(path), "mchead:cape:" + path, state.cape == path});
            cards.capePaths.push_back(path);
        }
        if (!state.skin.empty())
            EnsureSkin(state.skin);
        return cards;
    }

    ui::WardrobeLayout WardrobeController::LayoutFor(const Cards& cards) const {
        return ui::ComputeWardrobeLayout(static_cast<int>(cards.skins.size()), static_cast<int>(cards.capes.size()),
                                         state.skinOpen, state.capeOpen, state.line, in.guiW, in.guiH);
    }

    void WardrobeController::ScrollList(const ui::WardrobeLayout& layout) {
        if (in.gx >= layout.listX && in.gx < layout.listX + layout.listW + 6.0f && in.gy >= layout.listTop &&
            in.gy < layout.listBottom) {
            if (in.scroll > 0.0f)
                state.line--;
            else if (in.scroll < 0.0f)
                state.line++;
        }
        state.line = std::clamp(state.line, 0, layout.maxFirst);
    }

    void WardrobeController::DragPreview(const ui::WardrobeLayout&, bool overPreview) {
        if (in.click && overPreview) {
            state.drag = true;
            state.dragX = in.gx;
            state.dragY = in.gy;
        }
        if (!in.down)
            state.drag = false;
        if (state.drag) {
            state.yaw += (in.gx - state.dragX) * 1.6f;
            state.pitch = std::clamp(state.pitch + (in.gy - state.dragY) * 1.2f, -85.0f, 85.0f);
            state.dragX = in.gx;
            state.dragY = in.gy;
        }
        state.yaw = std::fmod(state.yaw, 360.0f);
    }

    std::string WardrobeController::PreviewKey(bool slimNow) const {
        const auto& parts = deps.options.skinParts;
        return state.skin + "|" + state.cape + "|" + std::to_string(slimNow) + "|" +
               std::to_string(static_cast<int>(state.yaw)) + "|" + std::to_string(static_cast<int>(state.pitch)) + "|" +
               state.stamp["skin:" + state.skin] + "|" + state.stamp["cape:" + state.cape] + "|" +
               std::to_string(parts.cape) + std::to_string(parts.jacket) + std::to_string(parts.leftSleeve) +
               std::to_string(parts.rightSleeve) + std::to_string(parts.leftPants) + std::to_string(parts.rightPants) +
               std::to_string(parts.hat) + (state.drag ? "d" : "s");
    }

    void WardrobeController::RefreshPreview(const std::string& previewKey, bool slimNow,
                                            const ui::WardrobeLayout& layout) {
        if (previewKey == state.previewKey)
            return;
        state.previewKey = previewKey;
        SkinConfig cfg;
        cfg.skinPath = state.skin;
        cfg.capePath = state.cape;
        const std::string source = state.skin + "|" + state.cape + "|" + state.stamp["skin:" + state.skin] + "|" +
                                   state.stamp["cape:" + state.cape];
        if (state.previewSource != source) {
            state.previewSource = source;
            state.previewLoaded = {};
            std::string err;
            if (!LoadSkinConfig(cfg, state.previewLoaded, err))
                state.previewLoaded = {};
        }
        const auto& parts = deps.options.skinParts;
        const LoadedSkin& loaded = state.previewLoaded;
        ui::UiAssets::Instance().EraseHead("ward:cur" + std::to_string(state.previewSerial));
        state.previewSerial++;
        ui::SkinView v;
        v.skin = state.skin.empty() ? &steve.pixels : &loaded.skinRgba;
        v.skinW = state.skin.empty() ? steve.width : loaded.skinWidth;
        v.skinH = state.skin.empty() ? steve.height : loaded.skinHeight;
        v.cape = loaded.capeRgba.empty() || !parts.cape ? nullptr : &loaded.capeRgba;
        v.capeW = loaded.capeWidth;
        v.capeH = loaded.capeHeight;
        v.slim = slimNow;
        v.yaw = state.yaw;
        v.pitch = state.pitch;
        v.jacket = parts.jacket;
        v.leftSleeve = parts.leftSleeve;
        v.rightSleeve = parts.rightSleeve;
        v.leftPants = parts.leftPants;
        v.rightPants = parts.rightPants;
        v.hat = parts.hat;
        const float q = state.drag ? 1.5f : 3.0f;
        ui::UiAssets::Instance().SetHead("ward:cur" + std::to_string(state.previewSerial),
                                         ui::RenderSkin3D(v, static_cast<int>(layout.prevW * q),
                                                          static_cast<int>(layout.prevH * q)));
    }

    void WardrobeController::ShowOverlay(const Cards& cards, const ui::WardrobeLayout& layout,
                                         const std::string& previewKey, const std::string& defaultName, bool slimNow) {
        ui::WardrobeText wt;
        wt.title = TrOr("perlin.wardrobe", "Wardrobe");
        wt.skins = TrOr("perlin.wardrobe.skins", "Skins");
        wt.capes = TrOr("perlin.wardrobe.capes", "Capes");
        wt.addSkin = "    " + TrOr("perlin.wardrobe.addSkin", "Add Skin");
        wt.addCape = "    " + TrOr("perlin.wardrobe.addCape", "Add Cape");
        wt.slim = TrOr("perlin.wardrobe.slim", "Slim Arms");
        wt.slimOn = TrOr("options.on", "ON");
        wt.slimOff = TrOr("options.off", "OFF");
        wt.done = TrOr("gui.done", "Done");
        wt.dropHint = TrOr("perlin.wardrobe.drop", "Drop .png files here to add skins and capes");
        wt.dragHint = TrOr("perlin.wardrobe.drag", "Drag to rotate");
        const std::string previewName = state.skin.empty() ? defaultName : Stem(state.skin);
        const std::string signature =
            std::to_string(in.guiW) + "x" + std::to_string(in.guiH) + "w" + std::to_string(layout.firstLine) + "|" +
            previewKey + "|" + std::to_string(state.previewSerial) + "|" + std::to_string(cards.skins.size()) + "," +
            std::to_string(cards.capes.size()) + "|" + state.skin + state.cape + std::to_string(state.skinOpen) +
            std::to_string(state.capeOpen);
        if (signature != deps.overlaySignature || !deps.hud.Overlay().HasOverlay()) {
            deps.overlaySignature = signature;
            std::string error;
            if (!deps.hud.Overlay().SetOverlay(
                    ui::BuildWardrobeScene(cards.skins, cards.capes, state.skinOpen, state.capeOpen, state.line,
                                         "mchead:ward:cur" + std::to_string(state.previewSerial), previewName, slimNow,
                                         !state.skin.empty(), !state.cape.empty(), wt, in.guiW, in.guiH),
                    error))
                Log("ui", "wardrobe screen: {}", error);
        }
        deps.hud.Overlay().MouseMove(in.gx, in.gy);
        deps.hud.UpdateOverlay(in.fbW, in.fbH, deps.options.guiScale);
    }

    void WardrobeController::RemoveFile(std::string& current) {
        Options& options = deps.options;
        std::error_code e;
        fsys::rename(current, current + ".removed", e);
        if (options.skin == current)
            options.skin.clear();
        if (options.cape == current)
            options.cape.clear();
        state.slim.erase(current);
        options.skinSlim.erase(current);
        options.Save();
        current.clear();
        state.listedAt = {};
        state.previewKey.clear();
    }

    WardrobeController::Outcome WardrobeController::HandleClick(const Cards& cards, const ui::WardrobeLayout& l,
                                                                bool slimNow) {
        const auto openFolder = [&](const std::string& dir) {
            std::error_code e;
            deps.openUrl("file://" + fsys::absolute(dir, e).generic_string());
        };
        if (Inside(l.doneX, l.doneY, l.doneW, 20.0f)) {
            Commit();
            state.init = false;
            return Outcome::Done;
        }
        if (Inside(l.slimX, l.slimY, l.slimW, 20.0f)) {
            state.slim[state.skin] = !slimNow;
            state.previewKey.clear();
            return Outcome::Stay;
        }
        for (const ui::WardrobeLine& line : l.lines) {
            const bool isSkin = line.kind < 2;
            if (line.kind % 2 == 0) {
                if (Inside(l.AddX(), line.y, l.addW, l.headH)) {
                    if (!deps.pickFile(isSkin ? "skin" : "cape"))
                        openFolder(isSkin ? skinDir : capeDir);
                } else if (Inside(l.TrashX(), line.y, l.trashW, l.headH)) {
                    if (isSkin && !state.skin.empty())
                        RemoveFile(state.skin);
                    else if (!isSkin && !state.cape.empty())
                        RemoveFile(state.cape);
                } else if (Inside(l.listX, line.y, l.ToggleW(), l.headH)) {
                    bool& open = isSkin ? state.skinOpen : state.capeOpen;
                    open = !open;
                }
                continue;
            }
            const int count = static_cast<int>(isSkin ? cards.skins.size() : cards.capes.size());
            for (int c = 0; c < l.cols; c++) {
                const int i = line.row * l.cols + c;
                if (i >= count)
                    break;
                if (!Inside(l.CardX(i), line.y, l.cardW, l.cardH))
                    continue;
                if (isSkin)
                    state.skin = cards.skinPaths[static_cast<size_t>(i)];
                else
                    state.cape = cards.capePaths[static_cast<size_t>(i)];
                state.previewKey.clear();
                break;
            }
        }
        return Outcome::Stay;
    }

    WardrobeController::Outcome WardrobeController::Update(const WardrobeFrame& frame) {
        in = frame;
        InitState();
        skinDir = paths::DataDir() + "/skins";
        capeDir = paths::DataDir() + "/capes";
        RefreshListing();
        haveSteve = ui::UiAssets::Instance().Image("textures/entity/steve.png", steve);
        EnsureDefaultCards();
        const std::string defaultName = TrOr("perlin.wardrobe.default", "Default");
        const Cards cards = BuildCards(defaultName);
        ui::WardrobeLayout layout = LayoutFor(cards);
        ScrollList(layout);
        if (state.line != layout.firstLine)
            layout = LayoutFor(cards);
        const bool overPreview = Inside(layout.prevX, layout.prevY, layout.prevW, layout.prevH);
        DragPreview(layout, overPreview);
        const bool slimNow = SlimOf(state.skin);
        const std::string previewKey = PreviewKey(slimNow);
        RefreshPreview(previewKey, slimNow, layout);
        ShowOverlay(cards, layout, previewKey, defaultName, slimNow);
        if (in.click && !overPreview)
            return HandleClick(cards, layout, slimNow);
        return Outcome::Stay;
    }
}
