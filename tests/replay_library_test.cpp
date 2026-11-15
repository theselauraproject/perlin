#include "client/screen/screens/ReplayLibrary.hpp"
#include "client/systems/replay/ReplayFormat.hpp"
#include "client/ui/ReplayControls.hpp"
#include "client/ui/TextEdit.hpp"
#include "util/Paths.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace perlin;

namespace perlin::ui {
    std::string FormatReplayTime(double ms) {
        return std::to_string(static_cast<long long>(ms / 1000.0)) + "s";
    }

    TextEdit& TextEdit::Focused() {
        static TextEdit none;
        return none;
    }

    bool TextEdit::Apply(const InputManager::Typed&, std::string&, const TextLimits&) {
        return false;
    }
}

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            failures++;
        }
    }
}

int main() {
    const fs::path root = fs::temp_directory_path() / "perlin_replay_library_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
#ifdef _WIN32
    _putenv_s("PERLIN_DATA_DIR", root.string().c_str());
#else
    setenv("PERLIN_DATA_DIR", root.string().c_str(), 1);
#endif

    ui::TitleModel title;
    int invalidations = 0;
    std::vector<std::string> announced, opened, played;
    screen::ReplayLibrary library(screen::ReplayLibrary::Deps{
        title,
        [&] { invalidations++; },
        [&](const std::string& url) { opened.push_back(url); },
        [&](const std::string& file) { played.push_back(file); },
        [&](const std::string& text) { announced.push_back(text); }});

    library.Refresh();
    Check(title.replays.empty(), "empty directory lists no replays");
    Check(fs::exists(replay::ReplayDirectory()), "refresh creates the replay directory");

    const fs::path junk = root / "not_a_replay.bin";
    {
        std::ofstream out(junk, std::ios::binary);
        out << "this is not a zip archive";
    }
    Check(!library.Import(junk.string()), "importing a non-replay is rejected");
    Check(title.replayMessage.rfind("Not a valid replay", 0) == 0, "rejection is reported on the title model");
    Check(announced.size() == 1, "rejection is announced once");

    const fs::path bogus = root / "missing.perlinreplay";
    Check(!library.Import(bogus.string()), "importing a missing file is rejected");
    Check(announced.size() == 2, "second rejection is announced");

    int copied = 0;
    for (const auto& e : fs::directory_iterator(replay::ReplayDirectory(), ec))
        if (e.path().extension() == replay::kExtension)
            copied++;
    Check(copied == 0, "rejected imports leave nothing in the library");

    Check(!library.Click("no_such_control"), "unknown hover is not consumed");
    Check(played.empty() && opened.empty(), "no playback or url open without a selection");

    fs::remove_all(root, ec);
    if (failures == 0)
        std::puts("replay_library_test: ok");
    return failures == 0 ? 0 : 1;
}
