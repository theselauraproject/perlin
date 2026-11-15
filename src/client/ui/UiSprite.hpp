#pragma once
#include "client/systems/resources/TextureLoader.hpp"
#include "client/ui/Scene.hpp"

#include <string>

namespace perlin::ui {

    bool PackSpriteFor(const std::string& resource, int baseWidth, int baseHeight, Texture& out);

    bool PackSpriteNative(const std::string& resource, Texture& out);

    void AddSprite(SceneBuilder& builder, const std::string& resource, int srcX, int srcY, int width, int height,
                   int dstX, int dstY);
}
