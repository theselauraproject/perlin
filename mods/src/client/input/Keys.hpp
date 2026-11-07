#pragma once
#include <string>

namespace perlin {
    namespace Key {
        constexpr int Backspace = 0x08;
        constexpr int Tab = 0x09;
        constexpr int Enter = 0x0D;
        constexpr int Shift = 0x10;
        constexpr int Control = 0x11;
        constexpr int Alt = 0x12;
        constexpr int Escape = 0x1B;
        constexpr int Space = 0x20;
        constexpr int PageUp = 0x21;
        constexpr int PageDown = 0x22;
        constexpr int End = 0x23;
        constexpr int Home = 0x24;
        constexpr int Left = 0x25;
        constexpr int Up = 0x26;
        constexpr int Right = 0x27;
        constexpr int Down = 0x28;
        constexpr int Delete = 0x2E;
        constexpr int Slash = 0xBF;
        constexpr int F1 = 0x70;
        constexpr int F12 = 0x7B;
        constexpr int F3 = 0x72;
        constexpr int F4 = 0x73;
    }

    namespace Mouse {
        constexpr int Left = 0;
        constexpr int Right = 1;
        constexpr int Middle = 2;
    }

    std::string KeyName(int key);
    int KeyFromName(const std::string& name);
    std::string MouseName(int button);
    int MouseFromName(const std::string& name);
}
