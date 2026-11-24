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
    };

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
