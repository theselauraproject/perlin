#include "client/screen/screens/TitleSupport.hpp"

#include "client/screen/screens/ScreenModels.hpp"
#include "client/systems/resources/SkinLoader.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/ui/JsonScreen.hpp"
#include "client/ui/SkinPreview.hpp"
#include "util/Env.hpp"
#include "util/JsonValue.hpp"
#include "util/Paths.hpp"
#include "util/FileIo.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>

namespace perlin::screen::title {

    ui::ScreenData ForcedScreenData(const std::string& reference, const std::string& playerName) {
        if (reference == "server_form.third_party_server_screen") {

            ui::ScreenData d;

            if (const char* file = PERLIN_ENV("PERLIN_TEST_FORM")) {
                const std::string contents = file::Read(file);
                json::Value form;
                std::string err;
                if (json::Value::Parse(contents, form, err) && form.IsObject()) {
                    d.Set("#title_text", json::Value(form.Find("title") ? form.Find("title")->StringOr("")
                                                                     : std::string()));
                    d.Set("#form_text", json::Value(form.Find("content") ? form.Find("content")->StringOr("")
                                                                      : std::string()));
                    const json::Value* list =
                        form.Find("elements") ? form.Find("elements") : form.Find("buttons");
                    int i = 0;
                    if (list && list->IsArray())
                        for (const json::Value& e : list->array) {
                            const json::Value* image = e.Find("image");
                            const json::Value* data = image ? image->Find("data") : nullptr;
                            d.Collection("form_buttons")
                                .push_back({{"#form_button_text",
                                             json::Value(e.Find("text") ? e.Find("text")->StringOr("")
                                                                     : std::string())},
                                            {"#form_button_texture",
                                             json::Value(data ? data->StringOr("") : std::string())}});
                            d.FactoryItems("buttons").push_back({"button", {}, "form_buttons", i++});
                        }
                    d.Set("#form_button_length", json::Value(static_cast<double>(i)));
                    d.FactoryItems("server_form_factory").push_back({"long_form", {}, {}, -1});
                    return d;
                }
            }
            const char* title = PERLIN_ENV("PERLIN_TEST_FORM_TITLE");
            d.Set("#title_text", std::string(title ? title : "&game_selectorSelect a game"));
            d.Set("#form_text", std::string(""));
            static const char* kGames[] = {"Lobby",         "SkyWars",        "EggWars",      "Bed Wars",
                                           "Tower Defence", "Lucky Islands",  "Free For All", "Parkour",
                                           "Block Wars",    "Survival Games", "Duels",        "Skyblock"};
            for (int i = 0; i < 30; i++) {
                const std::string name = kGames[i % 12];
                d.Collection("form_buttons")
                    .push_back({{"#form_button_text",
                                 json::Value(name + "\n" + std::to_string(100 + i * 37) + " playing")},
                                {"#form_button_texture", json::Value(std::string())}});
                d.FactoryItems("buttons").push_back({"button", {}, "form_buttons", i});
            }
            d.FactoryItems("server_form_factory").push_back({"long_form", {}, {}, -1});
            return d;
        }
        if (reference == "hud.hud_screen") {

            ui::ScreenData d;
            d.Set("#hud_visible", true);
            d.Set("#scoreboard_sidebar_visible", true);
            d.Set("#objective_sidebar_name", std::string(PERLIN_ENV("PERLIN_TEST_SIDEBAR_TITLE")
                                                             ? PERLIN_ENV("PERLIN_TEST_SIDEBAR_TITLE")
                                                             : "CubeCraft"));
            const std::vector<std::pair<std::string, int>> rows = {
                {"\u00a7bUser: \u00a7f" + playerName, 8},
                {"\u00a7bVIP Level: \u00a7f15", 7},
                {"\u00a7bLevel: \u00a7f17", 6},
                {"\u00a7bLobby: \u00a7fEU #11", 5},
                {"\u00a7bPlayers: \u00a7f7,183", 4},
                {" ", 3},
                {"\u00a77"
                 "28/10/26 (de0ax)",
                 2},
                {"\u00a76cubecraft.net", 1}};
            d.Set("#scoreboard_sidebar_size", static_cast<double>(rows.size()));
            for (const auto& [name, score] : rows) {
                d.Collection("scoreboard_players").push_back({{"#player_name_sidebar", json::Value(name)}});
                d.Collection("scoreboard_scores")
                    .push_back({{"#player_score_sidebar", json::Value(std::to_string(score))}});
            }
            return d;
        }
        if (reference == "pause.pause_screen") {
            ui::PauseModel pm;
            pm.playerName = playerName;
            pm.listTitle = "Player List";
            pm.players = {{playerName, 2, true}, {"Steve", 1, false}, {"Alex", 0, false}};
            return ui::PauseScreenData(pm);
        }
        return {};
    }
    std::string ServersPath() {
        return paths::DataDir() + "/servers.json";
    }
    void LoadServers(std::vector<ui::ServerEntry>& out, const std::string& fallback) {
        out.clear();
        std::string serversText;
        if (!file::Read(ServersPath(), serversText)) {
            if (!fallback.empty() && fallback != "127.0.0.1:19132")
                out.push_back({.name = "Minecraft Server", .address = fallback});
            return;
        }
        json::Value json;
        std::string error;
        if (!json::Value::Parse(serversText, json, error))
            return;
        const json::Value* list = json.Find("servers");
        if (!list || !list->IsArray())
            return;
        for (const json::Value& entry : list->array) {
            const json::Value* name = entry.Find("name");
            const json::Value* ip = entry.Find("ip");
            if (ip && ip->IsString() && !ip->string.empty())
                out.push_back({.name = name ? name->StringOr("") : std::string(), .address = ip->string});
        }
    }
    void SaveServers(const std::vector<ui::ServerEntry>& servers) {
        json::Value list = json::Value::MakeArray();
        for (const ui::ServerEntry& s : servers) {
            json::Value entry = json::Value::MakeObject();
            entry.Set("name", json::Value(s.name));
            entry.Set("ip", json::Value(s.address));
            list.Push(std::move(entry));
        }
        json::Value root = json::Value::MakeObject();
        root.Set("servers", std::move(list));
        std::error_code ec;
        std::filesystem::create_directories(paths::DataDir(), ec);
        std::ofstream(ServersPath(), std::ios::binary) << root.Dump(2);
    }

