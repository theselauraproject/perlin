#pragma once
#include "client/resources/TextureLoader.hpp"

#include <functional>
#include <string>
#include <vector>

namespace perlin::javapack {
    struct Source {
        std::string path;
        float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
        bool Whole() const { return x0 == 0 && y0 == 0 && x1 == 1 && y1 == 1; }
    };

    std::vector<Source> Sources(const std::string& bedrockPath);

    using SheetLoader = std::function<bool(const std::string& path, Texture& out)>;

    bool Compose(const std::string& bedrockPath, const SheetLoader& load, Texture& out);

    bool Crop(const Texture& sheet, const Source& source, Texture& out);
}
