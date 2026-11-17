#pragma once
#include <filesystem>
#include <string>

namespace perlin::clientassets {
    std::string Directory(const std::string& dataDir);
    bool Ready(const std::string& dataDir);
    bool BuildFromJar(const std::filesystem::path& jar, const std::string& dataDir);
}
