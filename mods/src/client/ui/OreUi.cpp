#include "client/ui/OreUi.hpp"

#include "client/ui/RmlEmit.hpp"

namespace perlin::ui::ore {
    namespace {
        std::string N(int v) {
            return std::to_string(v);
        }

        const char* KindClass(Kind k) {
            switch (k) {
            case Kind::Primary:
                return "op";
            case Kind::Secondary:
                return "os";
            case Kind::Destructive:
                return "od";
            case Kind::Neutral:
                break;
            }
            return "on";
        }
    }

    const std::string& Styles() {
        // per kind: outline, lip, face, highlight (top-left), edge (bottom-right); rest/hover/pressed
        static const std::string css = R"CSS(
.ob { display: block; position: absolute; z-index: 5; }
.ob div { display: block; position: absolute; }
.ob .ol { left: 0px; top: 0px; right: 0px; bottom: 0px; background-color: #1e1e1f; }
.ob .lp { left: 1px; right: 1px; bottom: 1px; height: 3px; }
.ob .fc { left: 1px; top: 1px; right: 1px; bottom: 3px; border-width: 1px; }
.ob .lb { left: 0px; right: 0px; top: 50%; margin-top: -6px; height: 9px; font-family: minecraft; font-size: 8px; line-height: 9px;
          text-align: center; white-space: nowrap; color: #ffffff; }
.ob.big .lb { margin-top: -10px; height: 18px; font-size: 16px; line-height: 18px; }
.ob .ic { left: 50%; top: 50%; width: 16px; height: 16px; margin-left: -8px; margin-top: -10px; }
.ob:active .fc { top: 3px; bottom: 1px; }
.ob:active .lp { display: none; }
.ob:active .lb { margin-top: -4px; }
.ob.big:active .lb { margin-top: -8px; }
.ob:active .ic { margin-top: -8px; }

.op .lp { background-color: #1d4d13; }
.op .fc { background-color: #3c8527; border-color: #639d52 #50913d #50913d #639d52; }
.op:hover .fc { background-color: #2a641c; border-color: #7fa277 #6a9260 #6a9260 #7fa277; }
.op:active .fc { background-color: #1d4d13; border-color: #4a7142 #345f2b #345f2b #4a7142; }

.on .lp { background-color: #313233; }
.on .fc { background-color: #48494a; border-color: #6d6d6e #5a5b5c #5a5b5c #6d6d6e; }
.on:hover .fc { background-color: #58585a; border-color: #9b9b9c #8a8a8b #8a8a8b #9b9b9c; }
.on:active .fc { background-color: #313233; border-color: #5a5b5c #464747 #464747 #5a5b5c; }

.os .ol { background-color: #000000; }
.os .lp { background-color: #58585a; }
.os .fc { background-color: #d0d1d4; border-color: #e3e3e5 #e3e3e5 #e3e3e5 #e3e3e5; }
.os .lb { color: #1e1e1f; }
.os:hover .fc { background-color: #b1b2b5; border-color: #e0e0e1 #e0e0e1 #e0e0e1 #e0e0e1; }
.os:active .fc { background-color: #b1b2b5; border-color: #c1c1c4 #b9babc #b9babc #c1c1c4; }

.od .lp { background-color: #ad1d1d; }
.od .fc { background-color: #ca3636; border-color: #d55e5e #cf4a4a #cf4a4a #d55e5e; }
.od:hover .fc { background-color: #c02d2d; border-color: #d98181 #d36c6c #d36c6c #d98181; }
.od:active .fc { background-color: #ad1d1d; border-color: #bd4a4a #b53434 #b53434 #bd4a4a; }

.ob.off .ol { background-color: #58585a; }
.ob.off .lp { background-color: #8c8d90; }
.ob.off .fc, .ob.off:hover .fc, .ob.off:active .fc { top: 1px; bottom: 3px; background-color: #b1b2b5; border-color: #b1b2b5; }
.ob.off .lb { color: #58585a; }

.ocard { display: block; position: absolute; background-color: #1e1e1f; }
.ocard div { display: block; position: absolute; }
.ocard .in { left: 1px; top: 1px; right: 1px; bottom: 1px; background-color: #313233; border-top-width: 1px; border-color: #48494a; }
.olbl { display: block; position: absolute; height: 9px; font-family: minecraft; font-size: 8px; line-height: 9px; color: #ffffff;
        white-space: nowrap; z-index: 6; }
.olbl.mut { color: #b1b2b5; }
.olbl.big { height: 18px; font-size: 16px; line-height: 18px; }
)CSS";
        return css;
    }

    std::string Button(const std::string& id, int x, int y, int w, int h, const std::string& label, Kind kind,
                       bool big, bool disabled) {
        std::string cls =
            std::string("ob ") + KindClass(kind) + (big ? " big" : "") + (disabled ? " off" : "");
        std::string out = "<div class=\"" + cls + "\"" +
                          (id.empty() || disabled ? std::string() : " id=\"" + EscapeRml(id) + "\"") +
                          " style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(w) +
                          "px;height:" + N(h) + "px;\">";
        out += "<div class=\"ol\"></div><div class=\"lp\"></div><div class=\"fc\"></div>";
        out += "<div class=\"lb\">" + EscapeRml(label) + "</div></div>\n";
        return out;
    }

    std::string IconButton(const std::string& id, int x, int y, int size, const std::string& iconSrc,
                           Kind kind) {
        std::string out = std::string("<div class=\"ob ") + KindClass(kind) + "\"" +
                          (id.empty() ? std::string() : " id=\"" + EscapeRml(id) + "\"") +
                          " style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(size) +
                          "px;height:" + N(size) + "px;\">";
        out += "<div class=\"ol\"></div><div class=\"lp\"></div><div class=\"fc\"></div>";
        out += "<mcimg class=\"ic\" src=\"" + EscapeRml(iconSrc) + "\"/></div>\n";
        return out;
    }

    std::string Card(int x, int y, int w, int h, int z) {
        return "<div class=\"ocard\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(w) +
               "px;height:" + N(h) + "px;z-index:" + N(z) + ";\"><div class=\"in\"></div></div>\n";
    }

    std::string Label(int x, int y, int w, const std::string& text, const char* align, bool muted, bool big) {
        return std::string("<div class=\"olbl") + (muted ? " mut" : "") + (big ? " big" : "") +
               "\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(w) + "px;text-align:" + align +
               ";\">" + EscapeRml(text) + "</div>\n";
    }
}
