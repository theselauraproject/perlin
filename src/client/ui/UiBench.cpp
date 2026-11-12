// Perlin --uibench <vanilla resource pack>: times the hud's json ui scan and build, as Hud::Rebuild does them
#include "client/ui/HudData.hpp"
#include "client/ui/JsonUi.hpp"

#include <chrono>
#include <cstdio>
#include <string>

namespace perlin {
    int RunUiBench(const std::string& resourcePack, int rounds) {
        using Clock = std::chrono::steady_clock;
        const auto ms = [](Clock::time_point a) {
            return std::chrono::duration<double, std::milli>(Clock::now() - a).count();
        };
        double scanMs = 0, buildMs = 0;
        size_t nodes = 0, files = 0;
        for (int r = 0; r < rounds; r++) {
            ui::UiLibrary library;
            library.SetRoots({resourcePack + "/ui"});
            auto t = Clock::now();
            if (!library.Scan()) {
                std::printf("[uibench] no ui under %s\n", resourcePack.c_str());
                return 1;
            }
            scanMs += ms(t);
            t = Clock::now();
            ui::BuildReport report;
            auto tree = ui::BuildTree(library, "hud.hud_screen", ui::EngineRootVariables(), report);
            buildMs += ms(t);
            if (!tree) {
                std::printf("[uibench] hud.hud_screen could not be built\n");
                return 1;
            }
            nodes = report.nodes;
            files = library.FileCount();
        }
        std::printf("[uibench] %zu files, %zu controls: scan %.1f ms, build %.1f ms\n", files, nodes,
                    scanMs / rounds, buildMs / rounds);
        std::printf("RESULT ui_scan=%.2f ui_build=%.2f\n", scanMs / rounds, buildMs / rounds);
        return 0;
    }
}
