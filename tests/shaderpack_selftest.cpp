#include "client/resources/ShaderPack.hpp"

#include <miniz.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace perlin;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                 \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main() {
    const fs::path root = fs::temp_directory_path() / "perlin_shaderpack_test";
    fs::remove_all(root);
    fs::create_directories(root / "Folder" / "nested" / "spirv");
    { std::ofstream(root / "Folder" / "nested" / "spirv" / "vs_terrain.bin", std::ios::binary) << "ABC"; }
    {
        std::ofstream(root / "Folder" / "nested" / "pack.json")
            << R"({"name":"Folder Pack","description":"d"})";
    }

    std::string error;
    ShaderPack& pack = ShaderPack::Instance();
    CHECK(pack.Open(root.string(), "Folder", error));
    CHECK(pack.Active() && pack.HasBackend("spirv") && !pack.HasBackend("dxbc"));
    std::vector<char> bytes;
    CHECK(pack.Read("spirv", "vs_terrain", bytes) && bytes.size() == 3 && bytes[0] == 'A');
    CHECK(!pack.Read("spirv", "fs_sky", bytes));
    CHECK(pack.Description() == "Folder Pack");

    const std::string zipPath = (root / "Zipped.zip").string();
    CHECK(mz_zip_add_mem_to_archive_file_in_place(zipPath.c_str(), "Top/dxbc/vs_terrain.bin", "XYZW", 4,
                                                  nullptr, 0, 0));
    CHECK(mz_zip_add_mem_to_archive_file_in_place(zipPath.c_str(), "Top/glsl/vs_terrain.bin", "Q", 1, nullptr,
                                                  0, 0));
    CHECK(pack.Open(root.string(), "Zipped", error));
    CHECK(pack.HasBackend("dxbc") && pack.HasBackend("glsl") && !pack.HasBackend("spirv"));
    CHECK(pack.Read("dxbc", "vs_terrain", bytes) && bytes.size() == 4 && bytes[3] == 'W');

    CHECK(!pack.Open(root.string(), "Missing", error) && !error.empty());
    CHECK(!pack.Active());
    fs::remove_all(root);
    std::printf(failures ? "shaderpack_selftest FAILED\n" : "shaderpack_selftest ok\n");
    return failures ? 1 : 0;
}
