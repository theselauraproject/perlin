#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

// builds every screen a ui stack defines, reports what the engine couldn't parse: check server packs without joining
//
//   ui_conformance <label>:<ui root> [<label>:<ui root> ...]
//
// roots layered like the client: pack roots first, then data ui folder and vanilla files
// env: VANILLA_UI=<vanilla resource_pack/ui> (default: bedrock samples install), DATA_UI=<data dir>/ui
#include "client/ui/HudData.hpp"
#include "client/ui/JsonUi.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/UiLayout.hpp"
#include "util/Paths.hpp"

using namespace perlin;
using namespace perlin::ui;

namespace {
    std::string ReadAll(const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // screens a stack defines: namespace + control names ending in "_screen"
    std::vector<std::string> ScreensIn(const std::string& root) {
        std::vector<std::string> out;
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator
                 it(root, std::filesystem::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".json")
                continue;
            Json document;
            std::string error;
            if (!Json::Parse(ReadAll(it->path()), document, error) || !document.IsObject())
                continue;
            const Json* ns = document.Find("namespace");
            if (!ns || !ns->IsString())
                continue;
            for (const auto& [key, value] : document.object) {
                if (!value.IsObject())
                    continue;
                // A template suffix is inheritance, not part of the control's name.
                const std::string name = key.substr(0, key.find('@'));
                if (!name.contains('/') && name.ends_with("_screen"))
                    out.push_back(ns->string + "." + name);
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    class Globals : public UiData {
    public:
        bool Global(const std::string&, Json&) const override { return false; }
        int CollectionSize(const std::string&) const override { return 0; }
        bool CollectionValue(const std::string&, int, const std::string&, Json&) const override {
            return false;
        }
        std::vector<FactoryItem> Factory(const std::string&) const override { return {}; }
    };

    struct Stats {
        int screens = 0, built = 0, failed = 0, missingControls = 0, unresolved = 0, controls = 0,
            nullText = 0, rawText = 0;
        std::set<std::string> missing;
        std::set<std::string> failedRefs;
    };

    void CountText(const LNode& n, Stats& s) {
        if (n.type == "label" && !n.text.empty()) {
            if (n.text == "null")
                s.nullText++;
            // undecoded text: raw "$var", "#binding" or control reference left in a label
            if (n.text[0] == '$' || n.text[0] == '#' || (n.text.starts_with('@') && n.text.contains('.')))
                s.rawText++;
        }
        for (const auto& k : n.kids)
            CountText(*k, s);
    }
}

int main(int argc, char** argv) {
    std::vector<std::pair<std::string, std::string>> stacks;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        const size_t colon = arg.find(':');
        if (colon == std::string::npos)
            continue;
        stacks.emplace_back(arg.substr(0, colon), arg.substr(colon + 1));
    }
    if (stacks.empty()) {
        std::fprintf(stderr, "usage: ui_conformance <label>:<ui root> [...]\n");
        return 2;
    }

    const char* vanillaEnv = std::getenv("VANILLA_UI");
    const std::string vanilla =
        vanillaEnv ? vanillaEnv : paths::DataDir() + "/bedrock-samples/repo/resource_pack/ui";
    const std::string dataUi = paths::DataDir() + "/ui";
    const std::string dataDefaults = paths::DataDir() + "/ui_defaults";

    McFont::Instance().UseBuiltin();
    TenFont::Instance().EnsureLoaded();

    UiServices services;
    services.textWidth = [](const std::string& text, float s, bool ten) {
        return ten ? TenFont::Instance().Width(text, s) : McFont::Instance().Width(text, s);
    };
    services.lineHeight = [](float s, bool ten) {
        return ten ? TenFont::Instance().LineHeight(s) : static_cast<float>(McFont::kLineHeight) * s;
    };
    // checks layout, bindings and emitted text, not textures or translations

    int totalFailed = 0;
    for (const auto& [label, root] : stacks) {
        std::vector<std::string> roots = {root, dataUi, dataDefaults, vanilla};
        UiLibrary library;
        library.SetRoots(roots);
        if (!library.Scan()) {
            std::printf("%-24s no ui files\n", label.c_str());
            continue;
        }
        std::vector<std::string> screens = ScreensIn(root);
        Stats stats;
        for (const std::string& reference : screens) {
            BuildReport report;
            auto tree = BuildTree(library, reference, EngineRootVariables(), report);
            stats.screens++;
            if (!tree) {
                stats.failed++;
                stats.failedRefs.insert(reference);
                continue;
            }
            for (const std::string& m : report.missingControls)
                stats.missing.insert(m);
            stats.missingControls += static_cast<int>(report.missingControls.size());
            stats.unresolved += static_cast<int>(report.unresolvedVariables.size());
            Globals data;
            auto root2 = EvaluateTree(*tree, data, services, 854.0f, 480.0f);
            if (!root2) {
                stats.failed++;
                stats.failedRefs.insert(reference);
                continue;
            }
            stats.built++;
            CountText(*root2, stats);
        }
        std::printf("%-24s screens=%-4d built=%-4d failed=%-3d missing=%-4d unresolved=%-4d nullText=%-3d "
                    "rawText=%-3d\n",
                    label.c_str(), stats.screens, stats.built, stats.failed, stats.missingControls,
                    stats.unresolved, stats.nullText, stats.rawText);
        for (const std::string& m : stats.missing)
            std::printf("    missing control: %s\n", m.c_str());
        for (const std::string& f : stats.failedRefs)
            std::printf("    build failed: %s\n", f.c_str());
        totalFailed += stats.failed;
    }
    return totalFailed == 0 ? 0 : 1;
}
