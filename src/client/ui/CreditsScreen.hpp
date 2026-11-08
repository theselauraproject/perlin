#pragma once
#include <string>

namespace perlin::ui {

    std::string BuildCreditsScreenRml(int guiW, int guiH, int scroll, const std::string& player);
    int CreditsHeight();
}
