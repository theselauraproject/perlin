#pragma once
#include <string>

namespace perlin::ui {
    // end poem, our credits and mojang's java credits, shown on ShowCredits (leaving the end);
    // `scroll`: how far the text moved up from the bottom edge, in gui pixels
    std::string BuildCreditsScreenRml(int guiW, int guiH, int scroll, const std::string& player);
    int CreditsHeight();
}
