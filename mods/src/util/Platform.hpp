#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// PERLIN_IOS: iPhone / iPad (not Mac Catalyst). PERLIN_MOBILE: a touch-first, always-fullscreen device.
#if defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_OS_MACCATALYST
#define PERLIN_IOS 1
#endif
#if defined(__ANDROID__) || defined(PERLIN_IOS)
#define PERLIN_MOBILE 1
#endif

namespace perlin::platform {
#if defined(PERLIN_MOBILE)
    inline constexpr bool kMobile = true;
#else
    inline constexpr bool kMobile = false;
#endif

    inline constexpr const char* kName =
#if defined(__EMSCRIPTEN__)
        "web";
#elif defined(__ANDROID__)
        "android";
#elif defined(PERLIN_IOS)
        "ios";
#elif defined(_WIN32)
        "windows";
#elif defined(__APPLE__)
        "macos";
#elif defined(__FreeBSD__)
        "freebsd";
#else
        "linux";
#endif
}
