#pragma once
#include <string>

namespace perlin::ui {
    inline constexpr const char* kBedLeaveId = "leave";
    std::string BuildBedScreenRml(const std::string& label, int guiW, int guiH);
}
