#include <chrono>
// builds any json ui screen offline, prints what the engine made of it
//
//   screen_dump <reference> <W> <H> <ui root> [<ui root> ...]
//
// roots highest priority first, like the client (server packs, resource packs, vanilla)
// env: SCREEN_TREE=1 prints visible controls, =2 hidden too; SCREEN_RML=1 prints emitted rml
// SCREEN_VARS='{"#binding": value, ...}' feeds global bindings
#include "client/ui/HudData.hpp"
#include "client/ui/JsonUi.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiLayout.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace perlin::ui;

namespace {
    bool PngSize(const std::string& path, int& w, int& h) {
        std::ifstream f(path, std::ios::binary);
        unsigned char b[24];
        if (!f.read(reinterpret_cast<char*>(b), 24))
            return false;
        w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
        h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
        return true;
    }

    void Tree(const LNode& n, int depth, bool hidden) {
        if (!hidden && !n.visible)
            return;
        std::string extra;
        if (!n.text.empty())
            extra += " text=" + n.text;
        if (const Json* t = n.Get("texture"); t && t->IsString())
            extra += " tex=" + t->string;
        if (const Json* maps = n.Get("button_mappings"); maps && maps->IsArray())
            for (const Json& m : maps->array)
                if (const Json* to = m.IsObject() ? m.Find("to_button_id") : nullptr; to && to->IsString())
                    extra += " btn=" + to->string;
        if (n.index >= 0)
            extra += " idx=" + std::to_string(n.index);
        std::fprintf(stderr, "%*s%s [%s]%s  abs=(%.1f,%.1f) %.1fx%.1f L%d%s\n", depth * 2, "", n.id.c_str(),
                     n.type.c_str(), n.visible ? "" : " HIDDEN", n.ax, n.ay, n.w, n.h, n.layer,
                     extra.c_str());
        for (auto& k : n.kids)
            Tree(*k, depth + 1, hidden);
    }

    void Props(const Node& n, const std::string& name) {
        if (n.name == name) {
            std::fprintf(stderr, "PROPS %s: %s\n", n.path.c_str(), n.props.Dump().c_str());
            for (const Json& b : n.bindings)
                std::fprintf(stderr, "  BINDING %s\n", b.Dump().c_str());
        }
        for (auto& k : n.children)
            Props(*k, name);
        if (n.gridTemplate)
            Props(*n.gridTemplate, name);
        for (auto& f : n.factoryControls)
            Props(*f.second, name);
    }

    // SCREEN_VARS: {"#name": value}; SCREEN_COLLECTIONS: {"form_buttons": [{"#form_button_text": "x"}]};
    // SCREEN_FACTORY: {"buttons": [["button", "form_buttons", 0]], "server_form_factory": [["long_form"]]}.
    class Globals : public UiData {
    public:
        Json vars = Json::MakeObject(), collections = Json::MakeObject(), factories = Json::MakeObject();
        bool Global(const std::string& name, Json& out) const override {
            const Json* v = vars.Find(name);
            if (!v)
                return false;
            out = *v;
            return true;
        }
        int CollectionSize(const std::string& c) const override {
            const Json* rows = collections.Find(c);
            return rows && rows->IsArray() ? static_cast<int>(rows->array.size()) : 0;
        }
        bool CollectionValue(const std::string& c, int index, const std::string& binding,
                             Json& out) const override {
            const Json* rows = collections.Find(c);
            if (!rows || !rows->IsArray() || index < 0 || index >= static_cast<int>(rows->array.size()))
                return false;
            const Json* v = rows->array[static_cast<size_t>(index)].Find(binding);
            if (!v)
                return false;
            out = *v;
            return true;
        }
        std::vector<FactoryItem> Factory(const std::string& name) const override {
            std::vector<FactoryItem> items;
            if (const Json* list = factories.Find(name); list && list->IsArray())
                for (const Json& e : list->array) {
                    if (!e.IsArray() || e.array.empty())
                        continue;
                    FactoryItem item;
                    item.id = e.array[0].StringOr("");
                    if (e.array.size() > 2) {
                        item.collection = e.array[1].StringOr("");
                        item.index = static_cast<int>(e.array[2].number);
                    }
                    items.push_back(item);
                }
            return items;
        }
    };
}

