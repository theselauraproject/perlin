#include "util/GraphicsDebugger.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace perlin::gfxdebug {
    const char* Attached() {
#if defined(_WIN32)
        if (GetModuleHandleW(L"renderdoc.dll"))
            return "RenderDoc";
        if (GetModuleHandleW(L"WinPixGpuCapturer.dll"))
            return "PIX";
        if (GetModuleHandleW(L"Nvda.Graphics.Interception.dll"))
            return "Nsight Graphics";
#elif defined(__linux__) && !defined(__ANDROID__)
        if (void* handle = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD)) {
            dlclose(handle);
            return "RenderDoc";
        }
#endif
        if (std::getenv("RENDERDOC_CAPFILE") || std::getenv("RENDERDOC_CAPOPTS") ||
            std::getenv("ENABLE_VULKAN_RENDERDOC_CAPTURE"))
            return "RenderDoc";
        return nullptr;
    }

    bool WantsRenderDoc(int argc, char** argv) {
        if (const char* env = std::getenv("PERLIN_RENDERDOC"); env && *env && *env != '0')
            return true;
        for (int i = 1; i < argc; i++)
            if (std::string(argv[i]) == "--renderdoc")
                return true;
        return false;
    }

    bool LoadRenderDoc(const std::string& captureDir) {
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        using GetApiFn = int (*)(int, void**);
        GetApiFn getApi = nullptr;
#endif
#if defined(_WIN32)
        HMODULE module = GetModuleHandleW(L"renderdoc.dll");
        if (const char* path = std::getenv("PERLIN_RENDERDOC_DLL"); !module && path && *path)
            module = LoadLibraryA(path);
        if (!module)
            module = LoadLibraryW(L"renderdoc.dll");
        if (!module) {
            wchar_t base[MAX_PATH] = {};
            const DWORD n = GetEnvironmentVariableW(L"ProgramFiles", base, MAX_PATH);
            if (n > 0 && n < MAX_PATH)
                module = LoadLibraryW((std::wstring(base) + L"\\RenderDoc\\renderdoc.dll").c_str());
        }
        if (module)
            getApi = reinterpret_cast<GetApiFn>(reinterpret_cast<void*>(GetProcAddress(module, "RENDERDOC_GetAPI")));
#elif defined(__linux__) && !defined(__ANDROID__)
        void* module = nullptr;
        if (const char* path = std::getenv("PERLIN_RENDERDOC_DLL"); path && *path)
            module = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        for (const char* name : {"librenderdoc.so", "/usr/lib/librenderdoc.so", "/usr/lib64/librenderdoc.so",
                                 "/usr/local/lib/librenderdoc.so", "/opt/renderdoc/lib/librenderdoc.so"})
            if (!module)
                module = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
        if (module)
            getApi = reinterpret_cast<GetApiFn>(dlsym(module, "RENDERDOC_GetAPI"));
#else
        (void)captureDir;
        return false;
#endif
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        if (!module) {
            std::printf("[renderdoc] renderdoc could not be loaded; install it or set PERLIN_RENDERDOC_DLL\n");
            return false;
        }
        void** api = nullptr;
        if (getApi && getApi(10102, reinterpret_cast<void**>(&api)) == 1 && api) {
            using SetTemplateFn = void (*)(const char*);
            reinterpret_cast<SetTemplateFn>(api[11])((captureDir + "/perlin").c_str());
            std::printf("[renderdoc] loaded; press F12 or Print Screen to capture, captures go to %s\n",
                        captureDir.c_str());
        } else {
            std::printf("[renderdoc] loaded; press F12 or Print Screen to capture\n");
        }
        return true;
#endif
    }
}
