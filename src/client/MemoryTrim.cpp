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
        // minimized: hand the working set back to the os so other instances can use the ram; pages fault
        // back in on restore
        if (aggressive)
            SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
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
