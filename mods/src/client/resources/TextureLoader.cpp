#include "client/resources/TextureLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "util/Json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace perlin {
    namespace {
        bool LoadImageFile(const std::string& path, Texture& out) {
            int w = 0, h = 0, channels = 0;
            stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
            if (!data)
                return false;
            out.width = w;
            out.height = h;
            out.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
            stbi_image_free(data);
            return true;
        }
    }

    bool TextureSetColor(const std::string& jsonText, std::string& image, unsigned char rgba[4]) {
        image.clear();
        const auto doc = json::Document::Parse(jsonText, true);
        if (!doc)
            return false;
        const json::View set = doc->root()["minecraft:texture_set"];
        const json::View color = set["color"];
        if (color.isString()) {
            image = color.str();
            return !image.empty();
        }
        if (color.isArray() && color.size() >= 3) {
            for (size_t i = 0; i < 4; i++)
                rgba[i] = static_cast<unsigned char>(
                    std::clamp(color[i].valid() ? color[i].integer(255) : 255, 0, 255));
            return true;
        }
        return false;
    }

    void MakeFlatTexture(const unsigned char rgba[4], int size, Texture& out) {
        out.width = out.height = size;
        out.pixels.resize(static_cast<size_t>(size) * size * 4);
        for (size_t i = 0; i < out.pixels.size(); i += 4)
            std::copy(rgba, rgba + 4, out.pixels.begin() + static_cast<std::ptrdiff_t>(i));
    }

    bool LoadTexture(const std::string& path, Texture& out) {
        if (LoadImageFile(path, out))
            return true;
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos)
            return false;
        std::string ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext != ".png" && ext != ".tga")
            return false;
        std::ifstream in(path.substr(0, dot) + ".texture_set.json", std::ios::binary);
        if (!in)
            return false;
        std::stringstream ss;
        ss << in.rdbuf();
        std::string image;
        unsigned char rgba[4] = {255, 255, 255, 255};
        if (!TextureSetColor(ss.str(), image, rgba))
            return false;
        if (image.empty()) {
            MakeFlatTexture(rgba, 16, out);
            return true;
        }
        const size_t slash = path.find_last_of("/\\");
        const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
        return LoadImageFile(dir + image + ".png", out) || LoadImageFile(dir + image + ".tga", out);
    }

    bool LoadTextureMemory(const unsigned char* data, size_t size, Texture& out) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
        if (!pixels)
            return false;
        out.width = w;
        out.height = h;
        out.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
        stbi_image_free(pixels);
        return true;
    }

    bool UpgradeLegacySkinTexture(Texture& skin) {
        if (skin.width < 64 || skin.width != skin.height * 2 ||
            skin.pixels.size() < static_cast<size_t>(skin.width) * static_cast<size_t>(skin.height) * 4)
            return false;
        const int w = skin.width, s = w / 64;
        std::vector<unsigned char> square(static_cast<size_t>(w) * static_cast<size_t>(w) * 4, 0);
        std::copy(skin.pixels.begin(), skin.pixels.begin() + static_cast<std::ptrdiff_t>(w) * skin.height * 4,
                  square.begin());
        // copy a face (64-unit skin coords) right limb -> left limb, mirrored: legacy format only stores the right side
        const auto face = [&](int sx, int sy, int dx, int dy, int fw, int fh) {
            for (int y = 0; y < fh * s; y++)
                for (int x = 0; x < fw * s; x++) {
                    const size_t from = (static_cast<size_t>(sy * s + y) * static_cast<size_t>(w) +
                                         static_cast<size_t>(sx * s + fw * s - 1 - x)) *
                                        4;
                    const size_t to = (static_cast<size_t>(dy * s + y) * static_cast<size_t>(w) +
                                       static_cast<size_t>(dx * s + x)) *
                                      4;
                    std::copy(square.begin() + static_cast<std::ptrdiff_t>(from),
                              square.begin() + static_cast<std::ptrdiff_t>(from) + 4,
                              square.begin() + static_cast<std::ptrdiff_t>(to));
                }
        };
        // leg: right box (0,16) -> left box (16,48); faces top, bottom, then four sides; outer/inner swap when mirrored
        face(4, 16, 20, 48, 4, 4);
        face(8, 16, 24, 48, 4, 4);
        face(0, 20, 24, 52, 4, 12);
        face(4, 20, 20, 52, 4, 12);
        face(8, 20, 16, 52, 4, 12);
        face(12, 20, 28, 52, 4, 12);
        // arm: right box (40,16) -> left box (32,48)
        face(44, 16, 36, 48, 4, 4);
        face(48, 16, 40, 48, 4, 4);
        face(40, 20, 40, 52, 4, 12);
        face(44, 20, 36, 52, 4, 12);
        face(48, 20, 32, 52, 4, 12);
        face(52, 20, 44, 52, 4, 12);
        skin.pixels = std::move(square);
        skin.height = w;
        return true;
    }
}
