#include "client/systems/renderer/gpu/JavaProgramCache.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace {
    using perlin::gpu::program_cache::Program;

    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            failures++;
        }
    }

    Program Sample() {
        Program p;
        p.uniformRegs = 7;
        perlin::jshader::UniformDecl u;
        u.name = "gbufferModelView";
        u.type = "mat4";
        u.arraySize = "";
        u.arrayCount = 1;
        u.reg = 2;
        u.regs = 4;
        u.regsPerElement = 4;
        u.components = 16;
        p.uniforms.push_back(u);
        p.drawBuffers = {0, 1, 3};
        perlin::jshader::RhiSampler s;
        s.name = "colortex0";
        s.slot = 5;
        s.shadow = true;
        p.samplers.push_back(s);
        p.images = {"colorimg0"};
        p.vertex = {1, 2, 3, 4};
        p.fragment = {9, 8, 7};
        return p;
    }

    std::string ReadAll(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    void WriteAll(const std::filesystem::path& file, const std::string& data) {
        std::ofstream(file, std::ios::binary | std::ios::trunc).write(data.data(), static_cast<std::streamsize>(data.size()));
    }
}

int main() {
    namespace pc = perlin::gpu::program_cache;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "perlin_program_cache_selftest";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "sample.prog";

    const Program original = Sample();
    pc::Store(file, original);
    const auto loaded = pc::Load(file);
    Check(loaded.has_value(), "stored program loads back");
    if (loaded) {
        Check(loaded->uniformRegs == original.uniformRegs, "uniform registers survive");
        Check(loaded->uniforms.size() == 1 && loaded->uniforms[0].name == "gbufferModelView", "uniforms survive");
        Check(loaded->drawBuffers == original.drawBuffers, "draw buffers survive");
        Check(loaded->samplers.size() == 1 && loaded->samplers[0].shadow && loaded->samplers[0].slot == 5,
              "samplers survive");
        Check(loaded->images == original.images, "images survive");
        Check(loaded->vertex == original.vertex && loaded->fragment == original.fragment, "code survives");
    }

    const std::string bytes = ReadAll(file);
    for (size_t keep = 0; keep < bytes.size(); keep++) {
        WriteAll(dir / "cut.prog", bytes.substr(0, keep));
        Check(!pc::Load(dir / "cut.prog").has_value(), "every truncation is rejected");
    }

    std::string hostile = bytes;
    const size_t countOffset = sizeof(uint32_t) * 2;
    hostile.replace(countOffset, sizeof(uint32_t), "\xff\xff\xff\xff");
    WriteAll(dir / "hostile.prog", hostile);
    Check(!pc::Load(dir / "hostile.prog").has_value(), "an absurd element count is rejected");

    Check(!pc::Load(dir / "missing.prog").has_value(), "a missing file is a miss");
    Check(!pc::Load({}).has_value(), "an empty path is a miss");

    std::filesystem::remove_all(dir);
    if (failures)
        return 1;
    std::puts("java_program_cache_selftest ok");
    return 0;
}
