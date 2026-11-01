#include "util/Paths.hpp"
#include "util/Platform.hpp"
#include <cstdlib>
#include <filesystem>

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#elif defined(__FreeBSD__)
#include <climits>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace perlin::paths {
    std::string ExeDir() {
#if defined(_WIN32)
        char buf[MAX_PATH] = {};
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0)
            return {};
        return fs::path(std::string(buf, n)).parent_path().generic_string();
#elif defined(__APPLE__)
        char buf[PATH_MAX] = {};
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) != 0)
            return {};
        std::error_code ec;
        fs::path p = fs::canonical(buf, ec);
        return (ec ? fs::path(buf) : p).parent_path().generic_string();
#elif defined(__FreeBSD__)
        // no /proc by default: the kernel names the running image
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
        char buf[PATH_MAX] = {};
        size_t size = sizeof(buf);
        if (sysctl(mib, 4, buf, &size, nullptr, 0) != 0 || size == 0)
            return {};
        return fs::path(buf).parent_path().generic_string();
#else
        char buf[PATH_MAX] = {};
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
            return {};
        return fs::path(std::string(buf, (size_t)n)).parent_path().generic_string();
#endif
    }

    std::string DataDir() {
        if (const char* override = std::getenv("PERLIN_DATA_DIR"); override && *override)
            return fs::path(override).generic_string();
#if defined(__EMSCRIPTEN__)
        return "/perlin";
#elif defined(__ANDROID__)
        if (const char* path = SDL_GetAndroidExternalStoragePath(); path && *path)
            return (fs::path(path) / "Perlin").generic_string();
        if (const char* path = SDL_GetAndroidInternalStoragePath(); path && *path)
            return (fs::path(path) / "Perlin").generic_string();
        return "/data/local/tmp/Perlin";
#elif defined(PERLIN_IOS)
        const char* home = std::getenv("HOME");
        return (fs::path(home ? home : ".") / "Documents").generic_string();
#elif defined(_WIN32)
        const char* base = std::getenv("APPDATA");
        return (fs::path(base ? base : ".") / "Perlin").generic_string();
#elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        return (fs::path(home ? home : ".") / "Library" / "Application Support" / "Perlin").generic_string();
#else
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
            return (fs::path(xdg) / "Perlin").generic_string();
        const char* home = std::getenv("HOME");
        return (fs::path(home ? home : ".") / ".local" / "share" / "Perlin").generic_string();
#endif
    }

    void EnsureDir(const std::string& dir) {
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
}
