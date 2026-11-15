#pragma once
#include "util/Strings.hpp"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace perlin {
    inline bool ParseServerAddress(const std::string& raw, std::string& host, uint16_t& port) {
        const std::string_view text = str::TrimView(raw);
        host = text;
        port = 19132;
        std::string_view portText;
        if (text.starts_with('[')) {
            const size_t close = text.find(']');
            if (close == std::string_view::npos)
                return false;
            host = text.substr(1, close - 1);
            if (close + 1 == text.size())
                return !host.empty();
            if (text[close + 1] != ':')
                return false;
            portText = text.substr(close + 2);
        } else {
            const size_t colon = text.rfind(':');

            if (colon == std::string_view::npos || text.find(':') != colon)
                return !host.empty();
            host = text.substr(0, colon);
            portText = text.substr(colon + 1);
        }
        portText = str::TrimView(portText);
        int value = 0;
        const auto [end, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), value);
        if (ec != std::errc() || end == portText.data() || value <= 0 || value > 65535)
            return false;
        port = static_cast<uint16_t>(value);
        return !host.empty();
    }
}
