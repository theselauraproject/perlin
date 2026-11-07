#include "util/Relaunch.hpp"
#include "util/Paths.hpp"

#include <filesystem>

#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif

namespace perlin {
    namespace {
        std::vector<std::string>& Args() {
            static std::vector<std::string> args;
            return args;
        }

        int Count() {
            const char* v = std::getenv("PERLIN_RELAUNCH");
            return v ? std::atoi(v) : 0;
        }
    }

    void RememberCommandLine(int argc, char** argv) {
        Args().assign(argv, argv + argc);
    }

    bool RelaunchSelf(bool requested) {
#ifdef __EMSCRIPTEN__
        return false;
#else
        const int count = requested ? 0 : Count();
        if (count >= 3 || Args().empty() || (!requested && std::getenv("PERLIN_NO_RELAUNCH")))
            return false;
        const std::string next = requested ? std::string("0") : std::to_string(count + 1);
#ifdef _WIN32
        SetEnvironmentVariableA("PERLIN_RELAUNCH", next.c_str());
        wchar_t path[MAX_PATH * 4] = {};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH * 4))
            return false;
        std::wstring line = GetCommandLineW();
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(path, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            return false;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
#else
        setenv("PERLIN_RELAUNCH", next.c_str(), 1);
        std::vector<char*> argv;
        for (std::string& a : Args())
            argv.push_back(a.data());
        argv.push_back(nullptr);
#if defined(__linux__)
        execv("/proc/self/exe", argv.data());
#else
        // the program's own path; argv[0] may be relative to a directory that is no longer current
        const std::string self = paths::ExeDir() + "/" + std::filesystem::path(Args()[0]).filename().string();
        execv(self.c_str(), argv.data());
#endif
        return false;
#endif
#endif
    }
}
