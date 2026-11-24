// FindImageDeclarations must find exactly what the whole-text regex finds. Synthetic cases always run; given a
// directory (a shader pack checked out for local testing), every file under it is compared too.
#include "client/systems/javashaders/GlslTokens.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace perlin::jshader::glsl;

namespace {
    int failures = 0;
    double fastMs = 0, slowMs = 0;

    bool Same(const std::vector<ImageDecl>& a, const std::vector<ImageDecl>& b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].layoutFirst != b[i].layoutFirst || a[i].type != b[i].type || a[i].name != b[i].name)
                return false;
        return true;
    }

    size_t Compare(const std::string& text, const std::string& what) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto fast = FindImageDeclarations(text);
        const auto t1 = std::chrono::steady_clock::now();
        const auto slow = FindImageDeclarationsSlow(text);
        const auto t2 = std::chrono::steady_clock::now();
        fastMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        slowMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
        if (!Same(fast, slow)) {
            std::printf("FAIL %s: %zu declarations vs %zu from the whole-text regex\n", what.c_str(), fast.size(),
                        slow.size());
            failures++;
        }
        return slow.size();
    }
}

int main(int argc, char** argv) {
    const char* cases[] = {
        "layout(rgba8) uniform image2D colorimg0;",
        "void main(){}\nlayout (r32ui) uniform uimage3D voxel_img;\n",
        "layout(binding = 2, rgba16f) writeonly uniform image2D a; layout(rgba8) readonly uniform image3D b;",
        "uniform sampler2D tex; // image2D in a comment without a declaration\n",
        "layout(rgba8)\n  restrict\n  uniform\n  iimage2DArray  arr ;",
        "coherent volatile uniform image2D plain;",
        "struct S { int image2Dx; };",
        "layout(rgba8) uniform image2D unterminated",
        "image2D",
        "imageStore(img, ivec2(0), vec4(1)); imageLoad(x, ivec2(0));",
        "}layout(r32f) uniform image3D afterBrace;{layout(r32f) uniform image3D afterOpen;",
        "uniform image2D a; uniform image2D b; uniform image3D c;",
    };
    size_t found = 0;
    for (const char* c : cases)
        found += Compare(c, c);
    if (found < 11) {
        std::printf("FAIL synthetic cases found only %zu declarations\n", found);
        failures++;
    }
    if (argc > 1) {
        size_t files = 0, decls = 0;
        for (const auto& e : std::filesystem::recursive_directory_iterator(argv[1])) {
            if (!e.is_regular_file())
                continue;
            const std::string ext = e.path().extension().string();
            if (ext != ".glsl" && ext != ".fsh" && ext != ".vsh" && ext != ".csh" && ext != ".gsh" && ext != ".inc")
                continue;
            std::ifstream in(e.path(), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            decls += Compare(ss.str(), e.path().string());
            files++;
        }
        std::printf("%zu files, %zu declarations: fast %.1f ms, whole-text regex %.1f ms\n", files, decls, fastMs,
                    slowMs);
    }
    std::printf("%s\n", failures ? "glsl image declaration selftest FAILED" : "glsl image declaration selftest ok");
    return failures ? 1 : 0;
}
