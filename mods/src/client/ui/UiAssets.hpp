#pragma once
#include "client/resources/TextureLoader.hpp"
#include "client/ui/HudState.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin {
    class BlockLibrary;
}

namespace perlin::ui {
    struct ItemSprite {
        bool ok = false;
        bool block = false;
        bool handheld = false;
        bool fromBlock = false;
        bool generic = false;
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        int tile = 0;
        std::vector<uint8_t> top, left, right;
        uint32_t tint = 0xffffffff;
    };

    struct TextureSidecar {
        bool present = false;
        bool nine = false;
        float slice[4] = {0, 0, 0, 0};
        int baseW = 0, baseH = 0;
    };

    class UiAssets {
    public:
        static UiAssets& Instance();

        void Configure(const std::string& vanillaRoot, const BlockLibrary* blocks);
        const std::string& VanillaRoot() const { return root; }

        bool Image(const std::string& rel, Texture& out);
        bool ImageSize(const std::string& rel, int& w, int& h);
        TextureSidecar Sidecar(const std::string& rel);

        const ItemSprite* Item(const std::string& key);
        bool ItemIcon(const std::string& key, Texture& out);
        bool HasHead(const std::string& key);
        void SetHead(const std::string& key, Texture head);
        void EraseHead(const std::string& key);
        bool Head(const std::string& key, Texture& out);
        void ClearItemCache();
        void ClearLevelCache();
        std::string LoadFont(const std::string& dataDir);
        uint32_t Generation() const { return generation; }

        std::string Localize(const std::string& key);
        std::string ItemName(const std::string& identifier);
        std::string ItemName(const std::string& identifier, int aux);

    private:
        bool ReadFile(const std::string& rel, std::string& out) const;
        bool FindImage(const std::string& rel, Texture& out) const;
        void LoadItemTextureTable();
        std::string FindByStem(const std::string& stem);
        void BuildStemIndex();
        void LoadLang();
        bool FlatSprite(const std::string& shortName, int aux, ItemSprite& out, int64_t dyeColor = -1,
                        const std::string& iconKey = {});
        bool BakeModelIcon(const std::string& name, int32_t hash, int size, Texture& out);

        std::string root;
        const BlockLibrary* blocks = nullptr;
        std::mutex mutex;
        std::unordered_map<std::string, ItemSprite> items;
        std::unordered_set<std::string> unbuiltIcons;
        std::unordered_map<std::string, Texture> heads;
        std::unordered_map<std::string, std::vector<std::string>> itemTextures;
        bool itemTableLoaded = false;
        uint32_t itemTableGeneration = 0;
        std::unordered_map<std::string, std::string> lang;
        bool langLoaded = false;
        uint32_t generation = 0;
        std::unordered_map<std::string, TextureSidecar> sidecars;
        uint64_t sidecarGeneration = 0;
        std::unordered_map<std::string, std::pair<int, int>> sizeCache;
        std::unordered_map<std::string, std::string> stemIndex;
        bool stemIndexBuilt = false;
        uint32_t stemGeneration = 0;
    };

    void BakeCubeIcon(const ItemSprite& sprite, int size, Texture& out);
}
