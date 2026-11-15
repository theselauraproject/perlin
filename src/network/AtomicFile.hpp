#pragma once
#include <filesystem>
#include <string>

namespace perlin::auth {
    void EnsurePrivateDirectory(const std::filesystem::path& dir);
    bool WriteFileAtomic(const std::string& path, const std::string& bytes);
}
