#pragma once
#include <string>
#include <vector>

#include "client/ui/UiLayout.hpp"

namespace perlin::ui {
    void BindAssetServices(UiServices& services);
    std::string WarmKey(const std::vector<std::string>& packDirs);
}
