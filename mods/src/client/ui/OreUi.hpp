#pragma once
#include <string>

// our drawing of bedrock's oreui look: flat faces, 1-texel dark outline, lighter top-left edge; elevated controls
// get a 2-texel lip gone while pressed; 1 texel = 1 gui pixel; game's oreui colours; plain rml boxes, no game artwork
namespace perlin::ui::ore {
    enum class Kind { Primary, Neutral, Secondary, Destructive };

    // stylesheet for everything below; goes inside the document's <style>
    const std::string& Styles();

    // elevated oreui button; `id` is what OverlayHover() reports, empty = inert;
    // `big` uses the heading size (16 px) for the label, like the game's primary actions
    std::string Button(const std::string& id, int x, int y, int w, int h, const std::string& label, Kind kind,
                       bool big = false, bool disabled = false);
    // square elevated button holding a 16x16 icon (any src mcimg understands)
    std::string IconButton(const std::string& id, int x, int y, int size, const std::string& iconSrc,
                           Kind kind);
    // dark rounded-corner oreui card (panels behind character previews and dialogs)
    std::string Card(int x, int y, int w, int h, int z = 2);
    // single-line text in oreui body style; `muted` uses the secondary text colour
    std::string Label(int x, int y, int w, const std::string& text, const char* align = "left",
                      bool muted = false, bool big = false);
}
