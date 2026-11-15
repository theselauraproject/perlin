#include "util/UrlHandlerRegistration.hpp"
#include "util/FileIo.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#ifdef PERLIN_HAS_XDG_URL_HANDLER
#include <unistd.h>
#endif

namespace {
    int failures = 0;
    void Check(bool ok, const char* what, int line) {
        if (!ok) {
            std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
            failures++;
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    std::string ReferenceEntry(const std::string& exe, const std::string& categories,
                               const std::string& mime) {
        std::string quoted = "\"";
        for (char c : exe) {
            if (c == '"' || c == '`' || c == '$' || c == '\\')
                quoted += '\\';
            quoted += c;
        }
        quoted += '"';
        return "[Desktop Entry]\nName=Perlin\nExec=" + quoted +
               " %u\nType=Application\nNoDisplay=true\nCategories=" + categories + "\nMimeType=" + mime +
               ";\n";
    }

    std::string Slurp(const std::filesystem::path& p) {
        const std::string contents = perlin::file::Read(p);
        return contents;
    }
}

int main() {
    std::mt19937 rng(11);
    const char alphabet[] = "ab /\"`$\\%u.-";
    for (int i = 0; i < 5000; i++) {
        std::string exe;
        for (size_t n = rng() % 20; n > 0; n--)
            exe.push_back(alphabet[rng() % (sizeof(alphabet) - 1)]);
        CHECK(perlin::platform::BuildDesktopEntry(exe, "Game;", "x-scheme-handler/perlin") ==
              ReferenceEntry(exe, "Game;", "x-scheme-handler/perlin"));
    }
    CHECK(perlin::platform::QuoteDesktopExec("/a b/$x\"y") == "\"/a b/\\$x\\\"y\"");

#ifdef PERLIN_HAS_XDG_URL_HANDLER
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("perlin_url_handler_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "bin");
    const fs::path log = root / "xdg-mime.log";
    {
        std::ofstream script(root / "bin" / "xdg-mime");
        script << "#!/bin/sh\necho \"$@\" >> '" << log.string() << "'\n";
    }
    fs::permissions(root / "bin" / "xdg-mime", fs::perms::owner_all);
    setenv("PATH", (root / "bin").c_str(), 1);
    unsetenv("APPIMAGE");
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);

    CHECK(perlin::platform::RegisterXdgHandler("perlin-url-perlin.desktop", "Game;",
                                               "x-scheme-handler/perlin", "/opt/Perlin App/perlin"));
    const fs::path desktop = root / "data" / "applications" / "perlin-url-perlin.desktop";
    CHECK(Slurp(desktop) == ReferenceEntry("/opt/Perlin App/perlin", "Game;", "x-scheme-handler/perlin"));
    CHECK(Slurp(log) == "default perlin-url-perlin.desktop x-scheme-handler/perlin\n");

    CHECK(perlin::platform::RegisterXdgHandler("perlin-url-perlin.desktop", "Game;",
                                               "x-scheme-handler/perlin", "/opt/Perlin App/perlin"));
    CHECK(Slurp(log) == "default perlin-url-perlin.desktop x-scheme-handler/perlin\n");

    CHECK(perlin::platform::RegisterXdgHandler("perlin-url-perlin.desktop", "Game;",
                                               "x-scheme-handler/perlin", "/elsewhere/perlin"));
    CHECK(Slurp(desktop) == ReferenceEntry("/elsewhere/perlin", "Game;", "x-scheme-handler/perlin"));
    CHECK(Slurp(log).size() ==
          2 * std::string("default perlin-url-perlin.desktop x-scheme-handler/perlin\n").size());

    setenv("APPIMAGE", "/tmp/Perlin.AppImage", 1);
    CHECK(perlin::platform::RegisterXdgHandler("discord-1.desktop", "Discord;Games;",
                                               "x-scheme-handler/discord-1", "/ignored"));
    CHECK(Slurp(root / "data" / "applications" / "discord-1.desktop") ==
          ReferenceEntry("/tmp/Perlin.AppImage", "Discord;Games;", "x-scheme-handler/discord-1"));
    unsetenv("APPIMAGE");

    unsetenv("XDG_DATA_HOME");
    setenv("HOME", (root / "home").c_str(), 1);
    CHECK(perlin::platform::RegisterXdgHandler("a.desktop", "Game;", "x-scheme-handler/a", "/x"));
    CHECK(fs::exists(root / "home" / ".local/share/applications" / "a.desktop"));

    unsetenv("HOME");
    CHECK(!perlin::platform::RegisterXdgHandler("b.desktop", "Game;", "x-scheme-handler/b", "/x"));

    fs::remove_all(root);
#endif

    if (failures) {
        std::fprintf(stderr, "url_handler_selftest FAILED (%d)\n", failures);
        return 1;
    }
    std::puts("url_handler_selftest passed");
    return 0;
}
