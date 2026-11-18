#include "client/systems/renderer/gpu/GpuFeatures.hpp"

#include <cstdio>

using perlin::gpu::GpuFeatures;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            failures++;
    }

    GpuFeatures::Profile Desktop() {
        GpuFeatures::Profile p;
        p.type = conduit::adapter_type::discrete;
        p.api = conduit::api::vulkan;
        p.vendor = 0x10DE;
        p.name = "NVIDIA GeForce RTX 4070";
        p.gpuDriven = p.multiDraw = p.firstInstance = true;
        return p;
    }
}

int main() {
    std::array<bool, GpuFeatures::kFeatureCount> on{};
    GpuFeatures::Profile p = Desktop();
    Check(GpuFeatures::Choose(p, on) == GpuFeatures::Level::GpuDrivenOcclusion, "discrete gpu gets every feature");
    Check(on[GpuFeatures::GpuCull] && on[GpuFeatures::Occlusion] && on[GpuFeatures::EntityCull] && on[GpuFeatures::GpuUi],
          "discrete gpu features on");

    p = Desktop();
    p.type = conduit::adapter_type::integrated;
    p.vendor = 0x8086;
    p.name = "Intel(R) UHD Graphics 620";
    Check(GpuFeatures::Choose(p, on) == GpuFeatures::Level::GpuDriven, "intel integrated is gpu-driven");
    Check(on[GpuFeatures::GpuCull] && !on[GpuFeatures::Occlusion], "intel integrated skips occlusion");

    p = Desktop();
    p.type = conduit::adapter_type::integrated;
    p.vendor = 0x5143;
    p.api = conduit::api::vulkan;
    p.name = "Adreno (TM) 740";
    GpuFeatures::Choose(p, on);
    Check(on[GpuFeatures::GpuCull] && !on[GpuFeatures::Occlusion], "tile-based mobile gpu skips occlusion");

    p = Desktop();
    p.type = conduit::adapter_type::cpu;
    p.name = "llvmpipe (LLVM 20.1.2, 256 bits)";
    GpuFeatures::Choose(p, on);
    Check(GpuFeatures::Software(p), "llvmpipe is software");
    Check(on[GpuFeatures::GpuCull] && !on[GpuFeatures::Occlusion] && !on[GpuFeatures::EntityCull],
          "software renderer keeps cheap culling only");

    p = Desktop();
    p.gpuDriven = false;
    Check(GpuFeatures::Choose(p, on) == GpuFeatures::Level::MultiDraw, "no indirect count falls back to multi-draw");
    Check(!on[GpuFeatures::GpuCull] && !on[GpuFeatures::Occlusion] && on[GpuFeatures::GpuUi], "multi-draw features");

    p = Desktop();
    p.multiDraw = false;
    p.gpuDriven = false;
    Check(GpuFeatures::Choose(p, on) == GpuFeatures::Level::Basic, "no multi-draw is basic");
    Check(!on[GpuFeatures::GpuUi], "basic has no batched ui");

    p = Desktop();
    p.safeMode = 1;
    GpuFeatures::Choose(p, on);
    Check(!on[GpuFeatures::GpuCull] && on[GpuFeatures::GpuUi], "safe mode 1 drops gpu-driven terrain");
    p.safeMode = 2;
    GpuFeatures::Choose(p, on);
    Check(!on[GpuFeatures::GpuUi], "safe mode 2 drops batched ui");

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
