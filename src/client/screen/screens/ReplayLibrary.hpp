#pragma once
#include "client/ui/TitleModel.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/ui/ReplayControls.hpp"
#include "client/systems/input/InputManager.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace perlin::screen {
    class ReplayLibrary {
    public:
        struct Deps {
            ui::TitleModel& title;
            std::function<void()> invalidate;
            std::function<void(const std::string&)> openUrl;
            std::function<void(const std::string&)> play;
            std::function<void(const std::string&)> announce;
        };

        explicit ReplayLibrary(Deps d) : deps(std::move(d)) {}

        void Refresh();
        bool Typed(const InputManager::Typed& t);
        bool Click(const std::string& hover);
        bool Import(const std::string& source);

    private:
        struct CachedMeta {
            std::filesystem::file_time_type mtime;
            uintmax_t size = 0;
            replay::Metadata meta;
            bool ok = false;
            std::string error;
        };

        Deps deps;
        std::map<std::string, CachedMeta> metaCache;
        bool recoveryChecked = false;
        int clickRow = -1;
        std::chrono::steady_clock::time_point clickAt;
    };
}
