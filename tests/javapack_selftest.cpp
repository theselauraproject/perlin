#include "client/resources/JavaPack.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/resources/TexturePack.hpp"

#include <miniz.h>

#include <cstdio>
#include <filesystem>
#include <iostream>

using namespace perlin;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::cout << "FAIL line " << __LINE__ << ": " #c << std::endl;                                   \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

namespace {
    struct Image {
        int w, h;
        std::vector<unsigned char> px;
        Image(int w, int h) : w(w), h(h), px(static_cast<size_t>(w) * h * 4, 0) {}
        void fill(int x, int y, int rw, int rh, unsigned r, unsigned g, unsigned b) {
            for (int j = y; j < y + rh; j++)
                for (int i = x; i < x + rw; i++) {
                    unsigned char* p = &px[(static_cast<size_t>(j) * w + i) * 4];
                    p[0] = static_cast<unsigned char>(r), p[1] = static_cast<unsigned char>(g),
                    p[2] = static_cast<unsigned char>(b), p[3] = 255;
                }
        }
        std::string png() const {
            size_t len = 0;
            void* data = tdefl_write_image_to_png_file_in_memory(px.data(), w, h, 4, &len);
            std::string out(static_cast<const char*>(data), len);
            mz_free(data);
            return out;
        }
    };

    void WriteFile(const fs::path& p, const std::string& bytes) {
        fs::create_directories(p.parent_path());
        FILE* f = fopen(p.string().c_str(), "wb");
        fwrite(bytes.data(), 1, bytes.size(), f);
        fclose(f);
    }

    bool Pixel(const Texture& t, int x, int y, unsigned r, unsigned g, unsigned b) {
        const unsigned char* p = &t.pixels[(static_cast<size_t>(y) * t.width + x) * 4];
        return p[0] == r && p[1] == g && p[2] == b;
    }
}

