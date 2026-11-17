#pragma once
#include "client/ui/JsonScreen.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/ui/TitleModel.hpp"

#include <string>
#include <vector>

namespace perlin::screen::title {
    ui::ScreenData ForcedScreenData(const std::string& reference, const std::string& playerName);
    std::string ServersPath();
    void LoadServers(std::vector<ui::ServerEntry>& out, const std::string& fallback);
    void SaveServers(const std::vector<ui::ServerEntry>& servers);
    std::string SkinDollKey(ui::UiAssets& assets, const std::string& skinPath, const std::string& capePath, bool showCape, float headYaw,
                            float headPitch, std::string& lastKey);
    std::string SkinHeadKey(ui::UiAssets& assets, const std::string& skinPath);
    void ScrollList(int& scroll, int selected, int count, int rows, float wheel);
    bool IsReplayPage(ui::TitlePage page);
    bool IsServerFieldPage(ui::TitlePage page);
    ui::TitlePage ParentPage(ui::TitlePage page);
    void ApplyPingResult(ui::ServerEntry& entry, const ui::ServerEntry& result, bool withMotd);
    ui::ServerEntry SectionHeader(std::string name, size_t count, int sectionId);
}
