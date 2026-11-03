#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::server {

    struct RecipeIngredient {
        std::string item;
        int count = 1;
    };

    struct CraftRecipe {
        uint32_t netId = 0;
        bool shaped = false;
        int width = 0, height = 0;
        std::vector<RecipeIngredient> input;
        std::string output;
        int16_t outputId = 0;
        int outputCount = 1;
        int outputData = 0;
    };

    struct SmeltRecipe {
        bool furnace = false, blast = false, smoker = false, campfire = false;
        std::string input;
        std::string output;
        int16_t outputId = 0;
        int outputCount = 1;
    };

    class Recipes {
    public:
        static Recipes& Instance();
        void Load();
        const CraftRecipe* ByNetId(uint32_t id) const;
        const SmeltRecipe* Smelt(const std::string& item, int station) const;
        const std::vector<CraftRecipe>& Crafting() const { return crafting; }
        const std::vector<uint8_t>& CraftingDataBody() const { return body; }
        static bool Matches(const std::string& ingredient, const std::string& item);
        static int FuelTicks(const std::string& item);

    private:
        bool loaded = false;
        std::vector<CraftRecipe> crafting;
        std::vector<SmeltRecipe> smelting;
        std::vector<uint8_t> body;
    };
}
