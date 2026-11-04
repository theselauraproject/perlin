#include "client/ui/JsonUi.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace perlin::ui;
namespace fs = std::filesystem;
int main() {
    const auto dir =
        fs::temp_directory_path() /
        ("perlin-ui-library-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir / "base");
    fs::create_directories(dir / "pack");
    std::ofstream(dir / "base/hud_screen.json")
        << R"({"namespace":"hud","hud_screen":{"type":"panel","size":[100,100],"controls":[{"sidebar":{"type":"label","text":"original"}}]}})";
    // A pack hides the built-in sidebar and supplies its own HUD background.
    std::ofstream(dir / "pack/hud_screen.json")
        << R"({"root_panel/sidebar":{"ignored":true},"hud_screen":{"marker":"pack-loaded"}})";
    std::ofstream(dir / "pack/late.json")
        << "{\"padding\":\"" << std::string(5000, 'x')
        << R"(","namespace":"late","screen":{"type":"label","text":"late-loaded"}})";
    UiLibrary library;
    library.SetRoots({(dir / "pack").string(), (dir / "base").string()});
    BuildReport report;
    const bool scanned = library.Scan();
    auto tree = BuildTree(library, "hud.hud_screen", {}, report);
    const Json* marker = tree ? tree->props.Find("marker") : nullptr;
    const auto* nested = library.Nested("hud");
    const bool ok = scanned && tree && marker && marker->StringOr("") == "pack-loaded" &&
                    tree->type == "panel" && tree->children.size() == 1 && nested &&
                    nested->count("root_panel") && library.Has("late") && library.DefinedBefore("hud", 1) &&
                    report.missingControls.empty();
    fs::remove_all(dir);
    if (!ok)
        std::cerr << "namespace overlay/late namespace regression\n";
    return ok ? 0 : 1;
}
