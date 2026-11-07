#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {

    struct BiomeColorRule {
        std::array<float, 3> grass = {0, 0, 0};
        std::array<float, 3> foliage = {0, 0, 0};
        std::array<float, 3> dryFoliage = {0, 0, 0};
        std::array<float, 3> water = {0, 0, 0};
        bool hasGrass = false;
        bool hasFoliage = false;
        bool hasDry = false;
        bool hasWater = false;
    };

    class BiomeTints {
    public:
        bool EnsureLoaded(const std::string& vanillaRpRoot);
        void Reset() {
            ready.store(false, std::memory_order_release);
            attempts = 0;
        }
        bool Ready() const { return ready.load(std::memory_order_acquire); }

        std::array<float, 3> MapAverage(const std::string& mapName) const;

        std::array<float, 3> Grass(const std::string& biomeName) const;
        std::array<float, 3> Foliage(const std::string& biomeName) const;
        std::array<float, 3> DryFoliage(const std::string& biomeName) const;
        std::array<float, 3> Water(const std::string& biomeName) const;

        bool Climate(const std::string& biomeName, float& temperature, float& downfall) const {
            const auto it = climate.find(biomeName);
            if (it == climate.end())
                return false;
            temperature = it->second[0];
            downfall = it->second[1];
            return true;
        }

        std::string NameForId(int id) const;
        int IdForName(const std::string& name) const;

    private:
        std::array<float, 3> SampleMap(const std::vector<uint8_t>& rgb, float temperature,
                                       float downfall) const;
        std::array<float, 3> SampleMapChecked(const std::vector<uint8_t>& rgb, float temperature,
                                              float downfall, const char* mapName) const;
        static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

        std::atomic<bool> ready{false};
        int attempts = 0;
        std::unordered_map<std::string, std::array<float, 3>> mapAvg;
        std::unordered_map<std::string, std::vector<uint8_t>> mapRGB;
        std::unordered_map<std::string, BiomeColorRule> rules;
        std::unordered_map<std::string, std::array<float, 2>> climate;
        std::unordered_map<int, std::string> idToName;
        std::unordered_map<std::string, int> nameToId;
        std::array<float, 3> defaultWater = {0.266f, 0.686f, 0.961f};
        std::unordered_map<std::string, std::array<float, 3>> packWater;
        std::optional<std::array<float, 3>> packDefaultWater;
    };

    std::array<float, 3> ResolveDefaultWater(const std::string& vanillaRpRoot);
}
