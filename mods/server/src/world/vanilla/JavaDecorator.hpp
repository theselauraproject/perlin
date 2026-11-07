#pragma once
#include "core/Json.hpp"
#include "world/Chunk.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin::server::vanilla {

    struct JavaBiome {
        std::string name;
        float temperature = 0.5f;
        bool frozen = false;
        bool precipitation = true;
        std::array<std::vector<std::string>, 11> steps;
        std::unordered_set<std::string> placed;
        const json::Value* spawns = nullptr;
    };

    class JavaDecorator {
    public:
        explicit JavaDecorator(int64_t seed);
        ~JavaDecorator();
        bool Ok() const { return ok; }

        void Decorate(int chunkX, int chunkZ, const ChunkColumn* const region[9], ChunkColumn& out) const;

        const JavaBiome* Biome(int tableIndex) const;
        float Temperature(int tableIndex, int x, int y, int z) const;
        const json::Value& Tags(const std::string& tag) const;
        bool InTag(const std::string& tag, const std::string& name) const;
        size_t PlacedCount() const { return placedCount; }

        struct Impl;

    private:
        std::unique_ptr<Impl> impl;
        bool ok = false;
        size_t placedCount = 0;
    };
}
