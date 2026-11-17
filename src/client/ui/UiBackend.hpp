#pragma once
#include "client/ui/Scene.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    class RenderContext;
}

namespace perlin::ui {
    class UiBackend {
    public:
        UiBackend();
        ~UiBackend();

        bool Initialize(RenderContext& renderer);
        void Shutdown();
        bool Ready() const { return ready; }
        using ImageSource = std::function<bool(const std::string& url, std::string& bytes)>;
        void SetImageSource(ImageSource source);

        void Resize(int guiW, int guiH, int scale);
        bool SetDocument(const Scene& scene, std::string& error);
        bool SetDocument(const SceneDoc& doc, std::string& error) { return SetDocument(doc.scene, error); }
        bool SetOverlay(const Scene& scene, std::string& error);
        bool SetOverlay(const SceneDoc& doc, std::string& error) { return SetOverlay(doc.scene, error); }
        void ClearOverlay();
        bool SetPluginLayer(const Scene& scene, std::string& error);
        bool SetPluginLayer(const SceneDoc& doc, std::string& error) { return SetPluginLayer(doc.scene, error); }
        void ClearPluginLayer();
        bool SetServerLayer(const Scene& scene, std::string& error);
        bool SetServerLayer(const SceneDoc& doc, std::string& error) { return SetServerLayer(doc.scene, error); }
        void ClearServerLayer();
        bool HasPluginLayer() const;
        bool HasOverlay() const;
        void MoveOverlayElement(const char* id, int guiX, int guiY);

        void MouseMove(float guiX, float guiY);
        std::string OverlayHover();
        bool OverlayHoverClickSound(std::string& name, float& volume, float& pitch);
        std::string HoverLink();
        void Update();
        void Render();
        void ReloadTextures();
        void UnloadDocuments();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        bool ready = false;
    };
}
