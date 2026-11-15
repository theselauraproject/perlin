#pragma once

namespace perlin::platform {
#if defined(__linux__) && !defined(__ANDROID__)
    void RegisterDesktopEntry();
#else
    inline void RegisterDesktopEntry() {}
#endif
}
