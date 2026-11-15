#pragma once
#include <string>
#include <string_view>

namespace perlin::platform {

    std::string QuoteDesktopExec(std::string_view executable);
    std::string BuildDesktopEntry(std::string_view executable, std::string_view categories,
                                  std::string_view mimeType);

#if defined(_WIN32)
    bool RegisterWindowsProtocol(const std::string& classKey, const std::string& description,
                                 const std::string& executable);
#elif defined(__APPLE__)
#elif (defined(__linux__) && !defined(__ANDROID__)) || defined(__FreeBSD__)
#define PERLIN_HAS_XDG_URL_HANDLER 1
    bool RegisterXdgHandler(const std::string& desktopFileName, std::string_view categories,
                            const std::string& mimeType, const std::string& executable);
#endif

}
