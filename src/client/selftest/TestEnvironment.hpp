#pragma once
#include "util/EnvVar.hpp"
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

namespace perlin::selftest {
    class TempDataDir {
    public:
        TempDataDir() {
            path = std::filesystem::temp_directory_path() /
                   ("perlin_selftest_" + std::to_string(std::random_device{}()));
            std::filesystem::create_directories(path);
            perlin::env::Set("PERLIN_DATA_DIR", path.string().c_str());
        }
        ~TempDataDir() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            perlin::env::Unset("PERLIN_DATA_DIR");
        }
        TempDataDir(const TempDataDir&) = delete;
        TempDataDir& operator=(const TempDataDir&) = delete;
        const std::filesystem::path& Path() const { return path; }

    private:
        std::filesystem::path path;
    };
}
