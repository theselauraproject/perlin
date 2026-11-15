#pragma once
#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/Scene.hpp"
#include "client/ui/UiLayout.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::ui {
    struct ImageStyle {
        float u = 0, v = 0;
        float uw = -1, vh = -1;
        bool hasNine = false;
        float nine[4] = {0, 0, 0, 0};
        int tiled = 0;
        float tileScale = 1.0f;
        std::string clipDirection;
        float clipRatio = 1.0f;
        float r = 1, g = 1, b = 1, alpha = 1;
        bool keepRatio = false;
        std::string id;
    };

    struct TextStyle {
        float scale = 1.0f;
        float r = 1, g = 1, b = 1, a = 1;
        bool shadow = false;
        bool outline = false;
        bool ten = false;
        std::string align = "left";
        float linePadding = 0;
    };

    struct BoxSound {
        std::string name, volume, pitch;
    };

    class SceneWriter {
    public:
        SceneWriter() : mainBuilder(main) {}
        SceneWriter(const SceneWriter&) = delete;
        SceneWriter& operator=(const SceneWriter&) = delete;

        void Image(float ax, float ay, float w, float h, int z, const std::string& texture,
                   const ImageStyle& style);
        void Item(float ax, float ay, float w, float h, int z, const std::string& itemKey, float alpha = 1.0f,
                  bool glint = false);
        void Rect(float ax, float ay, float w, float h, int z, float r, float g, float b, float a);
        void Text(float ax, float ay, float w, float h, int z, const std::vector<std::string>& lines,
                  const TextStyle& style);
        void Gradient(float ax, float ay, float w, float h, int z, bool vertical, const float c1[4],
                      const float c2[4]);
        void Link(float ax, float ay, float w, float h, int z, const std::string& command);

        static constexpr int kAutoZ = INT32_MIN;
        void BeginBox(float ax, float ay, float w, float h, int z, const std::string& cls,
                      const std::string& id, bool clip, float scroll = 0.0f, const BoxSound& sound = {});
        void EndBox();

        void BeginSection();
        void EndSection(const std::string& name);
        std::map<std::string, std::vector<SceneNode>>& Sections() { return sections; }

        std::vector<SceneNode>& Nodes() { return main; }
        size_t Elements() const { return count; }

    private:
        SceneBuilder& Builder() { return sectionBuilder ? *sectionBuilder : mainBuilder; }

        std::vector<SceneNode> main;
        SceneBuilder mainBuilder;
        std::vector<SceneNode> pending;
        std::unique_ptr<SceneBuilder> sectionBuilder;
        std::map<std::string, std::vector<SceneNode>> sections;
        size_t count = 0;
        float ox = 0, oy = 0;
        std::vector<std::pair<float, float>> origins;
    };

    using CustomRenderer = std::function<void(const LNode&, SceneWriter&)>;

    class SceneEmitter {
    public:
        void Register(const std::string& name, CustomRenderer renderer) {
            renderers[name] = std::move(renderer);
        }
        SceneDoc Emit(const LNode& root, float guiW, float guiH, const std::string& extraRcss);
        void SetTemplates(std::string hud, std::string chat) {
            hudTemplate = std::move(hud);
            chatTemplate = std::move(chat);
        }
        const std::vector<std::string>& UnknownRenderers() const { return unknown; }
        void SetSlotNamer(std::function<std::string(const LNode&)> namer) { slotNamer = std::move(namer); }
        void SetElements(std::unordered_map<std::string, std::string> map) { elements = std::move(map); }
        void SetScroll(float value) { scroll = value; }
        float ContentHeight() const { return contentHeight; }
        float ViewportHeight() const { return viewportHeight; }

        void SetEditText(
            std::function<bool(const LNode&, std::string& text, std::string& placeholder, bool& focused)>
                fn) {
            editText = std::move(fn);
        }

        const std::unordered_map<std::string, std::pair<float, float>>& SliderTracks() const {
            return sliderTracks;
        }

    private:
        void Visit(const LNode& n, SceneWriter& out);
        void EmitImage(const LNode& n, SceneWriter& out);
        void EmitLabel(const LNode& n, SceneWriter& out);
        void VisitButton(const LNode& n, SceneWriter& out);
        void VisitScroll(const LNode& n, SceneWriter& out);
        void VisitToggle(const LNode& n, SceneWriter& out);
        void VisitSlider(const LNode& n, SceneWriter& out);
        void VisitEditBox(const LNode& n, SceneWriter& out);
        std::function<bool(const LNode&, std::string&, std::string&, bool&)> editText;
        std::unordered_map<std::string, std::pair<float, float>> sliderTracks;

        std::string progressName, progressHoverName;
        float progressWidth = -1, progressLeft = 0;
        std::string editTextName, editShown;
        float scroll = 0, contentHeight = 0, viewportHeight = 0;
        std::string viewportName, contentName;
        std::function<std::string(const LNode&)> slotNamer;
        bool inSlot = false;
        std::unordered_map<std::string, std::string> elements;
        const LNode* grouped = nullptr;

        std::unordered_map<std::string, CustomRenderer> renderers;
        std::string hudTemplate, chatTemplate;
        std::vector<std::string> unknown;
    };

    std::string DefaultHudTemplate();
    std::string DefaultChatTemplate();
    std::unordered_map<std::string, std::string> ParseElementMap(const std::string& templateText);
}
