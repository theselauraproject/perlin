#pragma once
#include "client/ui/UiLayout.hpp"

#include <cstdint>
#include <functional>
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
        std::string id; // lets the game update the image without re-emitting
    };

    struct TextStyle {
        float scale = 1.0f;
        float r = 1, g = 1, b = 1, a = 1;
        bool shadow = false;
        bool outline = false;
        bool ten = false; // font_type: MinecraftTen
        std::string align = "left";
        float linePadding = 0;
    };

    class RmlWriter {
    public:
        explicit RmlWriter(bool comments) : comments(comments) {}

        void Image(float ax, float ay, float w, float h, int z, const std::string& texture,
                   const ImageStyle& style);
        void Item(float ax, float ay, float w, float h, int z, const std::string& itemKey, float alpha = 1.0f,
                  bool glint = false);
        void Rect(float ax, float ay, float w, float h, int z, float r, float g, float b, float a);
        void Text(float ax, float ay, float w, float h, int z, const std::vector<std::string>& lines,
                  const TextStyle& style);
        void Comment(const std::string& text);
        void Gradient(float ax, float ay, float w, float h, int z, bool vertical, const float c1[4],
                      const float c2[4]);
        void Link(float ax, float ay, float w, float h, int z, const std::string& command);

        // no stacking context: the box's children keep their absolute layer against the whole screen
        static constexpr int kAutoZ = INT32_MIN;
        void BeginBox(float ax, float ay, float w, float h, int z, const std::string& cls,
                      const std::string& id, bool clip, float scroll = 0.0f,
                      const std::string& extraAttrs = {});
        void EndBox();

        void BeginSection();
        void EndSection(const std::string& name);
        const std::unordered_map<std::string, std::string>& Sections() const { return sections; }

        const std::string& Body() const { return body; }
        size_t Elements() const { return count; }

    private:
        std::string body, saved;
        std::unordered_map<std::string, std::string> sections;
        bool comments;
        size_t count = 0;
        float ox = 0, oy = 0;
        std::vector<std::pair<float, float>> origins;
    };

    using CustomRenderer = std::function<void(const LNode&, RmlWriter&)>;

    class RmlEmitter {
    public:
        void Register(const std::string& name, CustomRenderer renderer) {
            renderers[name] = std::move(renderer);
        }
        std::string Emit(const LNode& root, float guiW, float guiH, const std::string& extraRcss,
                         bool comments);
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
        // edit_box content for a collection row: typed text, placeholder, and whether the box has focus
        void SetEditText(
            std::function<bool(const LNode&, std::string& text, std::string& placeholder, bool& focused)>
                fn) {
            editText = std::move(fn);
        }
        // slider tracks of the last emit by command ("k:3"): left edge and usable width, gui pixels
        const std::unordered_map<std::string, std::pair<float, float>>& SliderTracks() const {
            return sliderTracks;
        }

    private:
        void Visit(const LNode& n, RmlWriter& out);
        void EmitImage(const LNode& n, RmlWriter& out);
        void EmitLabel(const LNode& n, RmlWriter& out);
        void VisitButton(const LNode& n, RmlWriter& out);
        void VisitScroll(const LNode& n, RmlWriter& out);
        void VisitToggle(const LNode& n, RmlWriter& out);
        void VisitSlider(const LNode& n, RmlWriter& out);
        void VisitEditBox(const LNode& n, RmlWriter& out);
        std::function<bool(const LNode&, std::string&, std::string&, bool&)> editText;
        std::unordered_map<std::string, std::pair<float, float>> sliderTracks;
        // set while a slider or an edit box is being drawn: nested controls they style
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

    std::string EscapeRml(const std::string& text);
    std::string DefaultHudTemplate();
    std::string DefaultChatTemplate();
    std::unordered_map<std::string, std::string> ParseElementMap(const std::string& templateText);
}
