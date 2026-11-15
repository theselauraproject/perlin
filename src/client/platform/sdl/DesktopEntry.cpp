#include "client/platform/sdl/DesktopEntry.hpp"

#include "util/FileIo.hpp"
#include "util/Resources.hpp"
#include "util/ResourcePaths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__linux__) && !defined(__ANDROID__)
namespace perlin::platform {
    void RegisterDesktopEntry() {
        if (std::getenv("FLATPAK_ID"))
            return;
        std::error_code ec;
        std::filesystem::path exe;
        if (const char* image = std::getenv("APPIMAGE"); image && *image)
            exe = image;
        else
            exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec || exe.empty())
            return;
        std::filesystem::path data;
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
            data = xdg;
        else if (const char* home = std::getenv("HOME"); home && *home)
            data = std::filesystem::path(home) / ".local" / "share";
        else
            return;
        const auto writeIfChanged = [&](const std::filesystem::path& path, const std::string& content) {
            std::string current;
            if (std::ifstream in(path, std::ios::binary); in)
                perlin::file::ReadRest(in, current);
            if (current == content)
                return;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
        };
        std::string png;
        if (perlin::resources::Read(perlin::resources::path::kAppIcon, png))
            writeIfChanged(data / "icons" / "hicolor" / "256x256" / "apps" / "io.perlin.client.png", png);
        std::string command = "\"";
        for (const char c : exe.string()) {
            if (c == '"' || c == '`' || c == '$' || c == '\\')
                command += "\\\\";
            if (c == '%')
                command += '%';
            command += c;
        }
        command += "\"";
        writeIfChanged(data / "applications" / "io.perlin.client.desktop",
                       "[Desktop Entry]\nType=Application\nName=Perlin\nExec=" + command +
                           "\nIcon=io.perlin.client\nTerminal=false\nCategories=Game;\nStartupWMClass=io."
                           "perlin.client\n");
    }
}
#endif
