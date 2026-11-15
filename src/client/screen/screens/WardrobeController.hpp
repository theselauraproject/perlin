#pragma once
#include "client/systems/config/Options.hpp"
#include "client/screen/screens/WardrobeState.hpp"
#include "client/screen/screens/PauseScreen.hpp"
#include "client/systems/resources/TextureLoader.hpp"
#include "client/ui/Hud.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {
    class BedrockSession;
}

namespace perlin::screen {

    struct WardrobeDeps {
        Options& options;
        std::function<std::string()> activeAccountId;
        std::function<void(const std::string& id, const std::string& skin, const std::string& cape)> setSkin;
        std::function<void(const std::string& url)> openUrl;
        std::function<bool(const char* kind)> pickFile;
        ui::Hud& hud;
        BedrockSession* live;
        std::string& overlaySignature;
    };

    struct WardrobeFrame {
        std::chrono::steady_clock::time_point now;
        float gx, gy;
        bool down, click;
        float scroll;
        int fbW, fbH, guiW, guiH;
    };

    class WardrobeController {
    public:
        enum class Outcome { Stay, Done };

        WardrobeController(WardrobeState& state, WardrobeDeps deps) : state(state), deps(deps) {}
        Outcome Update(const WardrobeFrame& frame);

    private:
        struct Cards {
            std::vector<ui::WardrobeCard> skins, capes;
            std::vector<std::string> skinPaths, capePaths;
        };

        bool Inside(float x, float y, float w, float h) const;
        void InitState();
        void RefreshListing();
        bool SlimOf(const std::string& path) const;
        void RenderCard(const std::string& key, const std::vector<unsigned char>* skin, int sw, int sh, bool slim,
                        const std::vector<unsigned char>* cape, int cw, int ch, float yaw);
        void EnsureSkin(const std::string& path);
        void EnsureCape(const std::string& path);
        void EnsureDefaultCards();
        void Commit();
        Cards BuildCards(const std::string& defaultName);
        ui::WardrobeLayout LayoutFor(const Cards& cards) const;
        void ScrollList(const ui::WardrobeLayout& layout);
        void DragPreview(const ui::WardrobeLayout& layout, bool overPreview);
        std::string PreviewKey(bool slimNow) const;
        void RefreshPreview(const std::string& previewKey, bool slimNow, const ui::WardrobeLayout& layout);
        void ShowOverlay(const Cards& cards, const ui::WardrobeLayout& layout, const std::string& previewKey,
                         const std::string& defaultName, bool slimNow);
        Outcome HandleClick(const Cards& cards, const ui::WardrobeLayout& layout, bool slimNow);
        void RemoveFile(std::string& current);

        WardrobeState& state;
        WardrobeDeps deps;
        WardrobeFrame in{};
        Texture steve;
        bool haveSteve = false;
        std::string skinDir, capeDir;
    };
}