int main() {
    {
        const auto planks = javapack::Sources("textures/blocks/planks_big_oak.png");
        bool legacy = false, modern = false;
        for (const auto& s : planks) {
            legacy |= s.path == "textures/blocks/planks_big_oak.png";
            modern |= s.path == "textures/block/dark_oak_planks.png";
        }
        CHECK(legacy && modern);
        bool wool = false, dye = false, gold = false, sheet = false;
        for (const auto& s : javapack::Sources("textures/blocks/wool_colored_silver"))
            wool |= s.path == "textures/block/light_gray_wool.png";
        for (const auto& s : javapack::Sources("textures/items/dye_powder_blue"))
            dye |= s.path == "textures/item/blue_dye.png";
        for (const auto& s : javapack::Sources("textures/items/gold_pickaxe"))
            gold |= s.path == "textures/item/golden_pickaxe.png";
        for (const auto& s : javapack::Sources("textures/ui/heart"))
            sheet |= s.path == "textures/gui/icons.png" && !s.Whole();
        CHECK(wool && dye && gold && sheet);
        CHECK(javapack::Sources("textures/entity/unknown_thing").empty());
    }

    const fs::path tmp = fs::temp_directory_path() / "perlin_javapack_test";
    fs::remove_all(tmp);

    Image widgets(256, 256);
    widgets.fill(0, 0, 182, 22, 0, 0, 0);
    widgets.fill(1, 0, 20, 22, 0, 200, 0);
    widgets.fill(61, 0, 20, 22, 200, 0, 0);
    widgets.fill(0, 22, 24, 24, 0, 0, 200);
    Image icons(256, 256);
    icons.fill(52, 0, 9, 9, 250, 10, 10);
    icons.fill(16, 27, 9, 9, 250, 250, 10);
    icons.fill(0, 69, 182, 5, 10, 250, 10);
    Image oak(16, 16);
    oak.fill(0, 0, 16, 16, 150, 100, 50);
    Image stone(32, 32);
    stone.fill(0, 0, 32, 32, 120, 120, 120);

    const std::string files[][2] = {{"assets/minecraft/textures/gui/widgets.png", widgets.png()},
                                    {"assets/minecraft/textures/gui/icons.png", icons.png()},
                                    {"assets/minecraft/textures/block/oak_planks.png", oak.png()},
                                    {"assets/minecraft/textures/blocks/stone.png", stone.png()},
                                    {"pack.mcmeta", "{\"pack\":{\"pack_format\":1,\"description\":\"x\"}}"}};

    {
        mz_zip_archive zip{};
        const std::string path = (tmp / "packs/Java.zip").string();
        fs::create_directories(tmp / "packs");
        CHECK(mz_zip_writer_init_file(&zip, path.c_str(), 0));
        for (const auto& f : files)
            CHECK(mz_zip_writer_add_mem(&zip, ("My Pack/" + f[0]).c_str(), f[1].data(), f[1].size(),
                                        MZ_DEFAULT_LEVEL));
        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
    }
    for (const auto& f : files)
        WriteFile(tmp / "packs/JavaFolder" / f[0], f[1]);

    TexturePack& pack = TexturePack::Instance();
    for (const char* name : {"Java.zip", "JavaFolder"}) {
        std::string error;
        CHECK(pack.Open((tmp / "packs").string(), name, error));
        Texture t;
        CHECK(pack.Load("textures/blocks/planks_oak.png", t) && t.width == 16 &&
              Pixel(t, 3, 3, 150, 100, 50));
        CHECK(pack.Load("textures/blocks/stone.png", t) && t.width == 32);
        CHECK(pack.Load("textures/ui/hotbar_0.png", t) && t.width == 20 && t.height == 22 &&
              Pixel(t, 5, 5, 0, 200, 0));
        CHECK(pack.Load("textures/ui/hotbar_3.png", t) && t.width == 20 && t.height == 22 &&
              Pixel(t, 5, 5, 200, 0, 0));
        CHECK(pack.Load("textures/ui/selected_hotbar_slot.png", t) && t.width == 24 && t.height == 24 &&
              Pixel(t, 10, 10, 0, 0, 200));
        CHECK(pack.Load("textures/ui/heart.png", t) && t.width == 9 && t.height == 9 &&
              Pixel(t, 4, 4, 250, 10, 10));
        CHECK(pack.Load("textures/ui/hunger_background.png", t) && t.width == 9 &&
              Pixel(t, 0, 0, 250, 250, 10));
        CHECK(pack.Load("textures/ui/experience_bar_full.png", t) && t.width == 182 && t.height == 5);
        CHECK(!pack.Load("textures/ui/armor_full.png", t) || t.width == 9);
        CHECK(pack.Has("textures/ui/heart.png"));
        CHECK(!pack.Has("textures/blocks/not_there.png"));
        CHECK(!pack.Load("textures/blocks/not_there.png", t));
    }

    {
        mz_zip_archive zip{};
        const std::string path = (tmp / "packs/Realms.zip").string();
        CHECK(mz_zip_writer_init_file(&zip, path.c_str(), 0));
        const std::string wrapper = "\xC2\xA7"
                                    "9midnight [16x]/";
        const std::string other = oak.png();
        CHECK(mz_zip_writer_add_mem(&zip, (wrapper + "assets/realms/textures/gui/x.png").c_str(),
                                    other.data(), other.size(), MZ_DEFAULT_LEVEL));
        for (const auto& f : files)
            CHECK(mz_zip_writer_add_mem(&zip, (wrapper + f[0]).c_str(), f[1].data(), f[1].size(),
                                        MZ_DEFAULT_LEVEL));
        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
        std::string error;
        CHECK(pack.Open((tmp / "packs").string(), "Realms.zip", error));
        Texture t;
        CHECK(pack.Load("textures/ui/hotbar_3.png", t) && t.width == 20);
        CHECK(pack.Load("textures/blocks/planks_oak.png", t) && t.width == 16);
    }

    {
        Image big(512, 512);
        big.fill(122, 0, 40, 44, 7, 8, 9);
        WriteFile(tmp / "packs/Big/assets/minecraft/textures/gui/widgets.png", big.png());
        WriteFile(tmp / "packs/Big/pack.mcmeta", "{}");
        std::string error;
        CHECK(pack.Open((tmp / "packs").string(), "Big", error));
        Texture t;
        CHECK(pack.Load("textures/ui/hotbar_3.png", t) && t.width == 40 && t.height == 44 &&
              Pixel(t, 20, 20, 7, 8, 9));
    }

    fs::remove_all(tmp);
    if (failures) {
        std::cout << failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "javapack_selftest ok" << std::endl;
    return 0;
}
