#pragma once
#include "client/systems/javashaders/GlslTokens.hpp"

#include <vector>

namespace perlin::jshader {
    void RenameShadowedSamplers(std::vector<glsl::Tok>& tokens);
}
