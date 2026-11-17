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
        constexpr int Pause = 0x13;
        constexpr int CapsLock = 0x14;
        constexpr int PrintScreen = 0x2C;
        constexpr int Insert = 0x2D;
        constexpr int LeftWin = 0x5B;
        constexpr int RightWin = 0x5C;
        constexpr int Menu = 0x5D;
        constexpr int Keypad0 = 0x60;
        constexpr int KeypadMultiply = 0x6A;
        constexpr int KeypadAdd = 0x6B;
        constexpr int KeypadSubtract = 0x6D;
        constexpr int KeypadDecimal = 0x6E;
        constexpr int KeypadDivide = 0x6F;
        constexpr int NumLock = 0x90;
        constexpr int ScrollLock = 0x91;
        constexpr int Semicolon = 0xBA;
        constexpr int Equal = 0xBB;
        constexpr int Comma = 0xBC;
        constexpr int Minus = 0xBD;
        constexpr int Period = 0xBE;
        constexpr int Grave = 0xC0;
        constexpr int LeftBracket = 0xDB;
        constexpr int Backslash = 0xDC;
        constexpr int RightBracket = 0xDD;
        constexpr int Apostrophe = 0xDE;
        constexpr int World2 = 0xE2;
        constexpr int F1 = 0x70;
        constexpr int F12 = 0x7B;
        constexpr int F24 = 0x87;
        constexpr int F3 = 0x72;
        constexpr int F4 = 0x73;
    }

    namespace Mouse {
        constexpr int Left = 0;
        constexpr int Right = 1;
        constexpr int Middle = 2;
        constexpr int Back = 3;
        constexpr int Forward = 4;
    }

    std::string KeyName(int key);
    int KeyFromName(const std::string& name);
    std::string MouseName(int button);
    int MouseFromName(const std::string& name);
}
