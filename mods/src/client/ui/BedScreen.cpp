#include "client/ui/BedScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"

namespace perlin::ui {
    namespace {
        const char* kBedTemplate = R"PERLINRML(<rml>
<head>
<title>bed</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
.btn { display: block; position: absolute; width: 200px; height: 20px; background-color: #000000; z-index: 5; }
.btn div { display: block; position: absolute; }
.btn .rim { left: 1px; top: 1px; width: 197px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; width: 196px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; width: 198px; height: 2px; background-color: #565656; }
.btn .edge { left: 198px; top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; width: 200px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
</style>
</head>
<body>
  <div id="leave" class="btn" style="left: {{btn_x}}px; top: {{btn_y}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{label}}</div>
  </div>
</body>
</rml>
)PERLINRML";
        const ScreenTemplateDefault kBedDefault("bed_screen.rml", kBedTemplate);
    }

    std::string BuildBedScreenRml(const std::string& label, int guiW, int guiH) {
        std::string d = ScreenTemplate("bed_screen.rml", kBedTemplate);
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "btn_x", std::to_string(guiW / 2 - 100));
        ReplaceAll(d, "btn_y", std::to_string(guiH - 40));
        ReplaceAll(d, "label", EscapeRml(label));
        return d;
    }
}
