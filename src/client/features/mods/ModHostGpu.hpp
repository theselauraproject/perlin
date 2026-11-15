#pragma once
#include <string>

namespace perlin::mods {
    class ModManager;
    class OpScope;
}
namespace perlin::gpu {
    class ModGpuHub;
}

namespace perlin {
    void RegisterModGpuOps(gpu::ModGpuHub& hub, mods::OpScope& scope);
    void PumpModGpuEvents(gpu::ModGpuHub& hub, mods::ModManager& mm);
    void DropModGpu(gpu::ModGpuHub& hub, const std::string& plugin);
}
