#pragma once
#include "client/resources/TextureLoader.hpp"

#include <string>

namespace perlin::ui {

    bool PackSpriteFor(const std::string& resource, int baseWidth, int baseHeight, Texture& out);

    bool PackSpriteNative(const std::string& resource, Texture& out);

    std::string SpriteRml(const std::string& resource, int srcX, int srcY, int width, int height, int dstX,
                          int dstY);
}
