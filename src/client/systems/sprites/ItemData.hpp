#pragma once
#include <string>

namespace perlin::ui {
    int MaxDurability(const std::string& identifier);
    int ArmorPoints(const std::string& identifier);
    bool IsFood(const std::string& name);
    bool CanEatNow(const std::string& name, int food);
}
