#pragma once
#include "client/systems/model/HudState.hpp"
#include "client/screen/screens/InventoryView.hpp"
#include "client/ui/JsonUi.hpp"
#include "client/ui/SceneEmit.hpp"
#include "client/ui/UiLayout.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace perlin::ui {
    class ChestUi {
    public:
        ChestUi();
        ~ChestUi();
        void Reset();
        void Warm(const std::vector<std::string>& packDirs);
        bool Ready() const { return state.load() == 2; }
        bool WaitReady(int timeoutMs);
        bool Build(const InventoryView& view, const std::string& screen, int guiW, int guiH, SceneDoc& doc,
                   float (&panel)[4]);
        const std::string& Problem() const { return problem; }

    private:
        UiLibrary library;
        std::map<std::string, std::unique_ptr<Node>> trees;
        SceneEmitter emitter;
        UiServices services;
        HudState hudState;
        const InventoryView* current = nullptr;
        std::string problem, key;
        std::set<std::string> dumped;
        std::thread worker;
        std::atomic<int> state{0};
    };

    std::string ChestScreenName(const std::string& titleKey, int rows);
}
