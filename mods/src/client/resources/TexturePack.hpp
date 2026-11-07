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

    public:
        struct Impl;

    private:
        TexturePack();
        ~TexturePack();
        std::unique_ptr<Impl> impl;
        uint64_t generation = 0;
    };
}
