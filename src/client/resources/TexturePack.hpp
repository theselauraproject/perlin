#pragma once
#include "client/resources/TextureLoader.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    class TexturePack {
    public:
        static TexturePack& Instance();

        bool Open(const std::string& packsDir, const std::string& name, std::string& error);
        bool OpenStack(const std::string& packsDir, const std::vector<std::string>& names,
                       std::string& error);
        void Close();
        bool Active() const;
        uint64_t Generation() const { return generation; }
        bool Load(const std::string& rel, Texture& out) const;
        bool Has(const std::string& rel) const;
        std::vector<std::string> ReadTextLayers(const std::string& rel) const;
        std::vector<std::string> ListFiles(const std::string& dirRel, const std::string& suffix) const;
        std::string Description() const;

        // one pack of the stack (0 is the highest priority), for content that must come from a single pack
        size_t LayerCount() const;
        bool LayerJava(size_t layer) const;
        bool LayerReadText(size_t layer, const std::string& rel, std::string& out) const;
        bool LayerLoad(size_t layer, const std::string& rel, Texture& out) const;

    public:
        struct Impl;

    private:
        TexturePack();
        ~TexturePack();
        std::unique_ptr<Impl> impl;
        uint64_t generation = 0;
    };
}
