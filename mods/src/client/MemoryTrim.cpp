#include "client/MemoryTrim.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <malloc.h>
#include <windows.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

namespace perlin {
    void TrimMemory(bool aggressive) {
#if defined(_WIN32)
        _heapmin();
        (void)aggressive;
#elif defined(__GLIBC__)
        (void)aggressive;
        malloc_trim(0);
#else
        (void)aggressive;
#endif
    }

    bool ProcessInBackground() {
#if defined(_WIN32)
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
            return true;
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        return pid != GetCurrentProcessId();
#else
        return false;
#endif
    }
}
