#pragma once
#include "client/systems/input/InputManager.hpp"
#include "client/ui/TitleModel.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace perlin::screen {
    // What starting a singleplayer world needs: an existing world by folder, or a new one with the settings the
    // Create New World screen chose.
    struct WorldLaunch {
        std::string folder;
        std::string name;
        bool fresh = false;
        std::string seedText;
        int gameMode = 0;
        int difficulty = 2;
        bool cheats = false;
        bool flat = false;
        bool structures = true;
        bool bonusChest = false;
        bool hardcore = false;
        std::string flatLayers; // "minecraft:bedrock*1,minecraft:dirt*2,..." bottom up, for a superflat world
    };

    // The superflat presets of the Customize screen, in the game's preset text (bottom layer first).
    struct FlatPreset {
        const char* name;
        const char* layers;
    };
    inline constexpr FlatPreset kFlatPresets[] = {
        {"Classic Flat", "minecraft:bedrock,2*minecraft:dirt,minecraft:grass_block"},
        {"Tunnelers' Dream", "minecraft:bedrock,230*minecraft:stone,5*minecraft:dirt,minecraft:grass_block"},
        {"Water World", "minecraft:bedrock,5*minecraft:stone,5*minecraft:dirt,5*minecraft:sand,90*minecraft:water"},
        {"Overworld", "minecraft:bedrock,59*minecraft:stone,3*minecraft:dirt,minecraft:grass_block"},
        {"Snowy Kingdom", "minecraft:bedrock,59*minecraft:stone,3*minecraft:dirt,minecraft:grass_block,minecraft:snow_layer"},
        {"Bottomless Pit", "2*minecraft:cobblestone,3*minecraft:dirt,minecraft:grass_block"},
        {"Desert", "minecraft:bedrock,3*minecraft:stone,52*minecraft:sandstone,8*minecraft:sand"},
        {"Redstone Ready", "minecraft:bedrock,3*minecraft:stone,116*minecraft:sandstone"},
        {"The Void", "minecraft:air"},
    };

    struct FlatLayerRow {
        std::string block;
        int count = 1;
    };
    // Layers of a preset text ("2*minecraft:dirt" or "minecraft:dirt"; anything after ';' is ignored), bottom up.
    // Empty when the text is not a valid layer list or the layers exceed the world's 384 blocks.
    std::vector<FlatLayerRow> ParseFlatLayers(const std::string& text);
    // The same layers in the server's form: "minecraft:dirt*2,...".
    std::string ServerFlatLayers(const std::vector<FlatLayerRow>& layers);

    // The singleplayer world list and the screens around it (Select World, Create New World, Edit World and the
    // delete confirmation), laid out as Java Edition lays them out. Worlds are Bedrock worlds, one folder each.
    class WorldLibrary {
    public:
        struct Deps {
            ui::TitleModel& title;
            std::function<void()> invalidate;
            std::function<void(const std::string&)> openUrl;
            std::function<void(const WorldLaunch&)> play;
            std::function<std::string()> worldsDir;
            std::function<std::string()> backupsDir;
        };

        explicit WorldLibrary(Deps d) : deps(std::move(d)) {}

        // Singleplayer: the world list, or straight to Create New World when there are no worlds yet.
        void Open();
        void Refresh();
        // Re-reads the list now and then while it shows, so a world that was just saved shows its new time.
        void Poll();
        bool Typed(const InputManager::Typed& t);
        bool Click(const std::string& hover);
        std::string Signature() const;

        // Exposed for tests: a folder name for a world name that is free under the worlds directory.
        static std::string FolderFor(const std::string& worldsDir, const std::string& name);
        static std::string DateText(long long unixSeconds);

    private:
        void ApplySearch();
        void OpenCreate(const ui::WorldEntry* from);
        void Create();
        void Play(int row);
        const ui::WorldEntry* Selected() const;
        void Backup(const ui::WorldEntry& e);
        void SaveEdit(const ui::WorldEntry& e);

        Deps deps;
        std::vector<ui::WorldEntry> all;
        int clickRow = -1;
        std::chrono::steady_clock::time_point clickAt, lastScan;
    };
}
