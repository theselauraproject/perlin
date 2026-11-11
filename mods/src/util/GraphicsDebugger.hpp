#pragma once
#include <string>

namespace perlin::gfxdebug {
    const char* Attached();
    bool WantsRenderDoc(int argc, char** argv);
    bool LoadRenderDoc(const std::string& captureDir);
}
