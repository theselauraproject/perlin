#pragma once
// the overworld shape data (data/minecraft/worldgen/...) as compiled into the server: see tools/gen_overworld_shape.py.
// the terrain shape is not in bedrock-samples or the dedicated server package, so these values come from Java's data pack and are hardcoded
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

    class DataPack {
    public:
        ~DataPack();
        // the built-in data; never fails
        static std::unique_ptr<DataPack> Embedded();
        // "worldgen/noise/continentalness.json" (relative to data/minecraft/); empty when missing
        std::string Read(const std::string& relative) const;
        bool Has(const std::string& relative) const;
        std::vector<std::string> List(const std::string& relativePrefix) const;

    private:
        DataPack() = default;
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
