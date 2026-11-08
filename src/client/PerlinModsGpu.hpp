#pragma once
#include <string>

namespace perlin::mods {
    class ModManager;
}

namespace perlin {
    void RegisterModGpuOps(mods::ModManager& mm);
    void PumpModGpuEvents(mods::ModManager& mm);
    void DropModGpu(const std::string& plugin);
}
