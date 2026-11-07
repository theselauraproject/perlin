#pragma once
#include "client/resources/TextureLoader.hpp"

#include <string>

namespace perlin::ui {
    // the active texture pack's version of a built-in ui sheet, resampled to the sheet's own size so every
    // pixel coordinate the screens use still lands where it did. false when the pack does not restyle it
    bool PackSpriteFor(const std::string& resource, int baseWidth, int baseHeight, Texture& out);

    // same lookup at the pack's own resolution, for sprites drawn at an explicit size
    bool PackSpriteNative(const std::string& resource, Texture& out);

    std::string SpriteRml(const std::string& resource, int srcX, int srcY, int width, int height, int dstX,
                          int dstY);
}
