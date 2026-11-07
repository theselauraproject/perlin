#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "client/resources/ClientAssets.hpp"

#include <cstdlib>
#include <stb_image.h>
#include <stb_image_write.h>

#include <miniz.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include "util/Log.hpp"

namespace fs = std::filesystem;

namespace perlin::clientassets {
    namespace {
        constexpr const char* kMarker = ".ready8";
        const std::string kBase = "assets/minecraft/textures/";

        struct Image {
            int w = 0, h = 0;
            std::vector<uint8_t> px;
            bool Valid() const { return w > 0 && h > 0; }
        };

        Image Blank(int w, int h) {
            Image im;
            im.w = w;
            im.h = h;
            im.px.assign(static_cast<size_t>(w) * h * 4, 0);
            return im;
        }

        Image Crop(const Image& src, int x, int y, int w, int h) {
            Image out = Blank(w, h);
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w; i++) {
                    const int sx = x + i, sy = y + j;
                    if (sx < 0 || sy < 0 || sx >= src.w || sy >= src.h)
                        continue;
                    std::memcpy(&out.px[(static_cast<size_t>(j) * w + i) * 4],
                                &src.px[(static_cast<size_t>(sy) * src.w + sx) * 4], 4);
                }
            return out;
        }

        void Paste(Image& dst, const Image& src, int x, int y) {
            for (int j = 0; j < src.h; j++)
                for (int i = 0; i < src.w; i++) {
                    const int dx = x + i, dy = y + j;
                    if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h)
                        continue;
                    std::memcpy(&dst.px[(static_cast<size_t>(dy) * dst.w + dx) * 4],
                                &src.px[(static_cast<size_t>(j) * src.w + i) * 4], 4);
                }
        }

        struct Jar {
            mz_zip_archive zip{};
            bool open = false;
            std::map<std::string, mz_uint> index;

            explicit Jar(const fs::path& path) {
                open = mz_zip_reader_init_file(&zip, path.string().c_str(), 0) != 0;
                if (!open)
                    return;
                const mz_uint count = mz_zip_reader_get_num_files(&zip);
                for (mz_uint i = 0; i < count; i++) {
                    char name[512];
                    if (mz_zip_reader_get_filename(&zip, i, name, sizeof(name)))
                        index[name] = i;
                }
            }
            ~Jar() {
                if (open)
                    mz_zip_reader_end(&zip);
            }

            bool Raw(const std::string& name, std::string& out) {
                const auto it = index.find(name);
                if (it == index.end())
                    return false;
                size_t size = 0;
                void* bytes = mz_zip_reader_extract_to_heap(&zip, it->second, &size, 0);
                if (!bytes)
                    return false;
                out.assign(static_cast<const char*>(bytes), size);
                mz_free(bytes);
                return true;
            }

            Image Load(const std::string& name) {
                Image im;
                const auto it = index.find(name);
                if (it == index.end())
                    return im;
                size_t size = 0;
                void* bytes = mz_zip_reader_extract_to_heap(&zip, it->second, &size, 0);
                if (!bytes)
                    return im;
                int w = 0, h = 0, n = 0;
                unsigned char* data = stbi_load_from_memory(static_cast<const unsigned char*>(bytes),
                                                            static_cast<int>(size), &w, &h, &n, 4);
                mz_free(bytes);
                if (!data)
                    return im;
                im.w = w;
                im.h = h;
                im.px.assign(data, data + static_cast<size_t>(w) * h * 4);
                stbi_image_free(data);
                return im;
            }
        };

        bool Save(const fs::path& dir, const std::string& name, const Image& im) {
            if (!im.Valid())
                return false;
            int len = 0;
            unsigned char* png = stbi_write_png_to_mem(im.px.data(), im.w * 4, im.w, im.h, 4, &len);
            if (!png)
                return false;
            std::ofstream out(dir / name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(png), len);
            STBIW_FREE(png);
            return out.good();
        }
    }

    std::string Directory(const std::string& dataDir) {
        return (fs::path(dataDir) / "assets").string();
    }

    bool Ready(const std::string& dataDir) {
        std::error_code ec;
        return fs::exists(fs::path(dataDir) / "assets" / kMarker, ec);
    }

    bool BuildFromJar(const fs::path& jarPath, const std::string& dataDir) {
        Jar jar(jarPath);
        if (!jar.open)
            return false;
        const fs::path dir = Directory(dataDir);
        std::error_code ec;
        fs::create_directories(dir, ec);
        int saved = 0, missing = 0;
        const auto emit = [&](const std::string& name, const Image& im) {
            if (Save(dir, name, im))
                saved++;
            else {
                missing++;
                Log("assets", "missing source for {}", name);
            }
        };
        const auto gui = [&](const std::string& rel) { return jar.Load(kBase + "gui/" + rel); };

        emit("ui_inventory.png", Crop(gui("container/inventory.png"), 0, 0, 176, 166));
        const std::pair<const char*, const char*> creative[] = {{"tab_items", "ui_creative_items"},
                                                                {"tab_item_search", "ui_creative_search"},
                                                                {"tab_inventory", "ui_creative_survival"}};
        for (const auto& [src, dst] : creative)
            emit(std::string(dst) + ".png",
                 Crop(gui(std::string("container/creative_inventory/") + src + ".png"), 0, 0, 195, 136));

        {
            Image tabs = Blank(26 * 7, 32 * 4);
            const char* names[] = {"tab_top_unselected", "tab_top_selected", "tab_bottom_unselected",
                                   "tab_bottom_selected"};
            for (int row = 0; row < 4; row++)
                for (int col = 0; col < 7; col++)
                    Paste(tabs,
                          gui(std::string("sprites/container/creative_inventory/") + names[row] + "_" +
                              std::to_string(col + 1) + ".png"),
                          26 * col, 32 * row);
            emit("ui_creative_tabs.png", tabs);
        }
        {
            Image misc = Blank(24, 15);
            Paste(misc, gui("sprites/container/creative_inventory/scroller.png"), 0, 0);
            Paste(misc, gui("sprites/container/creative_inventory/scroller_disabled.png"), 12, 0);
            emit("ui_creative_misc.png", misc);
        }
        {
            Image icons = Blank(16 * 5, 16);
            const char* names[] = {"helmet", "chestplate", "leggings", "boots", "shield"};
            for (int i = 0; i < 5; i++)
                Paste(icons, gui(std::string("sprites/container/slot/") + names[i] + ".png"), 16 * i, 0);
            emit("ui_slot_icons.png", icons);
        }
        {
            static const char* const kJarDye[16] = {
                "white",      "orange", "magenta", "light_blue", "yellow", "lime",  "pink", "gray",
                "light_gray", "cyan",   "purple",  "blue",       "brown",  "green", "red",  "black"};
            static const char* const kOutDye[16] = {
                "white",  "orange", "magenta", "light_blue", "yellow", "lime",  "pink", "gray",
                "silver", "cyan",   "purple",  "blue",       "brown",  "green", "red",  "black"};
            const Image down = jar.Load(kBase + "block/bed_down.png");
            const Image headNorth = jar.Load(kBase + "block/bed_head_north.png");
            for (int c = 0; c < 16; c++) {
                const std::string base = kBase + "block/" + kJarDye[c] + "_bed_";
                Image sheet = Blank(64, 48);
                const char* foot[4] = {"foot_up", "foot_south", "foot_east", "foot_west"};
                for (int i = 0; i < 4; i++)
                    Paste(sheet, jar.Load(base + foot[i] + ".png"), 16 * i, 0);
                Paste(sheet, jar.Load(base + "head_up.png"), 0, 16);
                Paste(sheet, headNorth, 16, 16);
                Paste(sheet, jar.Load(base + "head_east.png"), 32, 16);
                Paste(sheet, jar.Load(base + "head_west.png"), 48, 16);
                Paste(sheet, down, 0, 32);
                emit(std::string("bed_") + kOutDye[c] + ".png", sheet);
            }
        }
        emit("ui_horse.png", Crop(gui("container/horse.png"), 0, 0, 176, 166));
        emit("ui_chest.png", Crop(gui("container/generic_54.png"), 0, 0, 176, 222));
        emit("ui_horse_slot.png", Crop(gui("container/horse.png"), 7, 83, 18, 18));
        emit("ui_horse_chest.png", gui("sprites/container/horse/chest_slots.png"));
        {
            Image icons = Blank(32, 16);
            Paste(icons, gui("sprites/container/slot/saddle.png"), 0, 0);
            Paste(icons, gui("sprites/container/slot/horse_armor.png"), 16, 0);
            emit("ui_horse_icons.png", icons);
        }
        emit("ui_crafting_table.png", Crop(gui("container/crafting_table.png"), 0, 0, 176, 166));
        emit("ui_furnace.png", Crop(gui("container/furnace.png"), 0, 0, 176, 166));
        emit("ui_blast_furnace.png", Crop(gui("container/blast_furnace.png"), 0, 0, 176, 166));
        emit("ui_smoker.png", Crop(gui("container/smoker.png"), 0, 0, 176, 166));
        {
            Image progress = Blank(24, 30);
            Paste(progress, gui("sprites/container/furnace/lit_progress.png"), 0, 0);
            Paste(progress, gui("sprites/container/furnace/burn_progress.png"), 0, 14);
            emit("ui_furnace_progress.png", progress);
        }
        emit("ui_recipe_book.png", Crop(gui("recipe_book.png"), 1, 1, 147, 166));
        {
            Image btn = Blank(20, 36);
            Paste(btn, gui("sprites/recipe_book/button.png"), 0, 0);
            Paste(btn, gui("sprites/recipe_book/button_highlighted.png"), 0, 18);
            emit("ui_recipe_button.png", btn);
            // the book alone: the button's own fill is cut away, so it sits on whatever button texture is in use
            const Image face = gui("sprites/recipe_book/button.png");
            if (face.Valid() && face.w >= 20 && face.h >= 18) {
                Image icon = Crop(face, 3, 3, 14, 12);
                const uint8_t* fill = &face.px[(static_cast<size_t>(3) * face.w + 3) * 4];
                for (size_t i = 0; i < icon.px.size(); i += 4) {
                    const int d = std::abs(icon.px[i] - fill[0]) + std::abs(icon.px[i + 1] - fill[1]) +
                                  std::abs(icon.px[i + 2] - fill[2]);
                    if (d < 30)
                        icon.px[i + 3] = 0;
                }
                emit("ui_recipe_icon.png", icon);
            }
        }
        {
            Image tabs = Blank(70, 27);
            Paste(tabs, gui("sprites/recipe_book/tab.png"), 0, 0);
            Paste(tabs, gui("sprites/recipe_book/tab_selected.png"), 35, 0);
            emit("ui_recipe_tabs.png", tabs);
        }
        {
            Image slots = Blank(50, 25);
            Paste(slots, gui("sprites/recipe_book/slot_craftable.png"), 0, 0);
            Paste(slots, gui("sprites/recipe_book/slot_uncraftable.png"), 25, 0);
            emit("ui_recipe_slots.png", slots);
        }
        for (const char* name : {"effect_background", "effect_background_ambient"})
            emit(std::string("ui_inv_") + name + ".png",
                 gui(std::string("sprites/container/inventory/") + name + ".png"));
        {
            Image pages = Blank(48, 17);
            const char* names[] = {"page_backward", "page_backward_highlighted", "page_forward",
                                   "page_forward_highlighted"};
            for (int i = 0; i < 4; i++)
                Paste(pages, gui(std::string("sprites/recipe_book/") + names[i] + ".png"), 12 * i, 0);
            emit("ui_recipe_pages.png", pages);
        }
        {
            Image hud = Blank(48, 24);
            Paste(hud, gui("sprites/hud/effect_background.png"), 0, 0);
            Paste(hud, gui("sprites/hud/effect_background_ambient.png"), 24, 0);
            emit("ui_hud_effect.png", hud);
        }
        const std::string effects = kBase + "mob_effect/";
        for (const auto& [name, index] : jar.index) {
            (void)index;
            if (name.compare(0, effects.size(), effects) == 0 && name.size() > 4 &&
                name.compare(name.size() - 4, 4, ".png") == 0 &&
                name.find('/', effects.size()) == std::string::npos)
                emit("effect_" + name.substr(effects.size()), jar.Load(name));
        }
        emit("spyglass_model.png", jar.Load(kBase + "item/spyglass.png"));
        emit("spyglass_scope.png", jar.Load(kBase + "misc/spyglass_scope.png"));

        {
            const std::pair<const char*, const char*> sprites[] = {
                {"ui_menu_btn.png", "sprites/widget/button.png"},
                {"ui_menu_btn_hi.png", "sprites/widget/button_highlighted.png"},
                {"ui_menu_btn_off.png", "sprites/widget/button_disabled.png"},
                {"ui_menu_field.png", "sprites/widget/text_field.png"},
                {"ui_menu_field_hi.png", "sprites/widget/text_field_highlighted.png"},
                {"ui_menu_logo.png", "title/minecraft.png"},
                {"ui_menu_edition.png", "title/edition.png"},
                {"ui_menu_bg.png", "menu_background.png"},
                {"ui_menu_list_bg.png", "menu_list_background.png"},
                {"ui_menu_header.png", "header_separator.png"},
                {"ui_menu_footer.png", "footer_separator.png"},
                {"ui_menu_join.png", "sprites/server_list/join.png"},
                {"ui_menu_join_hi.png", "sprites/server_list/join_highlighted.png"},
                {"ui_menu_ping.png", "sprites/server_list/ping_5.png"},
                {"ui_menu_unreachable.png", "sprites/server_list/unreachable.png"},
                {"ui_menu_language.png", "sprites/icon/language.png"},
                {"ui_menu_accessibility.png", "sprites/icon/accessibility.png"},
                {"ui_menu_scroller.png", "sprites/widget/scroller.png"},
            };
            for (const auto& [dst, src] : sprites)
                emit(dst, gui(src));
        }

        // end poem and mojang's java credits, rolled when leaving the end
        for (const char* text : {"end.txt", "credits.json", "postcredits.txt"}) {
            std::string bytes;
            if (jar.Raw(std::string("assets/minecraft/texts/") + text, bytes))
                std::ofstream(dir / text, std::ios::binary) << bytes;
        }

        Log("assets", "built {} client images ({} missing)", saved, missing);
        if (saved < 30)
            return false;
        std::ofstream(dir / kMarker) << "1";
        return true;
    }
}
