#pragma once
#include <string>
#include <string_view>

namespace perlin::xbox::rp {
    inline constexpr std::string_view kXboxLive = "http://xboxlive.com";
    inline constexpr std::string_view kPlayFab = "http://playfab.xboxlive.com/";
    inline constexpr std::string_view kRejectedPlayFab = "rp://playfabapi.com/";

    std::string ForPlayFab(const std::string& advertisedByNsal);
    bool IsRejected(std::string_view relyingParty);
}
