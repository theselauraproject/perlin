#pragma once
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace perlin {
    inline bool IsValidUtf8(const std::string& s) {
        for (size_t i = 0; i < s.size();) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            size_t n;
            if (c < 0x80)
                n = 0;
            else if ((c >> 5) == 0x6)
                n = 1;
            else if ((c >> 4) == 0xE)
                n = 2;
            else if ((c >> 3) == 0x1E)
                n = 3;
            else
                return false;
            if (i + n >= s.size() + (n == 0 ? 1 : 0))
                return false;
            for (size_t k = 1; k <= n; k++)
                if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
                    return false;
            i += n + 1;
        }
        return true;
    }

    inline std::string EnsureUtf8(const std::string& s) {
        if (IsValidUtf8(s))
            return s;
#ifdef _WIN32
        const int wide = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (wide <= 0)
            return s;
        std::wstring w(static_cast<size_t>(wide), L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), w.data(), wide);
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, w.data(), wide, nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
            return s;
        std::string out(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), wide, out.data(), bytes, nullptr, nullptr);
        return out;
#else
        std::string out;
        for (const unsigned char c : s) {
            if (c < 0x80) {
                out += static_cast<char>(c);
            } else {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return out;
#endif
    }
}
