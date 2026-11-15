#include "util/UrlHandlerRegistration.hpp"

#include "util/FileIo.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef PERLIN_HAS_XDG_URL_HANDLER
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace perlin::platform {

    std::string QuoteDesktopExec(std::string_view executable) {
        std::string quoted = "\"";
        for (char c : executable) {
            if (c == '"' || c == '`' || c == '$' || c == '\\')
                quoted += '\\';
            quoted += c;
        }
        quoted += '"';
        return quoted;
    }

    std::string BuildDesktopEntry(std::string_view executable, std::string_view categories,
                                  std::string_view mimeType) {
        std::string text = "[Desktop Entry]\nName=Perlin\nExec=";
        text += QuoteDesktopExec(executable);
        text += " %u\nType=Application\nNoDisplay=true\nCategories=";
        text += categories;
        text += "\nMimeType=";
        text += mimeType;
        text += ";\n";
        return text;
    }

#if defined(_WIN32)

    namespace {
        class RegistryKey {
        public:
            RegistryKey() = default;
            ~RegistryKey() {
                if (key)
                    RegCloseKey(key);
            }
            RegistryKey(const RegistryKey&) = delete;
            RegistryKey& operator=(const RegistryKey&) = delete;

            bool Create(HKEY parent, const char* path) {
                return RegCreateKeyExA(parent, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) ==
                       ERROR_SUCCESS;
            }
            void SetDefault(const std::string& value) const { Set(nullptr, value); }
            void Set(const char* name, const std::string& value) const {
                RegSetValueExA(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                               static_cast<DWORD>(value.size() + 1));
            }
            HKEY Handle() const { return key; }

        private:
            HKEY key = nullptr;
        };
    }

    bool RegisterWindowsProtocol(const std::string& classKey, const std::string& description,
                                 const std::string& executable) {
        RegistryKey root;
        if (!root.Create(HKEY_CURRENT_USER, classKey.c_str()))
            return false;
        std::string exe = executable;
        std::replace(exe.begin(), exe.end(), '/', '\\');
        root.SetDefault(description);
        root.Set("URL Protocol", "");
        if (RegistryKey icon; icon.Create(root.Handle(), "DefaultIcon"))
            icon.SetDefault(exe);
        if (RegistryKey command; command.Create(root.Handle(), "shell\\open\\command"))
            command.SetDefault("\"" + exe + "\" \"%1\"");
        return true;
    }

#elif defined(PERLIN_HAS_XDG_URL_HANDLER)

    namespace {
        std::filesystem::path ApplicationsDirectory() {
            if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
                return std::filesystem::path(data) / "applications";
            if (const char* home = std::getenv("HOME"); home && *home)
                return std::filesystem::path(home) / ".local/share/applications";
            return {};
        }

        bool SetDefaultHandler(const std::string& desktopFileName, const std::string& mimeType) {
            const pid_t child = fork();
            if (child == 0) {
                const int devnull = open("/dev/null", O_WRONLY);
                if (devnull >= 0) {
                    dup2(devnull, 1);
                    dup2(devnull, 2);
                }
                execlp("xdg-mime", "xdg-mime", "default", desktopFileName.c_str(), mimeType.c_str(),
                       static_cast<char*>(nullptr));
                _exit(127);
            }
            if (child > 0)
                waitpid(child, nullptr, 0);
            return child > 0;
        }
    }

    bool RegisterXdgHandler(const std::string& desktopFileName, std::string_view categories,
                            const std::string& mimeType, const std::string& executable) {
        std::string exe = executable;
        if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage)
            exe = appimage;
        const std::filesystem::path dir = ApplicationsDirectory();
        if (dir.empty())
            return false;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::string desktop = BuildDesktopEntry(exe, categories, mimeType);
        const std::filesystem::path path = dir / desktopFileName;
        std::string existing;
        if (std::ifstream in(path); in)
            file::ReadRest(in, existing);
        if (existing == desktop)
            return true;
        std::ofstream(path, std::ios::trunc) << desktop;
        return SetDefaultHandler(desktopFileName, mimeType);
    }

#endif

}
