#pragma once
#include <string>

namespace perlin::ui {
    struct SettingInfo {
        std::string description;
        int impact = 0;
    };
    bool FindSettingInfo(const std::string& label, SettingInfo& out);
}
