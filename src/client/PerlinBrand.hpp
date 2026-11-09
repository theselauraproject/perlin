#pragma once
#include <string>
#include <string_view>

// how Perlin clients spot each other: the skin id every Perlin client sends starts with this tag; servers
// pass skin ids through untouched in PlayerList / PlayerSkin, other clients ignore them
namespace perlin::brand {
    inline constexpr std::string_view kSkinIdTag = "Perlin_";

    inline bool IsPerlinSkinId(std::string_view id) {
        return id.starts_with(kSkinIdTag);
    }

    inline std::string MarkSkinId(const std::string& id) {
        return IsPerlinSkinId(id) ? id : std::string(kSkinIdTag) + id;
    }
}
