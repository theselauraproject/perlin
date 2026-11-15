#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_OS_MACCATALYST
#define PERLIN_IOS 1
#endif
#if defined(__ANDROID__) || defined(PERLIN_IOS)
#define PERLIN_MOBILE 1
#endif

namespace perlin::platform {
    enum class Os { Windows, MacOs, Linux, FreeBsd, Android, Ios, Web };

    inline constexpr Os kOs =
#if defined(__EMSCRIPTEN__)
        Os::Web;
#elif defined(__ANDROID__)
        Os::Android;
#elif defined(PERLIN_IOS)
        Os::Ios;
#elif defined(_WIN32)
        Os::Windows;
#elif defined(__APPLE__)
        Os::MacOs;
#elif defined(__FreeBSD__)
        Os::FreeBsd;
#else
        Os::Linux;
#endif

    inline constexpr bool kMobile = kOs == Os::Android || kOs == Os::Ios;
    inline constexpr bool kDesktop = !kMobile && kOs != Os::Web;

    constexpr const char* Name(Os os) {
        switch (os) {
        case Os::Web:
            return "web";
        case Os::Android:
            return "android";
        case Os::Ios:
            return "ios";
        case Os::Windows:
            return "windows";
        case Os::MacOs:
            return "macos";
        case Os::FreeBsd:
            return "freebsd";
        case Os::Linux:
            return "linux";
        }
        return "unknown";
    }

    inline constexpr const char* kName = Name(kOs);
}
