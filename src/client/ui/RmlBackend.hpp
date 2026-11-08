#pragma once
#include <memory>
#include <string>

namespace Rml {
    class Context;
    class ElementDocument;
}

namespace perlin {
    class RenderContext;
}

namespace perlin::ui {
    class BgfxRenderInterface;
    class GameSystemInterface;
    class McFontEngine;

    class RmlBackend {
    public:
        RmlBackend();
        ~RmlBackend();

        bool Initialize(RenderContext& renderer);
        void Shutdown();
        bool Ready() const { return ready; }

        void Resize(int guiW, int guiH, int scale);
        bool SetDocument(const std::string& rml, std::string& error);
        bool SetOverlay(const std::string& rml, std::string& error);
        void ClearOverlay();
        bool SetPluginLayer(const std::string& rml, std::string& error);
        void ClearPluginLayer();
        bool SetServerLayer(const std::string& rml, std::string& error);
        void ClearServerLayer();
        bool HasPluginLayer() const { return plugin != nullptr; }
        void MoveOverlayElement(const char* id, int guiX, int guiY);

        void SetOverlayAttribute(const char* id, const char* name, const std::string& value);
        bool HasOverlay() const { return overlay != nullptr; }
        void MouseMove(float guiX, float guiY);
        std::string OverlayHover() const;

        bool OverlayHoverClickSound(std::string& name, float& volume, float& pitch) const;
        std::string HoverLink() const;
        void Update();
        void Render();
        void ReloadTextures();
        void UnloadDocuments();

    private:
        bool ready = false;
        int guiW = 0, guiH = 0, scale = 1;
        std::unique_ptr<BgfxRenderInterface> renderInterface;
        std::unique_ptr<GameSystemInterface> systemInterface;
        std::unique_ptr<McFontEngine> fontEngine;
        Rml::Context* context = nullptr;
        Rml::ElementDocument* document = nullptr;
        Rml::ElementDocument* overlay = nullptr;
        Rml::ElementDocument* plugin = nullptr;
        Rml::ElementDocument* serverLayer = nullptr;
    };
}