namespace {
    size_t CountNodes(const Node& n) {
        size_t c = 1;
        for (const auto& ch : n.children)
            c += CountNodes(*ch);
        if (n.gridTemplate)
            c += CountNodes(*n.gridTemplate);
        for (const auto& f : n.factoryControls)
            c += CountNodes(*f.second);
        return c;
    }
    // SCREEN_HEAVY=N: lists template subtrees with at least N nodes
    void Heavy(const Node& n, size_t min, int depth) {
        const size_t c = CountNodes(n);
        if (c < min)
            return;
        std::fprintf(stderr, "%*s%zu %s [%s]\n", depth * 2, "", c, n.name.c_str(), n.type.c_str());
        for (const auto& ch : n.children)
            Heavy(*ch, min, depth + 1);
        if (n.gridTemplate)
            Heavy(*n.gridTemplate, min, depth + 1);
        for (const auto& f : n.factoryControls)
            Heavy(*f.second, min, depth + 1);
    }
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: screen_dump <namespace.screen> <W> <H> <ui root>...\n");
        return 2;
    }
    const std::string reference = argv[1];
    const float W = std::atof(argv[2]), H = std::atof(argv[3]);
    std::vector<std::string> roots(argv + 4, argv + argc);

    using Clock = std::chrono::steady_clock;
    auto mark = Clock::now();
    auto phase = [&](const char* name) {
        const auto t = Clock::now();
        std::fprintf(stderr, "  [time] %s %.0f ms\n", name,
                     std::chrono::duration<double, std::milli>(t - mark).count());
        mark = t;
    };
    UiLibrary lib;
    lib.SetRoots(roots);
    if (!lib.Scan()) {
        std::fprintf(stderr, "no ui files\n");
        return 1;
    }
    phase("scan");
    BuildReport report;
    auto tree = BuildTree(lib, reference, EngineRootVariables(), report);
    phase("build");
    for (auto& p : lib.Problems())
        std::fprintf(stderr, "  problem: %s\n", p.c_str());
    if (!tree) {
        std::fprintf(stderr, "build failed: %s\n", reference.c_str());
        return 1;
    }
    std::fprintf(stderr, "%s: nodes=%zu missing=%zu unresolved=%zu files=%zu\n", reference.c_str(),
                 report.nodes, report.missingControls.size(), report.unresolvedVariables.size(),
                 lib.FileCount());
    for (auto& m : report.missingControls)
        std::fprintf(stderr, "  missing control: %s\n", m.c_str());
    for (auto& m : report.unresolvedVariables)
        std::fprintf(stderr, "  unresolved var: %s\n", m.c_str());

    if (const char* h = std::getenv("SCREEN_HEAVY"))
        Heavy(*tree, std::strtoul(h, nullptr, 10), 0);
    if (const char* p = std::getenv("SCREEN_PROPS"))
        Props(*tree, p);
    Globals data;
    for (auto [env, target] :
         {std::pair{"SCREEN_VARS", &data.vars}, std::pair{"SCREEN_COLLECTIONS", &data.collections},
          std::pair{"SCREEN_FACTORY", &data.factories}})
        if (const char* v = std::getenv(env)) {
            Json parsed;
            std::string err;
            if (Json::Parse(v, parsed, err) && parsed.IsObject())
                *target = parsed;
            else
                std::fprintf(stderr, "%s: %s\n", env, err.c_str());
        }

    UiServices services;
    services.textWidth = [](const std::string& t, float s, bool ten) {
        return ten ? TenFont::Instance().Width(t, s) : McFont::Instance().Width(t, s);
    };
    services.lineHeight = [](float s, bool ten) {
        return ten ? TenFont::Instance().LineHeight(s) : McFont::kLineHeight * s;
    };
    services.textureSize = [&](const std::string& tex, int& w, int& h) {
        for (const std::string& r : roots) {
            std::string p = r + "/../" + tex;
            if (p.size() < 4 || p.substr(p.size() - 4) != ".png")
                p += ".png";
            if (PngSize(p, w, h))
                return true;
        }
        return false;
    };
    services.localize = [](const std::string& k) { return k; };

    auto eval = EvaluateTree(*tree, data, services, W, H);
    phase("evaluate");
    if (!eval) {
        std::fprintf(stderr, "evaluate failed\n");
        return 1;
    }
    if (const char* t = std::getenv("SCREEN_TREE"))
        Tree(*eval, 0, std::atoi(t) == 2);
    RmlEmitter emitter;
    const std::string rml = emitter.Emit(*eval, W, H, "", true);
    phase("emit");
    if (std::getenv("SCREEN_RML"))
        std::fputs(rml.c_str(), stdout);
    for (auto& u : emitter.UnknownRenderers())
        std::fprintf(stderr, "  unknown renderer: %s\n", u.c_str());
    return 0;
}
