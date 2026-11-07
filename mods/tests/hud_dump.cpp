#include "client/ui/HudData.hpp"
#include "client/ui/HudRenderers.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/RmlEmit.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace perlin::ui;

static bool PngSize(const std::string& path, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    unsigned char b[24];
    if (!f.read(reinterpret_cast<char*>(b), 24))
        return false;
    w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
    h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
    return true;
}

static void Props(const Node& n, const std::string& name) {
    if (n.name == name)
        std::fprintf(stderr, "PROPS %s: %s\n", n.path.c_str(), n.props.Dump().c_str());
    for (auto& k : n.children)
        Props(*k, name);
}

static void Tree(const LNode& n, int depth, bool onlyVisible) {
    if (onlyVisible && !n.visible)
        return;
    std::fprintf(stderr, "%*s%s [%s]%s  abs=(%.1f,%.1f) %.1fx%.1f L%d%s\n", depth * 2, "", n.id.c_str(),
                 n.type.c_str(), n.visible ? "" : " HIDDEN", n.ax, n.ay, n.w, n.h, n.layer,
                 n.text.empty() ? "" : (" text=" + n.text).c_str());
    for (auto& k : n.kids)
        Tree(*k, depth + 1, onlyVisible);
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--boss-selftest") {
        HudState state;
        HudData data(state);
        Json value;
        const auto initial = HudSignature(state);
        if (data.CollectionSize("boss_bars") != 0 || !data.Global("#boss_grid_dimension", value) ||
            value.array.size() != 2 || value.array[1].number != 0)
            return 1;
        state.bossBars.push_back({"native-title", 0.25f, 1, 0});
        if (data.CollectionSize("boss_bars") != 1 || HudSignature(state) == initial)
            return 1;
        if (!data.CollectionValue("boss_bars", 0, "#bossName", value) || value.string != "native-title")
            return 1;
        if (!data.CollectionValue("boss_bars", 0, "#progress_percentage", value) || value.number != 0.25)
            return 1;
        if (!data.CollectionValue("boss_bars", 1, "#bar_visible", value) || value.boolean)
            return 1;
        const auto beforeProgress = HudSignature(state);
        state.bossBars[0].progress = 0.75f;
        if (HudSignature(state) == beforeProgress)
            return 1;
        state.bossBars.clear();
        return HudSignature(state) == initial && data.CollectionSize("boss_bars") == 0 ? 0 : 1;
    }

    const std::string pack = argc > 1 ? argv[1] : "/tmp/pdata/Perlin/bedrock-samples/repo/resource_pack";
    const float W = argc > 2 ? std::atof(argv[2]) : 427, H = argc > 3 ? std::atof(argv[3]) : 240;
    UiLibrary lib;
    lib.SetRoots({pack + "/ui"});
    if (!lib.Scan()) {
        std::fprintf(stderr, "no ui files\n");
        return 1;
    }
    BuildReport report;
    auto tree = BuildTree(lib, "hud.hud_screen", EngineRootVariables(), report);
    if (!tree) {
        std::fprintf(stderr, "build failed\n");
        return 1;
    }
    std::fprintf(stderr, "nodes=%zu missing=%zu unresolved=%zu\n", report.nodes,
                 report.missingControls.size(), report.unresolvedVariables.size());
    for (auto& m : report.missingControls)
        std::fprintf(stderr, "  missing control: %s\n", m.c_str());
    for (auto& m : report.unresolvedVariables)
        std::fprintf(stderr, "  unresolved var: %s\n", m.c_str());

    if (const char* p = std::getenv("HUD_PROPS"))
        Props(*tree, p);
    HudState state;
    state.health = 20;
    state.food = 20;
    state.armor = 7;
    state.xpProgress = 0.55f;
    state.xpLevel = 12;
    for (int i = 0; i < 9; i++) {
        state.hotbar[i].name = i == 8 ? "" : "minecraft:stone";
        state.hotbar[i].count = i * 7;
    }
    state.selectedSlot = 2;
    if (const char* t = std::getenv("HUD_TITLE")) {
        state.title = t;
        state.subtitle = "sub";
        state.titleAlpha = 1;
        state.actionBar = "bar";
        state.actionBarAlpha = 1;
    }
    if (argc > 4)
        state.itemName = argv[4], state.itemNameAlpha = 1;
    HudData data(state);
    UiServices services;
    services.textWidth = [](const std::string& t, float s, bool ten) {
        return ten ? TenFont::Instance().Width(t, s) : McFont::Instance().Width(t, s);
    };
    services.lineHeight = [](float s, bool ten) {
        return ten ? TenFont::Instance().LineHeight(s) : McFont::kLineHeight * s;
    };
    services.textureSize = [&](const std::string& tex, int& w, int& h) {
        std::string p = pack + "/" + tex;
        if (p.size() < 4 || p.substr(p.size() - 4) != ".png")
            p += ".png";
        return PngSize(p, w, h);
    };
    services.localize = [](const std::string& k) { return k; };
    auto eval = EvaluateTree(*tree, data, services, W, H);
    if (std::getenv("HUD_TREE"))
        Tree(*eval, 0, std::atoi(std::getenv("HUD_TREE")) == 1);
    RmlEmitter emitter;
    RegisterHudRenderers(emitter, state, services);
    std::string rml = emitter.Emit(*eval, W, H, "", true);
    std::fputs(rml.c_str(), stdout);
    for (auto& u : emitter.UnknownRenderers())
        std::fprintf(stderr, "  unknown renderer: %s\n", u.c_str());
    return 0;
}