    std::string SkinDollKey(ui::UiAssets& assets, const std::string& skinPath, const std::string& capePath, bool showCape,
                            float headYaw, float headPitch, std::string& lastKey) {

        const int qy = static_cast<int>(std::lround(headYaw / 3.0f)) * 3,
                  qp = static_cast<int>(std::lround(headPitch / 3.0f)) * 3;
        const std::string key =
            "doll:" +
            std::to_string(std::hash<std::string>{}(skinPath + "|" + capePath + (showCape ? "c" : "n"))) +
            ":" + std::to_string(qy) + ":" + std::to_string(qp);
        if (assets.HasHead(key))
            return key;
        if (!lastKey.empty())
            assets.EraseHead(lastKey);
        lastKey = key;
        LoadedSkin loaded;
        if (!skinPath.empty()) {
            SkinConfig cfg;
            cfg.skinPath = skinPath;
            cfg.capePath = capePath;
            std::string err;
            if (!LoadSkinConfig(cfg, loaded, err))
                loaded = {};
        }
        Texture steve;
        if (loaded.skinRgba.empty()) {
            if (!assets.Image("textures/entity/steve.png", steve) || steve.width < 64)
                return {};
        }
        ui::SkinView v;
        v.skin = loaded.skinRgba.empty() ? &steve.pixels : &loaded.skinRgba;
        v.skinW = loaded.skinRgba.empty() ? steve.width : loaded.skinWidth;
        v.skinH = loaded.skinRgba.empty() ? steve.height : loaded.skinHeight;
        v.slim = ui::GuessSlimSkin(*v.skin, v.skinW, v.skinH);
        if (showCape && !loaded.capeRgba.empty()) {
            v.cape = &loaded.capeRgba;
            v.capeW = loaded.capeWidth;
            v.capeH = loaded.capeHeight;
        }
        v.yaw = -20.0f;
        v.pitch = 0.0f;
        v.headYaw = static_cast<float>(qy);
        v.headPitch = static_cast<float>(qp);
        assets.SetHead(key, ui::RenderSkin3D(v, 180, 300));
        return key;
    }
    std::string SkinHeadKey(ui::UiAssets& assets, const std::string& skinPath) {
        const std::string key = "skinhead:" + std::to_string(std::hash<std::string>{}(skinPath));
        if (assets.HasHead(key))
            return key;
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!skinPath.empty()) {
            SkinConfig cfg;
            cfg.skinPath = skinPath;
            LoadedSkin loaded;
            std::string err;
            if (LoadSkinConfig(cfg, loaded, err)) {
                rgba = std::move(loaded.skinRgba);
                w = loaded.skinWidth;
                h = loaded.skinHeight;
            }
        }
        if (rgba.empty()) {
            Texture steve;
            if (!assets.Image("textures/entity/steve.png", steve) || steve.width < 64)
                return {};
            rgba = std::move(steve.pixels);
            w = steve.width;
            h = steve.height;
        }
        const int sc = std::max(1, w / 64);
        Texture t;
        t.width = t.height = 8;
        t.pixels.assign(8 * 8 * 4, 255);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const auto px = [&](int ox, int oy, uint8_t out[4]) {
                    const size_t i =
                        (static_cast<size_t>((oy + y) * sc + sc / 2) * static_cast<size_t>(w) +
                         static_cast<size_t>((ox + x) * sc + sc / 2)) *
                        4;
                    for (int c = 0; c < 4; c++)
                        out[c] = i + 3 < rgba.size() ? rgba[i + c] : 255;
                };
                uint8_t face[4], hat[4];
                px(8, 8, face);
                px(40, 8, hat);
                if (h < 32)
                    hat[3] = 0;
                uint8_t* o = &t.pixels[static_cast<size_t>(y * 8 + x) * 4];
                const float a = hat[3] / 255.0f;
                for (int c = 0; c < 3; c++)
                    o[c] = static_cast<uint8_t>(face[c] * (1.0f - a) + hat[c] * a);
                o[3] = 255;
            }
        assets.SetHead(key, std::move(t));
        return key;
    }

    void ScrollList(int& scroll, int selected, int count, int rows, float wheel) {
        if (wheel != 0.0f)
            scroll -= wheel > 0 ? 1 : -1;
        if (selected >= 0) {
            scroll = std::min(scroll, selected);
            scroll = std::max(scroll, selected - rows + 1);
        }
        scroll = std::clamp(scroll, 0, std::max(0, count - rows));
    }

    bool IsReplayPage(ui::TitlePage page) {
        return page == ui::TitlePage::Replays || page == ui::TitlePage::ReplayRename ||
               page == ui::TitlePage::ReplayDelete;
    }

    bool IsServerFieldPage(ui::TitlePage page) {
        return page == ui::TitlePage::Edit || page == ui::TitlePage::Direct || page == ui::TitlePage::AddRealm;
    }

    ui::TitlePage ParentPage(ui::TitlePage page) {
        switch (page) {
        case ui::TitlePage::Edit:
        case ui::TitlePage::Direct:
        case ui::TitlePage::AddRealm:
            return ui::TitlePage::Servers;
        case ui::TitlePage::ReplayRename:
        case ui::TitlePage::ReplayDelete:
            return ui::TitlePage::Replays;
        case ui::TitlePage::AccountName:
        case ui::TitlePage::AccountSignIn:
            return ui::TitlePage::Accounts;
        case ui::TitlePage::Servers:
        case ui::TitlePage::Accounts:
        case ui::TitlePage::Replays:
            return ui::TitlePage::Main;
        default:
            return page;
        }
    }

    void ApplyPingResult(ui::ServerEntry& entry, const ui::ServerEntry& result, bool withMotd) {
        entry.status = result.status;
        if (withMotd)
            entry.motd = result.motd;
        entry.version = result.version;
        entry.players = result.players;
        entry.maxPlayers = result.maxPlayers;
        entry.pingMs = result.pingMs;
    }

    ui::ServerEntry SectionHeader(std::string name, size_t count, int sectionId) {
        ui::ServerEntry head;
        head.kind = ui::ServerEntry::Kind::Header;
        head.name = std::move(name) + " [" + std::to_string(count) + "]";
        head.realmId = sectionId;
        return head;
    }
}
