#include "client/renderer/block/BlockLibrary.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/resources/TexturePack.hpp"

#include "client/input/Keys.hpp"

#include <miniz.h>

#include <cstdio>
#include <filesystem>
#include <iostream>

using namespace perlin;
namespace fs = std::filesystem;

namespace perlin {
    std::string KeyName(int) {
        return {};
    }
    int KeyFromName(const std::string&) {
        return 0;
    }
    std::string MouseName(int) {
        return {};
    }
    int MouseFromName(const std::string&) {
        return 0;
    }
}

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::cout << "FAIL line " << __LINE__ << ": " #c << std::endl;                                   \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "usage: texture_selftest <resource_pack dir>" << std::endl;
        return 0;
    }
    const fs::path tmp = fs::temp_directory_path() / "perlin_texture_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "packs/Fake 64x/textures/blocks");

    auto writePng = [](const fs::path& p, int w, int h, unsigned r, unsigned g, unsigned b) {
        std::vector<unsigned char> px((size_t)w * h * 4);
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = (unsigned char)r;
            px[i + 1] = (unsigned char)g;
            px[i + 2] = (unsigned char)b;
            px[i + 3] = 255;
        }
        size_t len = 0;
        void* png = tdefl_write_image_to_png_file_in_memory(px.data(), w, h, 4, &len);
        FILE* f = fopen(p.string().c_str(), "wb");
        fwrite(png, 1, len, f);
        fclose(f);
        mz_free(png);
    };
    writePng(tmp / "packs/Fake 64x/textures/blocks/stone.png", 64, 64, 200, 10, 10);
    writePng(tmp / "packs/Fake 64x/textures/blocks/dirt.png", 64, 64, 10, 200, 10);

    {
        mz_zip_archive zip{};
        const std::string zipPath = (tmp / "packs/Zipped.mcpack").string();
        CHECK(mz_zip_writer_init_file(&zip, zipPath.c_str(), 0));
        for (const char* n : {"stone.png", "dirt.png"}) {
            const std::string src = (tmp / "packs/Fake 64x/textures/blocks" / n).string();
            CHECK(mz_zip_writer_add_file(&zip, (std::string("Top Folder/textures/blocks/") + n).c_str(),
                                         src.c_str(), nullptr, 0, MZ_DEFAULT_LEVEL));
        }
        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
    }
    std::string error;
    TexturePack& pack = TexturePack::Instance();
    CHECK(pack.Open((tmp / "packs").string(), "Fake 64x", error));
    CHECK(pack.Active());
    Texture probe;
    CHECK(pack.Load("textures/blocks/stone.png", probe) && probe.width == 64);
    CHECK(!pack.Load("textures/blocks/nope.png", probe));
    CHECK(pack.Open((tmp / "packs").string(), "Zipped.mcpack", error));
    CHECK(!fs::exists(tmp / "packs/.cache"));
    CHECK(pack.Load("textures/blocks/dirt.png", probe) && probe.width == 64);
    CHECK(pack.Load("Textures/Blocks/DIRT.png", probe));
    CHECK(!pack.Open((tmp / "packs").string(), "missing", error) && !error.empty());
    CHECK(pack.Open((tmp / "packs").string(), "Zipped.mcpack", error));

    BlockLibrary lib;
    lib.SetTexturePack(true);
    CHECK(BlockLibrary::TilePx() == 64);
    const std::string vanilla = argv[1];
    for (int attempt = 0; attempt < 12 && !lib.EnsureLoaded((tmp / "perlin").string(), vanilla); attempt++) {}
    CHECK(lib.Ready());
    const int tiles = lib.TilesLoaded();
    CHECK(tiles > 100);
    CHECK(lib.AtlasPixels().size() == (size_t)tiles * 64 * 64 * 4);
    int stoneTile = 0;
    for (int t = 1; t < tiles; t++)
        if (lib.TilePathOf(t) == "textures/blocks/stone")
            stoneTile = t;
    CHECK(stoneTile > 0);
    if (stoneTile) {
        const uint8_t* p = lib.AtlasPixels().data() + (size_t)stoneTile * 64 * 64 * 4;
        CHECK(p[0] == 200 && p[1] == 10 && p[2] == 10);
        const uint8_t* last = p + ((size_t)63 * 64 + 63) * 4;
        CHECK(last[0] == 200);
    }
    int animated = 0;
    lib.PollAnimation(1, [&](int, const uint8_t*) { animated++; });
    CHECK(animated > 0);
    float u0, v0, u1, v1;
    BlockLibrary::TileUV(1, u0, v0, u1, v1);
    CHECK(u1 > u0 && v1 > v0);
    CHECK(std::abs((u1 - u0) - (1.0f / 128 - 1.0f / (128 * 64))) < 1e-6f);

    fs::remove_all(tmp);
    std::cout << (failures ? "texture_selftest FAILED" : "texture_selftest passed") << std::endl;
    return failures ? 1 : 0;
}
