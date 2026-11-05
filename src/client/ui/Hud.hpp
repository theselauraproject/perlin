#pragma once
#include "client/ui/HudRenderers.hpp"
#include "client/ui/HudData.hpp"
#include "client/ui/HudState.hpp"
#include "client/ui/JsonUi.hpp"
#include "client/ui/RmlBackend.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiLayout.hpp"

#include <future>
#include <memory>
#include <string>
#include <unordered_map>

namespace perlin {
    class RenderContext;
}

namespace perlin::ui {
    std::string ExtractPackUi(const std::string& dataDir, const std::string& folder = "pack_ui");

    class Hud {
    public:
        Hud();
        ~Hud();

        bool Initialize(RenderContext& renderer, const std::string& vanillaRoot, const std::string& dataDir,
                        bool allowEmpty = false);
        void RequestReload() { reloadRequested = true; }
        bool HasLevelUi() const { return tree != nullptr; }
        void Shutdown();
        void UnloadLevelUi();

        int Update(const HudState& state, int framebufferW, int framebufferH, int guiScaleOption);
        void Render();

        bool Cursor(float& x, float& y, float& w, float& hgt) const {
            float dx = 0, dy = 0;
            if (!cursorValid || !FilterHudElement("cursor_renderer", dx, dy))
                return false;
            x = cursorX * scale;
            y = cursorY * scale;
            w = hgt = 15.0f * scale;
            return true;
        }
        bool Ready() const { return backend.Ready() && tree != nullptr; }
        RmlBackend& Overlay() { return backend; }
        void UpdateOverlay(int framebufferW, int framebufferH, int guiScaleOption);
        void FireUiEvent(const std::string& name) {
            animHost.Fire(name);
            animHost.animating = true;
        }
        int OverlayScale() const { return scale; }
        float GuiWidth() const { return guiW; }
        float GuiHeight() const { return guiH; }
        int Scale() const { return scale; }
        const std::string& LastDocument() const { return lastRml; }
        size_t Rebuilds() const { return rebuilds; }
        void Reload() { reloadRequested = true; }
        void SetJavaStyle(bool java);
        bool Overrides(const std::string& ns) const {
            return tree && library.RootCount() > 3 && library.DefinedBefore(ns, library.RootCount() - 3);
        }

    private:
        bool Rebuild();
        void WriteDefaults();
        std::string ReadUserRcss() const;

        RmlBackend backend;
        UiLibrary library;
        std::unique_ptr<Node> tree;
        RmlEmitter emitter;
        std::unique_ptr<Node> crosshairTree;
        RmlEmitter cursorEmitter;
        bool cursorValid = false;
        float cursorX = 0, cursorY = 0;
        HudState state;
        std::unique_ptr<HudData> data;
        UiServices services;
        AnimHost animHost;
        std::unordered_map<std::string, Json> animCache;
        double lastAnimEval = 0;
        std::string dataDir, vanillaRoot;
        std::string lastRml;
        std::string lastRcss;
        uint64_t lastSignature = 0;
        uint64_t lastIncomingSignature = 0;
        std::string rcssCache;
        int scale = 1;
        float guiW = 0, guiH = 0;
        int lastFbW = 0, lastFbH = 0;
        double sinceCheck = 0;
        std::future<bool> changeProbe;
        bool probeLaunched = false;
        size_t rebuilds = 0;
        bool reloadRequested = false;
        bool templatesStale = true, emitterDefault = false;
        double templateCheck = 0;
        uint64_t hudTemplateVersion = 0, chatTemplateVersion = 0;
        bool dumped = false;
        bool javaStyle = false;
        uint64_t fontVersion = 0;
        uint64_t filterVersion = 0;
    };
}
