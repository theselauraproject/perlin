#include "client/ui/JavaWidgets.hpp"

#include <regex>

namespace perlin::ui {
    namespace {
        const char* kSprites =
            "<mcimg class=\"jw jw-n\" src=\"mcres:ui_menu_btn.png\" nine=\"3 3 3 3\"/>"
            "<mcimg class=\"jw jw-h\" src=\"mcres:ui_menu_btn_hi.png\" nine=\"3 3 3 3\"/>"
            "<mcimg class=\"jw jw-d\" src=\"mcres:ui_menu_btn_off.png\" nine=\"3 3 3 3\"/>";

        const char* kStyle =
            ".btn, .btn:hover, .btn.off, .btn.off:hover, .btn.track, .btn.track:hover { background-color: "
            "transparent; }\n"
            ".jw { display: block; position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; }\n"
            ".jw-h, .jw-d { display: none; }\n"
            ".btn:hover .jw-h { display: block; }\n"
            ".btn:hover .jw-n { display: none; }\n"
            // disabled buttons and slider tracks: dark sprite, even hovered for tracks
            ".btn.off .jw-d, .btn.track .jw-d { display: block; }\n"
            ".btn.off .jw-n, .btn.off .jw-h, .btn.track .jw-n, .btn.track .jw-h { display: none; }\n"
            // slider knob: normal button, lit with its track
            ".btn.track .knob .jw-n { display: block; }\n"
            ".btn.track .knob .jw-d, .btn.track .knob .jw-h { display: none; }\n"
            ".btn.track:hover .knob .jw-h { display: block; }\n"
            ".btn.track:hover .knob .jw-n { display: none; }\n";
    }

    std::string WithJavaWidgets(const std::string& rml) {
        if (!rml.contains("class=\"rim\"") && !rml.contains("class=\"krim\""))
            return rml;
        static const std::regex button(
            R"(<div class="rim"[^>]*></div>\s*<div class="face"[^>]*></div>\s*(<div class="shade"[^>]*></div>)?\s*(<div class="edge"[^>]*></div>)?)");
        static const std::regex knob(
            R"(<div class="krim"[^>]*></div>\s*<div class="kface"[^>]*></div>\s*(<div class="kshade"[^>]*></div>)?)");
        std::string out = std::regex_replace(rml, button, kSprites);
        out = std::regex_replace(out, knob, kSprites);
        if (const size_t style = out.find("</style>"); style != std::string::npos)
            out.insert(style, kStyle);
        return out;
    }
}
