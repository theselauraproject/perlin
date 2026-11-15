#include "client/platform/EmbeddedProvider.hpp"
#include "util/ResourcePaths.hpp"
#include "util/Resources.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
    int failures = 0;
    int checks = 0;

    void Check(bool ok, const std::string& what) {
        checks++;
        if (!ok) {
            failures++;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    std::string Slurp(const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), {}};
    }
}

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    const fs::path root = argv[1];
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file())
            files.push_back(e.path());
    Check(files.size() > 40, "resource tree has files");

    const fs::path work = fs::temp_directory_path() / "perlin_resources_selftest";
    fs::remove_all(work);
    fs::create_directories(work / "data");
    fs::create_directories(work / "cwd");
    fs::current_path(work / "cwd");
    setenv("PERLIN_DATA_DIR", (work / "data").string().c_str(), 1);
    unsetenv("PERLIN_RESOURCES");

    std::string out;
    Check(!perlin::resources::Read("ui/screens/pause_screen.rml", out), "no provider and no disk: nothing found");

    perlin::RegisterEmbeddedResources();
    for (const fs::path& f : files) {
        const std::string rel = fs::relative(f, root).generic_string();
        std::string embedded;
        Check(perlin::resources::Read(rel, embedded) && embedded == Slurp(f), "embedded copy of " + rel + " matches the file");
    }

    for (const char* legacy : {"pause_screen.rml", "block_states.json", "perlin_icon.png", "light.json"}) {
        std::string flat;
        Check(!perlin::resources::Read(legacy, flat), std::string("flat legacy name is gone: ") + legacy);
    }

    for (const char* name :
         {perlin::resources::path::kBlockStates, perlin::resources::path::kBlockVersions,
          perlin::resources::path::kBlockDestroy, perlin::resources::path::kBlockModels,
          perlin::resources::path::kBlockPhysics, perlin::resources::path::kBlockSelection,
          perlin::resources::path::kBlockDefaults, perlin::resources::path::kBlockShapes,
          perlin::resources::path::kExtraItems, perlin::resources::path::kItemDisplay,
          perlin::resources::path::kItemNames, perlin::resources::path::kLight, perlin::resources::path::kAppIcon,
          perlin::resources::path::kBadge, perlin::resources::path::kControlPage,
          perlin::resources::path::kUiScreenDefaults, perlin::resources::path::kJavaHudOverrides}) {
        Check(!perlin::resources::ReadRequiredText(name).empty(), std::string("named resource is readable: ") + name);
        Check(fs::is_regular_file(root / name), std::string("named resource is on disk: ") + name);
    }

    fs::create_directories(work / "data" / "assets" / "ui" / "screens");
    std::ofstream(work / "data" / "assets" / "ui" / "screens" / "pause_screen.rml") << "user override";
    Check(perlin::resources::ReadText("ui/screens/pause_screen.rml") == "user override",
          "DataDir/assets overrides the embedded resource at the same relative path");

    fs::create_directories(work / "other" / "ui" / "screens");
    std::ofstream(work / "other" / "ui" / "screens" / "pause_screen.rml") << "env override";
    setenv("PERLIN_RESOURCES", (work / "other").string().c_str(), 1);
    Check(perlin::resources::ReadText("ui/screens/pause_screen.rml") == "env override",
          "PERLIN_RESOURCES wins over the data dir and the embedded copy");
    Check(perlin::resources::ReadText("ui/screens/title_screen.rml") != "",
          "files missing from PERLIN_RESOURCES fall through to the next source");

    std::string none;
    Check(!perlin::resources::Read(std::string("ui/screens/") + "does_not_exist.rml", none), "unknown resource is reported missing");

    fs::remove_all(work);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
