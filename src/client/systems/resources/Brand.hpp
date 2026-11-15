#pragma once
#include <string>
#include <string_view>

namespace perlin::brand {
    inline constexpr std::string_view kSkinIdTag = "Perlin_";

    inline bool IsPerlinSkinId(std::string_view id) {
        return id.starts_with(kSkinIdTag);
    }

    inline std::string MarkSkinId(const std::string& id) {
        return IsPerlinSkinId(id) ? id : std::string(kSkinIdTag) + id;
    }
}
