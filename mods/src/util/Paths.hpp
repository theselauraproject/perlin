#pragma once
#include <string>

namespace perlin::paths {
    std::string ExePath();
    std::string ExeDir();
    std::string DataDir();
    void EnsureDir(const std::string& dir);
}
