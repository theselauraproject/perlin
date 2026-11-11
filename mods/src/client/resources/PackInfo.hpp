#pragma once
#include "client/resources/TextureLoader.hpp"

#include <string>

namespace perlin {
    struct PackMeta {
        std::string name;
        std::string description;
        std::string version;
        bool hasIcon = false;
        Texture icon;
    };

    PackMeta ReadPackMeta(const std::string& path);
}
