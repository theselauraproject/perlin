#pragma once
#include <string>

namespace perlin::mods {
    struct UiRequest {
        int openView = -1;
        int openPage = 0;
        std::string settingsFor;
    };
}
