#include <cstdlib>
#include "client/ui/RmlEmit.hpp"
#include "client/ui/FormatCodes.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/TenFont.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>

namespace perlin::ui {
    namespace {
        int Px(float v) {
            return static_cast<int>(std::ceil(v - 0.5f));
        }

        std::string Num(float v) {
            char buffer[32];
            if (std::fabs(v - std::round(v)) < 0.0005f)
                std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
            else
                std::snprintf(buffer, sizeof(buffer), "%.3f", v);
            return buffer;
        }

        std::string Hex(float r, float g, float b, float a) {
            const auto byte = [](float v) {
                return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255);
            };
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", byte(r), byte(g), byte(b), byte(a));
            return buffer;
        }

        std::string ActiveCodes(const std::string& text) {
            FormatState state;
            for (size_t i = 0; i < text.size(); i++) {
                if (!IsFormatCode(text, i))
                    continue;
                state.Apply(static_cast<unsigned char>(text[i + 2]));
                i += 2;
            }
            return state.Codes();
        }

        std::string FormatLine(const std::string& line, const TextStyle& style) {
            return FormatCodesToRml(line, style.a);
        }
    }

    std::string EscapeRml(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (const char c : text) {
            switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += c;
            }
        }
        return out;
    }

    void RmlWriter::Comment(const std::string& text) {
        if (!comments)
            return;
        body += "  <!-- " + text + " -->\n";
    }

    void RmlWriter::Image(float ax, float ay, float w, float h, int z, const std::string& texture,
                          const ImageStyle& st) {
        ax -= ox;
        ay -= oy;
        if (w <= 0 || h <= 0)
            return;
        count++;
        body += "  <mcimg src=\"" + EscapeRml(texture) + "\" style=\"left:" + std::to_string(Px(ax)) +
                "px;top:" + std::to_string(Px(ay)) + "px;width:" + Num(w) + "px;height:" + Num(h) +
                "px;z-index:" + std::to_string(z) + ";\"";
        if (st.u != 0 || st.v != 0 || st.uw >= 0 || st.vh >= 0)
            body += " uv=\"" + Num(st.u) + " " + Num(st.v) + " " + Num(st.uw) + " " + Num(st.vh) + "\"";
        if (st.hasNine)
            body += " nine=\"" + Num(st.nine[0]) + " " + Num(st.nine[1]) + " " + Num(st.nine[2]) + " " +
                    Num(st.nine[3]) + "\"";
        if (st.tiled)
            body += " tiled=\"" + std::to_string(st.tiled) + "\"";
        if (st.tileScale != 1.0f)
            body += " tilescale=\"" + Num(st.tileScale) + "\"";
        if (!st.clipDirection.empty() && st.clipRatio < 1.0f)
            body += " clip=\"" + st.clipDirection + " " + Num(st.clipRatio) + "\"";
        if (st.r != 1.0f || st.g != 1.0f || st.b != 1.0f)
            body += " color=\"" + Num(st.r) + " " + Num(st.g) + " " + Num(st.b) + "\"";
        if (st.alpha != 1.0f)
            body += " alpha=\"" + Num(st.alpha) + "\"";
        if (st.keepRatio)
            body += " keep=\"1\"";
        if (!st.id.empty())
            body += " id=\"" + EscapeRml(st.id) + "\"";
        body += "/>\n";
    }

    void RmlWriter::Gradient(float ax, float ay, float w, float h, int z, bool vertical, const float c1[4],
                             const float c2[4]) {
        ax -= ox;
        ay -= oy;
        if (w <= 0 || h <= 0)
            return;
        const auto hex = [](const float c[4]) {
            char buf[16];
            const auto q = [](float v) {
                return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            };
            std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", q(c[0]), q(c[1]), q(c[2]), q(c[3]));
            return std::string(buf);
        };
        count++;
        body += "  <div style=\"position:absolute;display:block;left:" + Num(ax) + "px;top:" + Num(ay) +
                "px;width:" + Num(w) + "px;height:" + Num(h) + "px;z-index:" + std::to_string(z) +
                ";decorator:" + (vertical ? "vertical" : "horizontal") + "-gradient(" + hex(c1) + " " +
                hex(c2) + ");\"></div>\n";
    }

    void RmlWriter::Link(float ax, float ay, float w, float h, int z, const std::string& command) {
        ax -= ox;
        ay -= oy;
        count++;
        body += "  <div class=\"link\" id=\"link:" + EscapeRml(command) + "\" style=\"left:" + Num(ax) +
                "px;top:" + Num(ay) + "px;width:" + Num(w) + "px;height:" + Num(h) +
                "px;z-index:" + std::to_string(z) + ";\"><div class=\"ul\"></div></div>\n";
    }

    void RmlWriter::BeginBox(float ax, float ay, float w, float h, int z, const std::string& cls,
                             const std::string& id, bool clip, float scroll, const std::string& extraAttrs) {
        count++;
        body += "  <div" + (cls.empty() ? std::string() : " class=\"" + cls + "\"") +
                (id.empty() ? std::string() : " id=\"" + EscapeRml(id) + "\"") + extraAttrs +
                " style=\"position:absolute;" + (cls == "hv" || cls == "df" ? "" : "display:block;") +
                "left:" + Num(ax - ox) + "px;top:" + Num(ay - oy) + "px;width:" + Num(w) +
                "px;height:" + Num(h) +
                "px;z-index:" + (z == kAutoZ ? std::string("auto") : std::to_string(z)) + ";" +
                (clip ? "overflow:hidden;clip:always;" : "") + "\">\n";
        origins.emplace_back(ox, oy);
        ox = ax;
        oy = ay + scroll;
    }

    void RmlWriter::EndBox() {
        if (origins.empty())
            return;
        body += "  </div>\n";
        ox = origins.back().first;
        oy = origins.back().second;
        origins.pop_back();
    }

    void RmlWriter::BeginSection() {
        saved = std::move(body);
        body.clear();
    }

    void RmlWriter::EndSection(const std::string& name) {
        sections[name] = std::move(body);
        body = std::move(saved);
        saved.clear();
    }

    void RmlWriter::Rect(float ax, float ay, float w, float h, int z, float r, float g, float b, float a) {
        ImageStyle st;
        st.r = r;
        st.g = g;
        st.b = b;
        st.alpha = a;
        Image(ax, ay, w, h, z, "mc:white", st);
    }

    void RmlWriter::Item(float ax, float ay, float w, float h, int z, const std::string& itemKey, float alpha,
                         bool glint) {
        ax -= ox;
        ay -= oy;
        count++;
        body += "  <mcitem key=\"" + EscapeRml(itemKey) + "\" style=\"left:" + std::to_string(Px(ax)) +
                "px;top:" + std::to_string(Px(ay)) + "px;width:" + Num(w) + "px;height:" + Num(h) +
                "px;z-index:" + std::to_string(z) + ";\"";
        if (alpha != 1.0f)
            body += " alpha=\"" + Num(alpha) + "\"";
        if (glint)
            body += " glint=\"1\"";
        body += "/>\n";
    }

    void RmlWriter::Text(float ax, float ay, float w, float h, int z, const std::vector<std::string>& lines,
                         const TextStyle& st) {
        if (lines.empty() || st.a <= 0.0f)
            return;
        ax -= ox;
        ay -= oy;
        count++;
        const float size = (st.ten ? TenFont::kEm : 8.0f) * st.scale;
        const float lineHeight =
            (st.ten ? TenFont::Instance().LineHeight(st.scale) : 9.0f * st.scale) + st.linePadding;
        std::string cls = st.ten ? "t ten" : "t";
        if (st.outline)
            cls += " o";
        else if (st.shadow)
            cls += " s";
        std::string family = st.ten ? "minecraft-ten" : "minecraft";
        if (st.outline)
            family += "-outline";
        else if (st.shadow)
            family += "-shadow";
        family += "-size-" + Num(size);
        body += "  <div class=\"" + cls + "\" style=\"font-family:" + family +
                ";left:" + std::to_string(Px(ax)) + "px;top:" + std::to_string(Px(ay)) +
                "px;width:" + Num(std::max(w, 1.0f)) + "px;height:" + Num(h) +
                "px;z-index:" + std::to_string(z) + ";font-size:" + Num(size) +
                "px;line-height:" + Num(lineHeight) + "px;text-align:" + st.align +
                ";color:" + Hex(st.r, st.g, st.b, st.a) + ";\">";
        std::string carry;
        for (size_t i = 0; i < lines.size(); i++) {
            if (i)
                body += "<br/>";
            const std::string line = carry + lines[i];
            body += FormatLine(line, st);
            carry = ActiveCodes(line);
        }
        body += "</div>\n";
    }

    namespace {
        bool ReadColor(const Json* v, float& r, float& g, float& b, float* a = nullptr) {
            if (v && v->IsString()) {
                static const std::pair<const char*, uint32_t> kNamed[] = {
                    {"white", 0xFFFFFF}, {"black", 0x000000},  {"red", 0xFF0000},    {"green", 0x00FF00},
                    {"blue", 0x0000FF},  {"yellow", 0xFFFF00}, {"cyan", 0x00FFFF},   {"magenta", 0xFF00FF},
                    {"grey", 0x808080},  {"gray", 0x808080},   {"orange", 0xFFA500}, {"purple", 0x800080},
                    {"pink", 0xFFC0CB},  {"brown", 0x8B4513},  {"nil", 0xFFFFFF}};
                std::string name;
                for (const char c : v->string)
                    name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                uint32_t rgb = 0;
                bool found = false;
                for (const auto& [key, value] : kNamed)
                    if (name == key) {
                        rgb = value;
                        found = true;
                    }
                if (!found && name.size() == 7 && name[0] == '#') {
                    char* end = nullptr;
                    rgb = static_cast<uint32_t>(std::strtoul(name.c_str() + 1, &end, 16));
                    found = end && *end == 0;
                }
                if (!found)
                    return false;
                r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
                g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
                b = static_cast<float>(rgb & 0xFF) / 255.0f;
                return true;
            }
            if (!v || !v->IsArray() || v->array.size() < 3)
                return false;
            r = static_cast<float>(v->array[0].NumberOr(1));
            g = static_cast<float>(v->array[1].NumberOr(1));
            b = static_cast<float>(v->array[2].NumberOr(1));
            if (a && v->array.size() >= 4)
                *a = static_cast<float>(v->array[3].NumberOr(1));
            return true;
        }
    }

    void RmlEmitter::EmitImage(const LNode& n, RmlWriter& out) {
        std::string texture = n.Str("texture");
        if (const Json* t = n.bag.Find("#texture"); t && t->IsString())
            texture = t->string;
        if (texture.empty() || texture[0] == '$' || texture[0] == '#')
            return;
        ImageStyle st;
        if (const Json* uv = n.Get("uv"); uv && uv->IsArray() && uv->array.size() >= 2) {
            st.u = static_cast<float>(uv->array[0].NumberOr(0));
            st.v = static_cast<float>(uv->array[1].NumberOr(0));
        }
        if (const Json* uv = n.Get("uv_size"); uv && uv->IsArray() && uv->array.size() >= 2) {
            st.uw = static_cast<float>(uv->array[0].NumberOr(-1));
            st.vh = static_cast<float>(uv->array[1].NumberOr(-1));
        }
        if (const Json* ns = n.Get("nineslice_size")) {
            if (ns->IsNumber()) {
                st.hasNine = true;
                for (float& v : st.nine)
                    v = static_cast<float>(ns->number);
            } else if (ns->IsArray() && ns->array.size() >= 4) {
                st.hasNine = true;
                for (int i = 0; i < 4; i++)
                    st.nine[i] = static_cast<float>(ns->array[static_cast<size_t>(i)].NumberOr(0));
            } else if (ns->IsArray() && ns->array.size() == 2) {
                st.hasNine = true;
                st.nine[0] = st.nine[2] = static_cast<float>(ns->array[0].NumberOr(0));
                st.nine[1] = st.nine[3] = static_cast<float>(ns->array[1].NumberOr(0));
            }
        }
        if (const Json* t = n.Get("tiled")) {
            if (t->IsBool() && t->boolean)
                st.tiled = 1;
            else if (t->IsString())
                st.tiled = t->string == "x" ? 2 : t->string == "y" ? 3 : (t->string == "true" ? 1 : 0);
        }
        st.tileScale = static_cast<float>(n.Num("tiled_scale", 1.0));
        st.clipDirection = n.Str("clip_direction", "left");
        float ratio = 1.0f;
        if (const Json* c = n.bag.Find("#clip_ratio"); c && c->IsNumber())
            ratio = static_cast<float>(c->number);
        else if (const Json* c2 = n.Get("clip_ratio"); c2 && c2->IsNumber())
            ratio = static_cast<float>(c2->number);
        st.clipRatio = std::clamp(ratio, 0.0f, 1.0f);
        ReadColor(n.Get("color"), st.r, st.g, st.b);
        st.alpha = n.alpha;
        st.keepRatio = n.Bool("keep_ratio", true) && !st.hasNine && !st.tiled;
        if (st.alpha <= 0.0f)
            return;
        out.Image(n.ax, n.ay, n.w, n.h, n.layer, texture, st);
    }

    void RmlEmitter::EmitLabel(const LNode& n, RmlWriter& out) {
        if (n.lines.empty())
            return;
        TextStyle st;
        float scale = static_cast<float>(n.Num("font_scale_factor", 1.0));
        const std::string size = n.Str("font_size", "normal");
        if (size == "small")
            scale *= 0.5f;
        else if (size == "large")
            scale *= 2.0f;
        else if (size == "extra_large")
            scale *= 4.0f;
        st.scale = scale;
        float a = 1.0f;
        ReadColor(n.Get("color"), st.r, st.g, st.b, &a);
        st.a = a * n.alpha;
        st.shadow = n.Bool("shadow", false);
        st.outline = n.Bool("perlin_text_outline", false);
        st.align = n.Str("text_alignment", "left");
        st.linePadding = static_cast<float>(n.Num("line_padding", 0));
        if (IsTenFontType(n.Str("font_type"))) {
            TenFont::Instance().EnsureLoaded();
            st.ten = TenFont::Instance().Available();
        }
        out.Text(n.ax, n.ay, n.w, n.h, n.layer, n.lines, st);
    }

    namespace {
        std::string ButtonCommand(const LNode& n) {
            const Json* maps = n.Get("button_mappings");
            if (!maps || !maps->IsArray())
                return {};
            for (const Json& m : maps->array) {
                if (!m.IsObject())
                    continue;
                const Json* to = m.Find("to_button_id");
                if (!to || !to->IsString())
                    continue;
                const std::string& id = to->string;
                if (id == "button.form_button_click")
                    return "fb:" + std::to_string(std::max(0, n.index));
                if (id == "button.submit_custom_form")
                    return "submit";
                if (id == "popup_dialog.left_button")
                    return "fb:0";
                if (id == "popup_dialog.rightcancel_button")
                    return "fb:1";
                if (id == "button.perlin_form_toggle")
                    return "t:" + std::to_string(std::max(0, n.index));
                if (id == "button.perlin_form_dropdown")
                    return "d:" + std::to_string(std::max(0, n.index));
                if (id == "button.perlin_form_input")
                    return "i:" + std::to_string(std::max(0, n.index));
                if (id == "button.perlin_form_minus")
                    return "s:" + std::to_string(std::max(0, n.index)) + ":-";
                if (id == "button.perlin_form_plus")
                    return "s:" + std::to_string(std::max(0, n.index)) + ":+";
                if (id == "button.menu_exit" || id == "button.menu_cancel" || id == "button.exit_dialog" ||
                    id.contains("close"))
                    return "close";
            }
            for (const Json& m : maps->array) {
                const Json* to = m.IsObject() ? m.Find("to_button_id") : nullptr;
                if (!to || !to->IsString() || to->string != "button.container_take_all_place_all" ||
                    n.index < 0)
                    continue;
                if (n.collection == "container_items")
                    return "k" + std::to_string(n.index);
                if (n.collection == "inventory_items")
                    return "s" + std::to_string(9 + n.index);
                if (n.collection == "hotbar_items")
                    return "s" + std::to_string(n.index);
            }
            for (const Json& m : maps->array) {
                const Json* to = m.IsObject() ? m.Find("to_button_id") : nullptr;
                if (!to || !to->IsString() || to->string.empty() || to->string[0] == '$')
                    continue;
                const Json* from = m.Find("from_button_id");
                const Json* type = m.Find("mapping_type");
                if (type && type->IsString() && type->string == "focused" && from && from->IsString() &&
                    from->string != "button.menu_select" && from->string != "button.menu_ok")
                    continue;
                return "jb:" + to->string + (n.index >= 0 ? ":" + std::to_string(n.index) : std::string());
            }
            return {};
        }

        const LNode* FindKid(const LNode& n, const std::string& name) {
            if (name.empty())
                return nullptr;
            for (const auto& kid : n.kids)
                if (kid->id == name)
                    return kid.get();
            return nullptr;
        }
    }

    namespace {
        int TopLayer(const LNode& n) {
            int z = n.layer;
            for (const auto& kid : n.kids)
                if (kid->visible)
                    z = std::max(z, TopLayer(*kid));
            return z;
        }
    }

    namespace {
        // sound_name after inheritance: common.button and toggles declare random.click, container slot buttons none
        std::string SoundAttrs(const LNode& n) {
            const Json* sn = n.Get("sound_name");
            if (!sn || !sn->IsString())
                return {};
            return " snd=\"" + EscapeRml(sn->string) + "\" vol=\"" + Num(static_cast<float>(n.Num("sound_volume", 1.0))) +
                   "\" pit=\"" + Num(static_cast<float>(n.Num("sound_pitch", 1.0))) + "\"";
        }
    }

    void RmlEmitter::VisitButton(const LNode& n, RmlWriter& out) {
        const std::string command = ButtonCommand(n);
        const std::string defName = n.Str("default_control"), hoverName = n.Str("hover_control"),
                          pressedName = n.Str("pressed_control"), lockedName = n.Str("locked_control");
        const bool hasHover = FindKid(n, hoverName) != nullptr && (!command.empty() || inSlot);
        const std::string soundAttrs = SoundAttrs(n);
        out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, command.empty() && !inSlot ? "" : "bt", command,
                     false, 0.0f,
                     command.empty() && !inSlot ? std::string() : soundAttrs);
        for (const auto& kid : n.kids) {
            if (!kid->visible)
                continue;
            if (kid->id == defName) {
                out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, hasHover ? "df" : "", "", false);
                Visit(*kid, out);
                out.EndBox();
            } else if (kid->id == hoverName) {
                if (hasHover) {
                    out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, "hv", "", false);
                    Visit(*kid, out);
                    out.EndBox();
                }
            } else if (kid->id == pressedName || kid->id == lockedName) {
                continue;
            } else {
                Visit(*kid, out);
            }
        }
        out.EndBox();
    }

    namespace {
        std::string WidgetCommand(const LNode& n) {
            if (n.index < 0)
                return {};
            const std::string row = std::to_string(n.index);
            if (n.type == "toggle")
                return (n.Str("toggle_name") == "custom_dropdown_radio_toggle" ? "r:" : "t:") + row;
            if (n.type == "dropdown")
                return "d:" + row;
            if (n.type == "slider")
                return "k:" + row;
            if (n.type == "edit_box")
                return "i:" + row;
            return {};
        }

        void Shift(const LNode& n, float dx) {
            LNode& m = const_cast<LNode&>(n);
            m.ax += dx;
            for (const auto& kid : m.kids)
                Shift(*kid, dx);
        }
    }

    void RmlEmitter::VisitToggle(const LNode& n, RmlWriter& out) {
        bool on = n.Bool("toggle_default_state", false);
        if (const Json* st = n.bag.Find("#toggle_state"); st && st->IsBool())
            on = st->boolean;
        const std::string rest = n.Str(on ? "checked_control" : "unchecked_control");
        const std::string hover = n.Str(on ? "checked_hover_control" : "unchecked_hover_control");
        std::vector<std::string> states;
        for (const char* key :
             {"checked_control", "unchecked_control", "checked_hover_control", "unchecked_hover_control",
              "checked_locked_control", "unchecked_locked_control", "checked_locked_hover_control",
              "unchecked_locked_hover_control"})
            if (std::string name = n.Str(key); !name.empty())
                states.push_back(std::move(name));
        const std::string command = WidgetCommand(n);
        const bool hasHover = !hover.empty() && FindKid(n, hover) != nullptr && !command.empty();
        out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, command.empty() ? "" : "bt", command, false,
                     0.0f, command.empty() ? std::string() : SoundAttrs(n));
        for (const auto& kid : n.kids) {
            if (!kid->visible)
                continue;
            if (!rest.empty() && kid->id == rest) {
                out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, hasHover ? "df" : "", "", false);
                Visit(*kid, out);
                out.EndBox();
            } else if (kid->id == hover) {
                if (hasHover) {
                    out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, "hv", "", false);
                    Visit(*kid, out);
                    out.EndBox();
                }
            } else if (std::find(states.begin(), states.end(), kid->id) == states.end()) {
                Visit(*kid, out);
            }
        }
        out.EndBox();
    }

    void RmlEmitter::VisitSlider(const LNode& n, RmlWriter& out) {
        double steps = 1, value = 0;
        if (const Json* st = n.bag.Find("#slider_steps"); st && st->IsNumber())
            steps = st->number;
        if (const Json* v = n.bag.Find("#slider_value"); v && v->IsNumber())
            value = v->number;
        const float f = static_cast<float>(std::clamp(steps > 1 ? value / (steps - 1) : value, 0.0, 1.0));
        const std::string boxName = n.Str("slider_box_control", "slider_box");
        const LNode* box = FindKid(n, boxName);
        const float boxW = box ? box->w : 0.0f;
        const std::string command = WidgetCommand(n);
        if (!command.empty())
            sliderTracks[command] = {n.ax + boxW * 0.5f, std::max(1.0f, n.w - boxW)};
        progressName = n.Str("progress_control", "slider_progress");
        progressHoverName = n.Str("progress_hover_control", "slider_progress_hover");
        progressLeft = n.ax;
        progressWidth = boxW * 0.5f + f * (n.w - boxW);
        out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, command.empty() ? "" : "bt", command, false);
        const std::string barHover = n.Str("hover_control");
        for (const auto& kid : n.kids)
            if (kid->visible && kid->id != barHover && kid->id != boxName)
                Visit(*kid, out);
        for (const auto& kid : n.kids) {
            if (!kid->visible || kid->id != boxName)
                continue;
            const float dx = n.ax + f * (n.w - boxW) - kid->ax;
            Shift(*kid, dx);
            const LNode* rest = FindKid(*kid, kid->Str("default_control", "default"));
            const LNode* hover = FindKid(*kid, kid->Str("hover_control", "hover"));
            if (rest) {
                out.BeginBox(kid->ax, kid->ay, kid->w, kid->h, RmlWriter::kAutoZ,
                             hover && !command.empty() ? "df" : "", "", false);
                Visit(*rest, out);
                out.EndBox();
            }
            if (hover && !command.empty()) {
                out.BeginBox(kid->ax, kid->ay, kid->w, kid->h, RmlWriter::kAutoZ, "hv", "", false);
                Visit(*hover, out);
                out.EndBox();
            }
            Shift(*kid, -dx);
        }
        out.EndBox();
        progressWidth = -1;
    }

    void RmlEmitter::VisitEditBox(const LNode& n, RmlWriter& out) {
        std::string text, placeholder;
        bool focused = false;
        if (editText)
            editText(n, text, placeholder, focused);
        editTextName = n.Str("text_control", "display_text");
        editShown = focused               ? text + "_"
                    : !text.empty()       ? text
                    : placeholder.empty() ? std::string()
                                          : "\xC2\xA7"
                                            "7" +
                                                placeholder;
        const std::string command = WidgetCommand(n);
        const std::string rest = n.Str("default_control", "default"), hover = n.Str("hover_control", "hover");
        const std::string pressed = n.Str("pressed_control", "pressed"),
                          locked = n.Str("locked_control", "locked");
        const bool hasHover = FindKid(n, hover) != nullptr && !command.empty();
        out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, command.empty() ? "" : "bt", command, false);
        for (const auto& kid : n.kids) {
            if (!kid->visible || kid->id == locked)
                continue;
            const bool shownRest = focused ? kid->id == pressed : kid->id == rest;
            if (shownRest) {
                out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, hasHover && !focused ? "df" : "", "",
                             false);
                Visit(*kid, out);
                out.EndBox();
            } else if (kid->id == hover) {
                if (hasHover && !focused) {
                    out.BeginBox(n.ax, n.ay, n.w, n.h, RmlWriter::kAutoZ, "hv", "", false);
                    Visit(*kid, out);
                    out.EndBox();
                }
            } else if (kid->id != rest && kid->id != pressed) {
                Visit(*kid, out);
            }
        }
        out.EndBox();
        editTextName.clear();
    }

    void RmlEmitter::VisitScroll(const LNode& n, RmlWriter& out) {
        float content = 0;
        for (const auto& kid : n.kids)
            if (kid->id == contentName || content == 0)
                content = std::max(content, kid->h);
        contentHeight = content;
        viewportHeight = n.h;
        const float maxScroll = std::max(0.0f, content - n.h);
        const float s = std::clamp(scroll, 0.0f, maxScroll);
        out.BeginBox(n.ax, n.ay, n.w, n.h, TopLayer(n), "", "", true, s);
        for (const auto& kid : n.kids)
            Visit(*kid, out);
        out.EndBox();
    }

    void RmlEmitter::Visit(const LNode& n, RmlWriter& out) {
        if (!n.visible)
            return;
        if (slotNamer && !inSlot) {
            if (const std::string slot = slotNamer(n); !slot.empty()) {
                out.BeginBox(n.ax, n.ay, n.w, n.h, TopLayer(n), "slot", slot, false);
                inSlot = true;
                Visit(n, out);
                inSlot = false;
                out.EndBox();
                return;
            }
        }
        if (!elements.empty() && grouped != &n) {
            if (const auto it = elements.find(n.id); it != elements.end()) {
                std::string id = it->second;
                if (n.index >= 0 && !n.collection.empty())
                    id += "-" + std::to_string(n.index);
                int z = n.layer;
                const std::function<void(const LNode&)> deepest = [&](const LNode& k) {
                    z = std::max(z, k.layer);
                    for (const auto& kid : k.kids)
                        deepest(*kid);
                };
                deepest(n);
                out.BeginBox(n.ax, n.ay, n.w, n.h, z, "el " + it->second, id, false);
                const LNode* saved = grouped;
                grouped = &n;
                Visit(n, out);
                grouped = saved;
                out.EndBox();
                return;
            }
        }
        if (n.type == "button") {
            VisitButton(n, out);
            return;
        }
        if (n.type == "toggle" || n.type == "dropdown") {
            VisitToggle(n, out);
            return;
        }
        if (n.type == "slider") {
            VisitSlider(n, out);
            return;
        }
        if (n.type == "edit_box") {
            VisitEditBox(n, out);
            return;
        }
        if (progressWidth >= 0 && (n.id == progressName || n.id == progressHoverName)) {
            out.BeginBox(n.ax, n.ay, std::max(0.0f, progressWidth - (n.ax - progressLeft)), n.h, TopLayer(n),
                         "", "", true);
            const float saved = progressWidth;
            progressWidth = -1;
            Visit(n, out);
            progressWidth = saved;
            out.EndBox();
            return;
        }
        if (!editTextName.empty() && n.type == "label" &&
            (n.id == editTextName || n.id == "$text_edit_text_control")) {
            if (editShown.empty())
                return;
            LNode& shown = const_cast<LNode&>(n);
            const std::vector<std::string> saved = shown.lines;
            shown.lines = {editShown};
            EmitLabel(shown, out);
            shown.lines = saved;
            return;
        }
        if (n.type == "scroll_view") {
            viewportName = n.Str("scrolling_view_port", "scrolling_view_port");
            contentName = n.Str("scrolling_content", "scrolling_content");
        } else if (!viewportName.empty() && n.id == viewportName) {
            VisitScroll(n, out);
            return;
        }
        if (n.type == "image") {
            EmitImage(n, out);
        } else if (n.type == "label") {
            EmitLabel(n, out);
        } else if (n.type == "custom" && n.Str("renderer") == "gradient_renderer") {
            float c1[4] = {1, 1, 1, 1}, c2[4] = {1, 1, 1, 1};
            ReadColor(n.Get("color1"), c1[0], c1[1], c1[2], &c1[3]);
            ReadColor(n.Get("color2"), c2[0], c2[1], c2[2], &c2[3]);
            c1[3] *= n.alpha;
            c2[3] *= n.alpha;
            out.Gradient(n.ax, n.ay, n.w, n.h, n.layer,
                         n.Str("gradient_direction", "vertical") != "horizontal", c1, c2);
        } else if (n.type == "custom") {
            const std::string name = n.Str("renderer");
            auto it = renderers.find(name);
            if (it != renderers.end()) {
                it->second(n, out);
            } else if (std::find(unknown.begin(), unknown.end(), name) == unknown.end()) {
                unknown.push_back(name);
            }
        }
        const bool clipFlag = n.Bool("clips_children", false) && n.type != "button" && !n.kids.empty();
        if (clipFlag && (n.w <= 0 || n.h <= 0))
            return;
        const bool clips = clipFlag;
        if (clips)
            out.BeginBox(n.ax, n.ay, n.w, n.h, TopLayer(n), "", "", true);
        for (const auto& kid : n.kids)
            Visit(*kid, out);
        if (clips)
            out.EndBox();
    }

    std::unordered_map<std::string, std::string> ParseElementMap(const std::string& templateText) {
        std::unordered_map<std::string, std::string> map;
        const size_t open = templateText.find("<!--elements");
        if (open == std::string::npos)
            return map;
        const size_t close = templateText.find("-->", open);
        const std::string block = templateText.substr(
            open + 12, close == std::string::npos ? std::string::npos : close - open - 12);
        size_t i = 0;
        while (i < block.size()) {
            while (i < block.size() && std::isspace(static_cast<unsigned char>(block[i])))
                i++;
            const size_t start = i;
            while (i < block.size() && !std::isspace(static_cast<unsigned char>(block[i])))
                i++;
            const std::string token = block.substr(start, i - start);
            if (const size_t eq = token.find('='); eq != std::string::npos && eq > 0 && eq + 1 < token.size())
                map[token.substr(0, eq)] = token.substr(eq + 1);
        }
        return map;
    }

    std::string DefaultHudTemplate() {
        return R"PERLINRML(<rml>
<head>
<title>hud</title>
<!--elements
    hotbar_grid=hotbar
    gui_hotbar_grid_item=hotbar-slot
    item_icon=hotbar-item
    hotbar_start_cap=hotbar-start-cap
    hotbar_end_cap=hotbar-end-cap
    heart_rend=hearts
    horse_heart_rend=horse-hearts
    armor_rend=armor
    hunger_rend=hunger
    bubbles_rend=air
    exp_rend=xp-bar
    item_text=item-name
    hud_actionbar_text=actionbar
    hud_title_text=title
    subtitle_panel=subtitles
    mob_effects_renderer=effects
    boss_health_panel=bossbars
    sidebar=scoreboard
-->
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
mcimg, mcitem { display: block; position: absolute; }
.t { display: block; position: absolute; font-family: minecraft; white-space: pre; overflow: visible; }
.t.s { font-family: minecraft-shadow; }
.t.o { font-family: minecraft-outline; }
.t.ten { font-family: minecraft-ten; }
.t.ten.s { font-family: minecraft-ten-shadow; }
.t.ten.o { font-family: minecraft-ten-outline; }

/* ---- HUD elements ----------------------------------------------------------------------------------------------
   Every part of the HUD is its own element, named by the list in <!--elements--> above (JSON UI control = element id;
   add any control name from hud_screen.json there to give it an element too). The game places each one; adjust it here:
     transform: translate(10px, -4px) scale(1.5);   move / resize (added to where the game puts it)
     opacity: 0.5;   display: none;                 fade / hide
   Slots and other repeated parts get their index: #hotbar-slot-0 .. #hotbar-slot-8, and share a class (.hotbar-slot).
   Chat has its own file, chat_screen.rml. */
#hotbar { }
.hotbar-slot { }
#hotbar-slot-0 { }
#hotbar-slot-1 { }
#hotbar-slot-2 { }
#hotbar-slot-3 { }
#hotbar-slot-4 { }
#hotbar-slot-5 { }
#hotbar-slot-6 { }
#hotbar-slot-7 { }
#hotbar-slot-8 { }
.hotbar-item { }
#hotbar-start-cap { }
#hotbar-end-cap { }
#hearts { }
#horse-hearts { }
#armor { }
#hunger { }
#air { }
#xp-bar { }
#item-name { }
#actionbar { }
#title { }
#subtitles { }
#effects { }
#bossbars { }
#scoreboard { }
{{chat_style}}
{{rcss}}
</style>
</head>
<body>
{{hud}}
{{chat}}
</body>
</rml>
)PERLINRML";
    }

    std::string DefaultChatTemplate() {
        return R"PERLINRML(<rml>
<head>
<title>chat</title>
<style>
/* The chat. Lines are generated (newest at the bottom) inside #chat-lines; change {{chat_top}} to move the whole block
   (it sits above the hearts / armor row by default). #chat-input is the bar shown while typing. */
#chat-lines { display: block; position: absolute; left: 0px; top: {{chat_top}}px; width: {{chat_w}}px; height: 180px; z-index: 30; }
.link { display: block; position: absolute; }
.link .ul { display: none; position: absolute; left: 0px; bottom: 0px; width: 100%; height: 1px; background-color: #ffff55; }
.link:hover .ul { display: block; }
#chat-scroll { display: block; position: absolute; left: 0px; top: {{chat_top}}px; width: 2px; height: 180px; z-index: 31; }
#chat-input { display: block; position: absolute; left: 2px; top: {{input_top}}px; width: {{input_w}}px; height: 12px; z-index: 32; }
</style>
</head>
<body>
  <div id="chat-lines">{{lines}}</div>
  <div id="chat-scroll">{{scrollbar}}</div>
  <div id="chat-input">{{input}}</div>
</body>
</rml>
)PERLINRML";
    }

    namespace {
        void Replace(std::string& text, const std::string& key, const std::string& value) {
            const std::string needle = "{{" + key + "}}";
            for (size_t at = text.find(needle); at != std::string::npos;
                 at = text.find(needle, at + value.size()))
                text.replace(at, needle.size(), value);
        }
        std::string Between(const std::string& doc, const std::string& tag) {
            const size_t open = doc.find("<" + tag);
            if (open == std::string::npos)
                return {};
            const size_t start = doc.find('>', open);
            const size_t end = doc.find("</" + tag + ">", start == std::string::npos ? open : start);
            if (start == std::string::npos || end == std::string::npos)
                return {};
            return doc.substr(start + 1, end - start - 1);
        }
    }

    std::string RmlEmitter::Emit(const LNode& root, float guiW, float guiH, const std::string& extraRcss,
                                 bool comments) {
        sliderTracks.clear();
        RmlWriter writer(comments);
        unknown.clear();
        viewportName.clear();
        contentName.clear();
        contentHeight = viewportHeight = 0;
        Visit(root, writer);

        std::string chatStyle, chatBody;
        const auto& sec = writer.Sections();
        if (sec.contains("chat_lines")) {
            std::string chat = chatTemplate.empty() ? DefaultChatTemplate() : chatTemplate;
            chatStyle = Between(chat, "style");
            chatBody = Between(chat, "body");
            const auto get = [&](const char* name) {
                const auto it = sec.find(name);
                return it == sec.end() ? std::string() : it->second;
            };
            for (std::string* part : {&chatStyle, &chatBody}) {
                Replace(*part, "w", Num(guiW));
                Replace(*part, "h", Num(guiH));
                Replace(*part, "chat_top", Num(guiH - 60.0f - 180.0f));
                Replace(*part, "chat_w", Num(328.0f));
                Replace(*part, "input_top", Num(guiH - 14.0f));
                Replace(*part, "input_w", Num(guiW - 4.0f));
            }
            Replace(chatBody, "lines", get("chat_lines"));
            Replace(chatBody, "scrollbar", get("chat_scroll"));
            Replace(chatBody, "input", get("chat_input"));
        }
        std::string doc = hudTemplate.empty() ? DefaultHudTemplate() : hudTemplate;
        Replace(doc, "w", Num(guiW));
        Replace(doc, "h", Num(guiH));
        Replace(doc, "chat_style", chatStyle);
        Replace(doc, "rcss", extraRcss);
        Replace(doc, "chat", chatBody);
        Replace(doc, "hud", writer.Body());
        static const char* kEngineCss =
            "\nmcimg, mcitem { display: block; position: absolute; }\n"
            ".t { display: block; position: absolute; font-family: minecraft; white-space: pre; overflow: "
            "visible; }\n"
            ".t.s { font-family: minecraft-shadow; }\n.t.o { font-family: minecraft-outline; }\n"
            ".t.ten { font-family: minecraft-ten; }\n.t.ten.s { font-family: minecraft-ten-shadow; }\n"
            ".t.ten.o { font-family: minecraft-ten-outline; }\n"
            ".link { display: block; position: absolute; }\n"
            ".link .ul { display: none; position: absolute; left: 0px; bottom: 0px; width: 100%; height: "
            "1px; background-color: #ffff55; }\n"
            ".link:hover .ul { display: block; }\n"
            ".df { display: block; }\n.bt .hv { display: none; }\n.bt:hover .hv { display: block; "
            "}\n.bt:hover .df { display: none; }\n"
            "body * { pointer-events: none; }\n.bt, .bt *, .link, .link * { pointer-events: auto; }\n";
        if (const size_t at = doc.find("<style>"); at != std::string::npos)
            doc.insert(at + 7, kEngineCss);
        return doc;
    }
}
