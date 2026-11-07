#include "client/input/Keys.hpp"

#include <cctype>
#include <cstdlib>

namespace perlin {
    namespace {
        struct Named {
            int code;
            const char* name;
        };

        constexpr Named kNamed[] = {
            {Key::Backspace, "backspace"},
            {Key::Tab, "tab"},
            {Key::Enter, "enter"},
            {Key::Shift, "shift"},
            {Key::Control, "control"},
            {Key::Alt, "alt"},
            {Key::Escape, "escape"},
            {Key::Space, "space"},
            {Key::PageUp, "page.up"},
            {Key::PageDown, "page.down"},
            {Key::End, "end"},
            {Key::Home, "home"},
            {Key::Delete, "delete"},
            {Key::Slash, "slash"},
            {Key::Left, "left"},
            {Key::Up, "up"},
            {Key::Right, "right"},
            {Key::Down, "down"},
        };

        constexpr Named kMouse[] = {
            {Mouse::Left, "left"},
            {Mouse::Right, "right"},
            {Mouse::Middle, "middle"},
        };

        constexpr const char* kKeyPrefix = "key.keyboard.";
        constexpr const char* kMousePrefix = "key.mouse.";
    }

    std::string KeyName(int key) {
        std::string out = kKeyPrefix;
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
            out += static_cast<char>(std::tolower(key));
            return out;
        }
        if (key >= Key::F1 && key <= Key::F12) {
            out += 'f';
            out += std::to_string(key - Key::F1 + 1);
            return out;
        }
        for (const auto& n : kNamed)
            if (n.code == key)
                return out + n.name;
        return {};
    }

    int KeyFromName(const std::string& name) {
        std::string prefix = kKeyPrefix;
        if (name.compare(0, prefix.size(), prefix) != 0)
            return 0;
        std::string tail = name.substr(prefix.size());
        if (tail.size() == 1 && std::isalnum(static_cast<unsigned char>(tail[0])))
            return std::toupper(static_cast<unsigned char>(tail[0]));
        if (tail.size() >= 2 && tail[0] == 'f' && std::isdigit(static_cast<unsigned char>(tail[1]))) {
            int n = std::atoi(tail.c_str() + 1);
            if (n >= 1 && n <= 12)
                return Key::F1 + n - 1;
        }
        for (const auto& n : kNamed)
            if (tail == n.name)
                return n.code;
        return 0;
    }

    std::string MouseName(int button) {
        for (const auto& n : kMouse)
            if (n.code == button)
                return std::string(kMousePrefix) + n.name;
        return {};
    }

    int MouseFromName(const std::string& name) {
        std::string prefix = kMousePrefix;
        if (name.compare(0, prefix.size(), prefix) != 0)
            return -1;
        std::string tail = name.substr(prefix.size());
        for (const auto& n : kMouse)
            if (tail == n.name)
                return n.code;
        return -1;
    }
}
