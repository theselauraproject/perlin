#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace perlin {
    struct FogMedium {
        std::optional<float> start, end;
        std::optional<glm::vec3> color;
        bool relative = false;
    };

    struct FogSettings {
        FogMedium air, water, lava, lavaResistance, weather;
    };

    class FogDefs {
    public:
        void Configure(const std::vector<std::string>& packDirs);
        bool Has(const std::string& identifier) const { return defs.find(identifier) != defs.end(); }
        FogMedium Resolve(const std::vector<std::string>& stack, int medium) const;

    private:
        std::string key;
        std::unordered_map<std::string, FogSettings> defs;
    };
}
