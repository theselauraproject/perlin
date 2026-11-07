#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace perlin {

    struct ModelQuad {
        uint8_t dir;
        uint8_t shadeDir;
        int8_t cull;
        int8_t tint;
        uint8_t shade;
        uint8_t light;
        uint16_t tex;
        float pos[4][3];
        float uv[4][2];
        int tile = 0;
    };

    struct ModelEntry {
        uint8_t offset = 0;
        bool cube = false;
        struct Choice {
            uint32_t variant;
            uint8_t weight;
        };
        std::vector<std::vector<Choice>> groups;
        bool direct = false;
        std::vector<ModelQuad> quads;
    };

    class BlockModels {
    public:
        bool Load(const std::string& path);
        bool LoadFromMemory(const std::string& bytes);
        bool Loaded() const { return loaded; }
        const std::vector<std::string>& Textures() const { return textures; }
        const ModelEntry* Find(int32_t hash) const {
            auto it = byHash.find(hash);
            return it == byHash.end() ? nullptr : &entries[it->second];
        }
        const std::vector<ModelQuad>& Variant(uint32_t id) const { return variants[id]; }
        size_t BlockCount() const { return byHash.size(); }

        std::vector<int> tiles;

    private:
        bool loaded = false;
        std::vector<std::string> textures;
        std::vector<std::vector<ModelQuad>> variants;
        std::vector<ModelEntry> entries;
        std::unordered_map<int32_t, uint32_t> byHash;
    };

}
