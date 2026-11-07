#include "client/ui/FormScreen.hpp"

#include "client/ui/FormatCodes.hpp"
#include "client/ui/Json.hpp"
#include "client/resources/TexturePack.hpp"
#include "client/ui/Hud.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/HudData.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include "network/Http.hpp"
#include "util/Log.hpp"

namespace perlin::ui {
    namespace {
        std::string Str(const Json* j) {
            return j && j->IsString() ? j->string : std::string();
        }

        std::string Num(double v) {
            char buf[48];
            if (v == std::floor(v) && std::fabs(v) < 1e12)
                std::snprintf(buf, sizeof(buf), "%.1f", v);
            else
                std::snprintf(buf, sizeof(buf), "%.6g", v);
            return buf;
        }

        std::string JsonString(const std::string& s) {
            Json j(s);
            return j.Dump();
        }

        std::string F(float v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
            return buf;
        }

        int LineCount(const std::string& text, int perLine) {
            int lines = 0;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                if (end == std::string::npos)
                    end = text.size();
                const std::string line = StripFormatCodes(text.substr(start, end - start));
                lines += std::max(1, static_cast<int>((line.size() + perLine - 1) / std::max(1, perLine)));
                start = end + 1;
            }
            return lines;
        }

        std::string Rich(const std::string& text) {
            std::string out = FormatCodesToRml(text);
            std::string result;
            for (size_t i = 0; i < out.size(); i++) {
                if (out[i] == '\n')
                    result += "<br/>";
                else
                    result += out[i];
            }
            return result;
        }

        const char* kStyle =
            "body { position: absolute; left: 0px; top: 0px; margin: 0; padding: 0; background: transparent; "
            "}\n"
            "div { display: block; position: absolute; }\n"
            "#bg { left: 0px; top: 0px; background-color: rgba(0,0,0,150); z-index: 1; }\n"
            "#panel { background-color: rgba(16,16,16,235); z-index: 2; }\n"
            "#clip { overflow: hidden; z-index: 3; }\n"
            ".t { font-family: minecraft-shadow; white-space: normal; color: #ffffff; font-size: 8px; "
            "line-height: 9px; }\n"
            ".c { text-align: center; }\n"
            ".btn { background-color: #000000; z-index: 5; }\n"
            ".btn div { display: block; position: absolute; }\n"
            ".btn .rim { left: 1px; top: 1px; background-color: #aaaaaa; }\n"
            ".btn .face { left: 2px; top: 2px; background-color: #6f6f6f; }\n"
            ".btn .shade { left: 1px; background-color: #565656; }\n"
            ".btn .edge { top: 1px; background-color: #565656; }\n"
            ".btn .lbl { font-family: minecraft-shadow; left: 0px; font-size: 8px; line-height: 9px; "
            "text-align: center; color: #ffffff; white-space: nowrap; overflow: hidden; }\n"
            ".btn:hover { background-color: #ffffff; }\n"
            ".btn:hover .rim { background-color: #afafaf; }\n"
            ".btn:hover .face { background-color: #757575; }\n"
            ".field { background-color: #ffffff; z-index: 5; }\n"
            ".field .in { left: 1px; top: 1px; background-color: #000000; }\n"
            ".field.focus .in { background-color: #101030; }\n"
            ".field .txt { font-family: minecraft-shadow; left: 4px; top: 5px; font-size: 8px; line-height: "
            "9px; white-space: nowrap; overflow: hidden; color: #e0e0e0; }\n";

        const std::string kBuiltinFormHead =
            std::string(
                "<rml>\n<head>\n<title>form</title>\n<style>\nbody { width: {{w}}px; height: {{h}}px; }\n") +
            kStyle + "</style>\n</head>\n<body>\n";
        const ScreenTemplateDefault kBuiltinFormDefault("server_form_builtin.rml", kBuiltinFormHead);

        std::mutex gImageMutex;
        std::map<std::string, std::string> gImages;
        std::set<std::string> gImagePending;
        std::atomic<uint32_t> gImageVersion{0};
        uint64_t gImageEpoch = 0;

        // tab stops have no defined advance in the bitmap font: long runs push labels out of view,
        // so server menus treat them as plain word separators
        void NormalizeFormText(std::string& text) {
            for (char& c : text)
                if (c == '\t')
                    c = '\n';
            std::string collapsed;
            collapsed.reserve(text.size());
            bool lastBreak = true;
            for (const char c : text) {
                if (c == '\n') {
                    if (lastBreak)
                        continue;
                    lastBreak = true;
                    collapsed += c;
                    continue;
                }
                if (c == ' ') {
                    collapsed += c;
                    lastBreak = false;
                    continue;
                }
                lastBreak = false;
                collapsed += c;
            }
            while (!collapsed.empty() && collapsed.back() == '\n')
                collapsed.pop_back();
            text = std::move(collapsed);
        }

        std::string ImageSource(const FormButton& b) {
            if (b.imageData.empty())
                return {};
            if (b.imageType == "path") {
                std::string path = b.imageData;
                if (path.size() > 4 && path.compare(path.size() - 4, 4, ".png") == 0)
                    path.resize(path.size() - 4);
                return path;
            }
            if (b.imageType == "url") {
                std::string bytes;
                if (FormImageBytes(b.imageData, bytes))
                    return "mcurl:" + b.imageData;
            }
            return {};
        }

        float ButtonHeight(const std::string& label, float w, float h) {
            std::string shown = label;
            NormalizeFormText(shown);
            const int perLine = std::max(8, static_cast<int>(w / 5.2f));
            const int lines = std::max(1, LineCount(shown, perLine));
            return std::max(h, static_cast<float>(lines * 9 + 8));
        }

        std::string Button(const std::string& id, const std::string& label, float x, float y, float w,
                           float h) {
            // fallback-only display copy: tabs have no advance here, wrap them into lines
            std::string shown = label;
            NormalizeFormText(shown);
            const int perLine = std::max(8, static_cast<int>(w / 5.2f));
            const int lines = std::max(1, LineCount(shown, perLine));
            const float hh = ButtonHeight(label, w, h);
            const std::string W = F(w), H = F(hh);
            std::string s = "<div id=\"" + EscapeRml(id) + "\" class=\"btn\" style=\"left:" + F(x) +
                            "px;top:" + F(y) + "px;width:" + W + "px;height:" + H + "px;\">";
            s += "<div class=\"rim\" style=\"width:" + F(w - 3) + "px;height:" + F(hh - 4) + "px;\"></div>";
            s += "<div class=\"face\" style=\"width:" + F(w - 4) + "px;height:" + F(hh - 5) + "px;\"></div>";
            s += "<div class=\"shade\" style=\"top:" + F(hh - 3) + "px;width:" + F(w - 2) +
                 "px;height:2px;\"></div>";
            s += "<div class=\"edge\" style=\"left:" + F(w - 2) + "px;width:1px;height:" + F(hh - 4) +
                 "px;\"></div>";
            s += "<div class=\"lbl\" style=\"top:" + F((hh - lines * 9) / 2) + "px;width:" + W +
                 "px;height:" + F(static_cast<float>(lines * 9)) + "px;\">" + FormatCodesToRmlLines(shown) +
                 "</div></div>\n";
            return s;
        }
    }

    bool FormImageBytes(const std::string& url, std::string& out) {
        uint64_t epoch;
        {
            std::lock_guard lock(gImageMutex);
            const auto it = gImages.find(url);
            if (it != gImages.end()) {
                if (it->second.empty())
                    return false;
                out = it->second;
                return true;
            }
            if (!gImagePending.insert(url).second)
                return false;
            epoch = gImageEpoch;
        }
        if (!url.starts_with("http")) {
            std::lock_guard lock(gImageMutex);
            if (epoch == gImageEpoch)
                gImages[url] = {};
            return false;
        }
        std::thread([url, epoch]() {
            std::string body;
            try {
                const http::Response r = http::GetUrl(url, {}, 20);
                if (r.status >= 200 && r.status < 300 && r.body.size() < (8u << 20))
                    body = r.body;
            } catch (...) {}
            {
                std::lock_guard lock(gImageMutex);
                // a download from the previous level cannot repopulate the cache
                if (epoch != gImageEpoch)
                    return;
                gImages[url] = std::move(body);
                gImageVersion++;
            }
        }).detach();
        return false;
    }

    uint32_t FormImageVersion() {
        return gImageVersion.load();
    }

    int FormSliderAt(const std::string& hoverId) {
        if (hoverId.size() < 4 || hoverId[0] != 'k' || hoverId[1] != ':')
            return -1;
        return std::atoi(hoverId.c_str() + 2);
    }

    bool FormDrag(FormModel& form, int index, float guiX) {
        if (index < 0 || static_cast<size_t>(index) >= form.elements.size())
            return false;
        FormElement& e = form.elements[static_cast<size_t>(index)];
        if (e.trackW <= 1.0f)
            return false;
        const double f = std::clamp(static_cast<double>((guiX - e.trackX) / e.trackW), 0.0, 1.0);
        if (e.kind == FormElement::Kind::Slider) {
            const double span = e.max - e.min;
            double v = e.min + f * span;
            v = e.min + std::round((v - e.min) / e.step) * e.step;
            v = std::clamp(v, e.min, e.max);
            if (v == e.number)
                return false;
            e.number = v;
            return true;
        }
        if (e.kind == FormElement::Kind::StepSlider) {
            const int last = static_cast<int>(e.options.size()) - 1;
            const int i = std::clamp(static_cast<int>(std::lround(f * last)), 0, std::max(0, last));
            if (i == e.index)
                return false;
            e.index = i;
            return true;
        }
        return false;
    }

    bool ParseForm(uint32_t id, const std::string& json, FormModel& out) {
        Json root;
        std::string error;
        if (!Json::Parse(json, root, error) || !root.IsObject())
            return false;
        out = FormModel{};
        out.id = id;
        out.title = Str(root.Find("title"));
        const std::string type = Str(root.Find("type"));
        if (type == "modal") {
            out.type = FormModel::Type::Modal;
            out.content = Str(root.Find("content"));
            out.button1 = Str(root.Find("button1"));
            out.button2 = Str(root.Find("button2"));
            return true;
        }
        if (type == "custom_form") {
            out.type = FormModel::Type::Custom;
            out.submit = Str(root.Find("submit"));
            const Json* content = root.Find("content");
            if (content && content->IsArray()) {
                for (const Json& e : content->array) {
                    if (!e.IsObject())
                        continue;
                    FormElement el;
                    const std::string kind = Str(e.Find("type"));
                    el.text = Str(e.Find("text"));
                    if (kind == "label") {
                        el.kind = FormElement::Kind::Label;
                    } else if (kind == "header") {
                        el.kind = FormElement::Kind::Header;
                    } else if (kind == "divider") {
                        el.kind = FormElement::Kind::Divider;
                    } else if (kind == "input") {
                        el.kind = FormElement::Kind::Input;
                        el.placeholder = Str(e.Find("placeholder"));
                        el.value = Str(e.Find("default"));
                    } else if (kind == "toggle") {
                        el.kind = FormElement::Kind::Toggle;
                        if (const Json* d = e.Find("default"))
                            el.flag = d->BoolOr(false);
                    } else if (kind == "slider") {
                        el.kind = FormElement::Kind::Slider;
                        el.min = e.Find("min") ? e.Find("min")->NumberOr(0) : 0;
                        el.max = e.Find("max") ? e.Find("max")->NumberOr(1) : 1;
                        el.step = e.Find("step") ? e.Find("step")->NumberOr(1) : 1;
                        if (el.step <= 0)
                            el.step = 1;
                        el.number = e.Find("default") ? e.Find("default")->NumberOr(el.min) : el.min;
                    } else if (kind == "step_slider" || kind == "dropdown") {
                        el.kind =
                            kind == "dropdown" ? FormElement::Kind::Dropdown : FormElement::Kind::StepSlider;
                        const Json* list = e.Find(kind == "dropdown" ? "options" : "steps");
                        if (list && list->IsArray())
                            for (const Json& o : list->array)
                                el.options.push_back(o.IsString() ? o.string : std::string());
                        if (const Json* d = e.Find("default"))
                            el.index = static_cast<int>(d->NumberOr(0));
                        if (el.options.empty())
                            el.options.push_back("");
                        el.index = std::clamp(el.index, 0, static_cast<int>(el.options.size()) - 1);
                    } else {
                        continue;
                    }
                    out.elements.push_back(std::move(el));
                }
            }
            return true;
        }
        out.type = FormModel::Type::Simple;
        out.content = Str(root.Find("content"));
        // classic simple forms send "buttons"; newer format sends "elements" (buttons mixed with labels, headers, dividers)
        const Json* buttons = root.Find("buttons");
        if (!buttons || !buttons->IsArray())
            buttons = root.Find("elements");
        if (buttons && buttons->IsArray())
            for (const Json& b : buttons->array) {
                FormButton fb;
                fb.text = Str(b.Find("text"));
                fb.kind = Str(b.Find("type"));
                if (const Json* img = b.Find("image")) {
                    fb.imageType = Str(img->Find("type"));
                    fb.imageData = Str(img->Find("data"));
                }
                out.buttons.push_back(std::move(fb));
            }
        return true;
    }

    std::string BuildFormRml(const FormModel& form, int guiW, int guiH, float scroll, float& contentHeight,
                             float& viewHeight) {
        const float panelW = static_cast<float>(std::min(guiW - 16, 260));
        const float inner = panelW - 20;
        const int perLine = std::max(8, static_cast<int>(inner / 5.2f));
        const float panelX = (guiW - panelW) / 2.0f;
        const float viewTop = 30.0f;
        viewHeight = static_cast<float>(std::max(60, guiH - 60));
        std::string body;
        float y = 0.0f;
        const auto text = [&](const std::string& t, bool center) {
            std::string shown = t;
            NormalizeFormText(shown);
            const int lines = LineCount(shown, perLine);
            const float h = static_cast<float>(lines * 9);
            body += "<div class=\"t" + std::string(center ? " c" : "") + "\" style=\"left:10px;top:" + F(y) +
                    "px;width:" + F(inner) + "px;height:" + F(h) + "px;\">" + FormatCodesToRmlLines(shown) +
                    "</div>\n";
            y += h + 4.0f;
        };
        if (!form.content.empty() && form.type != FormModel::Type::Custom)
            text(form.content, false);
        if (form.type == FormModel::Type::Simple) {
            for (size_t i = 0; i < form.buttons.size(); i++) {
                const std::string src = ImageSource(form.buttons[i]);
                float h = src.empty() ? 20.0f : 32.0f;
                h = ButtonHeight(form.buttons[i].text, inner, h);
                std::string b = Button("b:" + std::to_string(i), form.buttons[i].text, 10, y, inner, h);
                if (!src.empty()) {
                    const std::string img =
                        "<mcimg src=\"" + EscapeRml(src) +
                        "\" style=\"left:4px;top:4px;width:24px;height:24px;z-index:8;\"/>";
                    const size_t closeAt = b.rfind("</div>");
                    if (closeAt != std::string::npos)
                        b.insert(closeAt, img);
                }
                body += b;
                y += h + 4.0f;
            }
        } else if (form.type == FormModel::Type::Modal) {
            const float half = (inner - 4) / 2;
            body += Button("m:1", form.button1, 10, y, half, 20);
            body += Button("m:2", form.button2, 10 + half + 4, y, half, 20);
            y += 24.0f;
        } else {
            for (size_t i = 0; i < form.elements.size(); i++) {
                const FormElement& e = form.elements[i];
                const std::string n = std::to_string(i);
                switch (e.kind) {
                case FormElement::Kind::Label:
                    text(e.text, false);
                    break;
                case FormElement::Kind::Header:
                    text(e.text, true);
                    break;
                case FormElement::Kind::Divider:
                    body += "<div style=\"left:10px;top:" + F(y + 2) + "px;width:" + F(inner) +
                            "px;height:1px;background-color:#707070;\"></div>\n";
                    y += 6.0f;
                    break;
                case FormElement::Kind::Input: {
                    if (!e.text.empty())
                        text(e.text, false);
                    const bool focus = form.focus == static_cast<int>(i);
                    const std::string shown = e.value.empty() && !focus ? "\xC2\xA7"
                                                                          "8" +
                                                                              e.placeholder
                                              : focus ? TextFieldPlain(e.value, true)
                                                      : e.value;
                    body += "<div id=\"i:" + n + "\" class=\"field" + (focus ? " focus" : "") +
                            "\" style=\"left:10px;top:" + F(y) + "px;width:" + F(inner) +
                            "px;height:20px;\">";
                    body += "<div class=\"in\" style=\"width:" + F(inner - 2) + "px;height:18px;\"></div>";
                    body += "<div class=\"txt\" style=\"width:" + F(inner - 8) + "px;height:9px;\">" +
                            Rich(shown) + "</div></div>\n";
                    y += 24.0f;
                    break;
                }
                case FormElement::Kind::Toggle:
                    body += Button("t:" + n,
                                   e.text + ": " +
                                       (e.flag ? "\xC2\xA7"
                                                 "aON"
                                               : "\xC2\xA7"
                                                 "cOFF"),
                                   10, y, inner, 20);
                    y += 24.0f;
                    break;
                case FormElement::Kind::Dropdown:
                    body += Button("d:" + n, e.text + ": " + e.options[static_cast<size_t>(e.index)], 10, y,
                                   inner, 20);
                    y += 24.0f;
                    break;
                case FormElement::Kind::Slider:
                case FormElement::Kind::StepSlider: {
                    const bool plain = e.kind == FormElement::Kind::Slider;
                    double frac = 0.0;
                    if (plain)
                        frac = e.max > e.min ? (e.number - e.min) / (e.max - e.min) : 0.0;
                    else
                        frac = e.options.size() > 1
                                   ? static_cast<double>(e.index) / static_cast<double>(e.options.size() - 1)
                                   : 0.0;
                    frac = std::clamp(frac, 0.0, 1.0);
                    e.trackX = panelX + 10.0f + 4.0f;
                    e.trackW = inner - 8.0f;
                    std::string track = Button(
                        "k:" + n,
                        e.text + ": " + (plain ? Num(e.number) : e.options[static_cast<size_t>(e.index)]), 10,
                        y, inner, 20);
                    const float knobX = 10.0f + 1.0f + static_cast<float>(frac) * (inner - 10.0f);
                    const std::string knob =
                        "<div style=\"left:" + F(knobX - 10.0f) +
                        "px;top:0px;width:8px;height:20px;background-color:#000000;z-index:7;\">"
                        "<div "
                        "style=\"left:1px;top:1px;width:6px;height:18px;background-color:#c6c6c6;\"></div>"
                        "<div "
                        "style=\"left:1px;top:1px;width:5px;height:17px;background-color:#ffffff;\"></div>"
                        "<div "
                        "style=\"left:2px;top:2px;width:4px;height:16px;background-color:#8b8b8b;\"></div></"
                        "div>";
                    const size_t closeAt = track.rfind("</div>");
                    if (closeAt != std::string::npos)
                        track.insert(closeAt, knob);
                    body += track;
                    y += 24.0f;
                    break;
                }
                }
            }
            body += Button("submit", form.submit.empty() ? "Submit" : form.submit, 10, y, inner, 20);
            y += 24.0f;
        }
        contentHeight = y + 6.0f;
        const float maxScroll = std::max(0.0f, contentHeight - viewHeight);
        const float s = std::clamp(scroll, 0.0f, maxScroll);
        const float visible = std::min(contentHeight, viewHeight);

        std::string out = ScreenTemplate("server_form_builtin.rml", kBuiltinFormHead);
        ReplaceAll(out, "w", std::to_string(guiW));
        ReplaceAll(out, "h", std::to_string(guiH));
        out += "<div id=\"bg\" style=\"width:" + std::to_string(guiW) + "px;height:" + std::to_string(guiH) +
               "px;\"></div>\n";
        out += "<div id=\"panel\" style=\"left:" + F(panelX) + "px;top:" + F(viewTop - 18) +
               "px;width:" + F(panelW) + "px;height:" + F(visible + 24) + "px;\"></div>\n";
        out += "<div class=\"t c\" style=\"z-index:4;left:" + F(panelX) + "px;top:" + F(viewTop - 13) +
               "px;width:" + F(panelW) + "px;height:9px;\">" + Rich(form.title) + "</div>\n";
        out += Button(kFormCloseId, "x", panelX + panelW - 14, viewTop - 16, 12, 12);
        out += "<div id=\"clip\" style=\"left:" + F(panelX) + "px;top:" + F(viewTop) +
               "px;width:" + F(panelW) + "px;height:" + F(visible) + "px;\">\n";
        out += "<div style=\"left:0px;top:" + F(-s) + "px;width:" + F(panelW) +
               "px;height:" + F(contentHeight) + "px;\">\n" + body + "</div>\n</div>\n";
        out += "</body>\n</rml>\n";
        return out;
    }

    std::string FormCustomResponse(const FormModel& form) {
        std::string out = "[";
        for (size_t i = 0; i < form.elements.size(); i++) {
            const FormElement& e = form.elements[i];
            if (i)
                out += ",";
            switch (e.kind) {
            case FormElement::Kind::Input:
                out += JsonString(e.value);
                break;
            case FormElement::Kind::Toggle:
                out += e.flag ? "true" : "false";
                break;
            case FormElement::Kind::Slider:
                out += Num(e.number);
                break;
            case FormElement::Kind::StepSlider:
            case FormElement::Kind::Dropdown:
                out += std::to_string(e.index);
                break;
            default:
                out += "null";
                break;
            }
        }
        out += "]";
        return out;
    }

    bool FormDismissDropdown(FormModel& form, const std::string& hoverId) {
        if (form.openDropdown < 0 || hoverId.starts_with("r:") || hoverId.starts_with("d:"))
            return false;
        form.openDropdown = -1;
        return true;
    }

    bool FormClick(FormModel& form, const std::string& id, std::string& response) {
        if (id.size() < 2)
            return false;
        if (id.starts_with("fb:")) {
            const int index = std::atoi(id.c_str() + 3);
            if (form.type == FormModel::Type::Modal) {
                response = index == 0 ? "true" : "false";
                return true;
            }
            // server numbers only buttons; labels, headers, dividers between them don't count
            int buttonIndex = 0;
            for (int i = 0; i < index && i < static_cast<int>(form.buttons.size()); i++) {
                const std::string& k = form.buttons[static_cast<size_t>(i)].kind;
                if (k.empty() || k == "button")
                    buttonIndex++;
            }
            response = std::to_string(buttonIndex);
            return true;
        }
        const char kind = id[0];
        if (id == "submit") {
            response = FormCustomResponse(form);
            return true;
        }
        if (id[1] != ':')
            return false;
        const std::string rest = id.substr(2);
        const size_t colon = rest.find(':');
        const int index = std::atoi(rest.substr(0, colon).c_str());
        if (kind == 'b') {
            response = std::to_string(index);
            return true;
        }
        if (kind == 'm') {
            response = index == 1 ? "true" : "false";
            return true;
        }
        if (kind == 'r') {
            const int owner = form.openDropdown, option = index;
            if (owner >= 0 && static_cast<size_t>(owner) < form.elements.size() &&
                option < static_cast<int>(form.elements[static_cast<size_t>(owner)].options.size()))
                form.elements[static_cast<size_t>(owner)].index = option;
            form.openDropdown = -1;
            return false;
        }
        if (index < 0 || static_cast<size_t>(index) >= form.elements.size())
            return false;
        FormElement& e = form.elements[static_cast<size_t>(index)];
        if (kind == 'i') {
            form.focus = index;
        } else if (kind == 't') {
            e.flag = !e.flag;
        } else if (kind == 'd') {
            form.openDropdown = form.openDropdown == index ? -1 : index;
        } else if (kind == 's') {
            const bool plus = colon != std::string::npos && rest[colon + 1] == '+';
            if (e.kind == FormElement::Kind::Slider)
                e.number = std::clamp(e.number + (plus ? e.step : -e.step), e.min, e.max);
            else
                e.index = std::clamp(e.index + (plus ? 1 : -1), 0, static_cast<int>(e.options.size()) - 1);
        }
        return false;
    }

    bool FormEdit(FormModel& form, const InputManager::Typed& t) {
        if (form.focus < 0 || static_cast<size_t>(form.focus) >= form.elements.size())
            return false;
        FormElement& e = form.elements[static_cast<size_t>(form.focus)];
        if (e.kind != FormElement::Kind::Input)
            return false;
        TextLimits limits;
        limits.maxBytes = 500;
        return TextEdit::Focused().Apply(t, e.value, limits);
    }

    namespace {
        std::string ElementRowText(const FormElement& e, bool focused) {
            switch (e.kind) {
            case FormElement::Kind::Toggle:
                return e.text + ": " +
                       (e.flag ? "\xC2\xA7"
                                 "aON"
                               : "\xC2\xA7"
                                 "cOFF");
            case FormElement::Kind::Dropdown:
                return e.text + ": " + e.options[static_cast<size_t>(e.index)];
            case FormElement::Kind::Slider:
                return e.text + ": " + Num(e.number);
            case FormElement::Kind::StepSlider:
                return e.text + ": " + e.options[static_cast<size_t>(e.index)];
            case FormElement::Kind::Input: {
                const std::string shown = e.value.empty() && !focused ? "\xC2\xA7"
                                                                        "8" +
                                                                            e.placeholder
                                          : focused ? TextFieldPlain(e.value, true)
                                                    : e.value;
                return e.text.empty() ? shown : e.text + ": " + shown;
            }
            default:
                return e.text;
            }
        }

        const char* ElementFactoryId(FormElement::Kind kind) {
            switch (kind) {
            case FormElement::Kind::Label:
                return "label";
            case FormElement::Kind::Header:
                return "header";
            case FormElement::Kind::Divider:
                return "divider";
            case FormElement::Kind::Input:
                return "input";
            case FormElement::Kind::Toggle:
                return "toggle";
            case FormElement::Kind::Slider:
                return "slider";
            case FormElement::Kind::StepSlider:
                return "step_slider";
            case FormElement::Kind::Dropdown:
                return "dropdown";
            }
            return "label";
        }

        class FormData final : public UiData {
        public:
            explicit FormData(const FormModel& f) : form(f) {
                if (form.type == FormModel::Type::Modal) {
                    FormButton a, b;
                    a.text = form.button1;
                    b.text = form.button2;
                    items.push_back(a);
                    items.push_back(b);
                } else {
                    items = form.buttons;
                }
            }

            bool Global(const std::string& name, Json& out) const override {
                if (name == "#form_button_length") {
                    out = Json(static_cast<double>(items.size()));
                    return true;
                }
                if (name == "#title_text") {
                    out = Json(form.title);
                    return true;
                }
                if (name == "#form_text") {
                    out = Json(form.content);
                    return true;
                }
                if (name == "#custom_form_length") {
                    out = Json(static_cast<double>(form.elements.size()));
                    return true;
                }
                if (name == "#submit_text") {
                    out = Json(form.submit.empty() ? std::string("Submit") : form.submit);
                    return true;
                }
                if (name == "#submit_button_visible") {
                    out = Json(true);
                    return true;
                }
                if (name == "#is_using_gamepad") {
                    out = Json(false);
                    return true;
                }
                if (name == "#close_button_visible") {
                    out = Json(true);
                    return true;
                }
                if (name == "#form_button_contents") {
                    out = Json(static_cast<double>(items.size()));
                    return true;
                }
                if (form.type == FormModel::Type::Modal) {
                    if (name == "#modal_title_text") {
                        out = Json(form.title);
                        return true;
                    }
                    if (name == "#modal_label_text") {
                        out = Json(form.content);
                        return true;
                    }
                    if (name == "#modal_left_button_text") {
                        out = Json(form.button1);
                        return true;
                    }
                    if (name == "#modal_rightcancel_button_text") {
                        out = Json(form.button2);
                        return true;
                    }
                    if (name == "#modal_middle_button_text") {
                        out = Json(std::string());
                        return true;
                    }
                }
                return false;
            }

            int CollectionSize(const std::string& collection) const override {
                if (collection == "form_buttons")
                    return static_cast<int>(items.size());
                if (collection == "custom_form")
                    return static_cast<int>(form.elements.size());
                return 0;
            }

            bool CollectionValue(const std::string& collection, int index, const std::string& binding,
                                 Json& out) const override {
                if (collection == "custom_form") {
                    if (index < 0 || static_cast<size_t>(index) >= form.elements.size())
                        return false;
                    const FormElement& e = form.elements[static_cast<size_t>(index)];
                    if (binding == "#custom_text") {
                        out = Json(e.text);
                    } else if (binding == "#custom_row_text") {
                        out = Json(ElementRowText(e, form.focus == index));
                    } else if (binding == "#custom_placeholder_text") {
                        out = Json(e.placeholder);
                    } else if (binding == "#custom_input_text") {
                        out = Json(e.value);
                    } else if (binding == "#custom_toggle_state") {
                        out = Json(e.flag);
                    } else if (binding == "#custom_dropdown") {
                        out = Json(form.openDropdown == index);
                    } else if (binding == "#custom_slider_value") {
                        // the slider control reads 0..1
                        const double span = e.max - e.min;
                        out = Json(span > 0 ? std::clamp((e.number - e.min) / span, 0.0, 1.0) : 0.0);
                    } else if (binding == "#custom_slider_step_value") {
                        out = Json(static_cast<double>(e.index));
                    } else if (binding == "#custom_slider_steps") {
                        out = Json(static_cast<double>(std::max<size_t>(e.options.size(), 1)));
                    } else if (binding == "#custom_slider_text" || binding == "#custom_slider_step_text" ||
                               binding == "#custom_slider_text_value" ||
                               binding == "#custom_slider_step_text_value") {
                        out = Json(ElementRowText(e, false));
                    } else if (binding == "#custom_tooltip_text") {
                        out = Json(std::string());
                    } else if (binding == "#custom_input_enabled" || binding == "#custom_toggle_enabled" ||
                               binding == "#custom_slider_enabled") {
                        out = Json(true);
                    } else if (binding == "#dropdown_option_text") {
                        out =
                            Json(e.options.empty() ? std::string() : e.options[static_cast<size_t>(e.index)]);
                    } else if (binding == "#custom_dropdown_length") {
                        out = Json(static_cast<double>(form.openDropdown == index ? e.options.size() : 0));
                    } else {
                        return false;
                    }
                    return true;
                }
                // only the open dropdown lists options, so its rows are this collection
                if (collection == "custom_dropdown") {
                    const int owner = form.openDropdown, option = index;
                    if (owner < 0 || static_cast<size_t>(owner) >= form.elements.size())
                        return false;
                    const FormElement& e = form.elements[static_cast<size_t>(owner)];
                    if (option < 0 || option >= static_cast<int>(e.options.size()))
                        return false;
                    if (binding == "#custom_radio_text")
                        out = Json(e.options[static_cast<size_t>(option)]);
                    else if (binding == "#custom_radio_toggled")
                        out = Json(option == e.index);
                    else
                        return false;
                    return true;
                }
                if (collection != "form_buttons" || index < 0 || static_cast<size_t>(index) >= items.size())
                    return false;
                const FormButton& b = items[static_cast<size_t>(index)];
                if (binding == "#form_button_text") {
                    out = Json(b.text);
                    return true;
                }
                if (binding == "#form_button_texture") {
                    // web images report "loading" until downloaded, as the game does; packs show a spinner
                    if (b.imageType == "url") {
                        std::string bytes;
                        out = Json(FormImageBytes(b.imageData, bytes)
                                       ? (bytes.empty() ? std::string() : "mcurl:" + b.imageData)
                                       : std::string("loading"));
                    } else {
                        out = Json(b.imageType == "path" ? b.imageData : std::string());
                    }
                    return true;
                }
                if (binding == "#form_button_texture_file_system") {
                    out = Json(std::string(b.imageType == "url"    ? "Url"
                                           : b.imageType == "path" ? "InUserPackage"
                                                                   : ""));
                    return true;
                }
                if (binding == "#form_button_contents") {
                    out = Json(static_cast<double>(items.size()));
                    return true;
                }
                return false;
            }

            std::vector<FactoryItem> Factory(const std::string& name) const override {
                std::vector<FactoryItem> result;
                if (name == "server_form_factory") {
                    result.push_back(
                        {form.type == FormModel::Type::Custom ? "custom_form" : "long_form", {}, {}, -1});
                } else if (name == "buttons" && form.type == FormModel::Type::Custom) {
                    for (size_t i = 0; i < form.elements.size(); i++)
                        result.push_back({ElementFactoryId(form.elements[i].kind),
                                          {},
                                          "custom_form",
                                          static_cast<int>(i)});
                } else if (name == "buttons") {
                    for (size_t i = 0; i < items.size(); i++) {
                        const std::string& k = items[i].kind;
                        const std::string id = k == "label" || k == "header" || k == "divider" ? k : "button";
                        result.push_back({id, {}, "form_buttons", static_cast<int>(i)});
                    }
                }
                return result;
            }

        private:
            const FormModel& form;
            std::vector<FormButton> items;
        };

        const char* kFormTemplate =
            "<rml>\n<head>\n<title>form</title>\n<style>\n"
            "body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; "
            "padding: 0; background: transparent; }\n"
            "mcimg, mcitem { display: block; position: absolute; }\n"
            "{{chat_style}}{{rcss}}\n</style>\n</head>\n<body>\n{{chat}}{{hud}}</body>\n</rml>\n";
        const ScreenTemplateDefault kFormTemplateDefault("server_form_screen.rml", kFormTemplate);
    }

    FormUi::FormUi() {
        services.textWidth = [](const std::string& text, float s, bool ten) {
            return ten ? TenFont::Instance().Width(text, s) : McFont::Instance().Width(text, s);
        };
        services.lineHeight = [](float s, bool ten) {
            return ten ? TenFont::Instance().LineHeight(s) : static_cast<float>(McFont::kLineHeight) * s;
        };
        services.textureSize = [](const std::string& tex, int& w, int& h) {
            return UiAssets::Instance().ImageSize(tex, w, h);
        };
        services.localize = [](const std::string& key) { return UiAssets::Instance().Localize(key); };
        services.anim = &anim;
        services.animDef = [this](const std::string& ref, const std::string& ns) -> Json {
            std::string full = ref;
            if (!full.empty() && full[0] == '@')
                full.erase(0, 1);
            if (!full.contains('.'))
                full = ns + "." + full;
            if (auto cached = animCache.find(full); cached != animCache.end())
                return cached->second;
            BuildReport report;
            Json props = Json::MakeObject();
            if (auto node = BuildTree(library, full, EngineRootVariables(), report))
                props = node->props;
            animCache[full] = props;
            return props;
        };
        emitter.SetTemplates(kFormTemplate, std::string());
    }

    FormUi::~FormUi() {
        if (worker.joinable())
            worker.join();
    }

    void FormUi::Reset() {
        if (worker.joinable())
            worker.join();
        state = 0;
        key.clear();
        tree.reset();
        modalTree.reset();
        library = UiLibrary{};
        anim = AnimHost{};
        animCache.clear();
        problem.clear();
        animStart = std::chrono::steady_clock::now();
        std::lock_guard lock(gImageMutex);
        ++gImageEpoch;
        gImages.clear();
        gImagePending.clear();
        gImageVersion++;
    }

    void FormUi::Warm(const std::vector<std::string>& packDirs) {
        std::string wanted;
        for (const std::string& d : packDirs)
            wanted += d + "|";
        wanted += paths::DataDir() + "|" + UiAssets::Instance().VanillaRoot() + "|" +
                  std::to_string(TexturePack::Instance().Generation());
        if (state.load() == 1)
            return;
        if (state.load() != 0 && wanted == key)
            return;
        if (worker.joinable())
            worker.join();
        key = wanted;
        state = 1;
        std::vector<std::string> roots;
        for (size_t i = packDirs.size(); i-- > 0;)
            roots.push_back(packDirs[i] + "/ui");
        const std::string data = paths::DataDir();
        if (const std::string packUi = ExtractPackUi(data, "pack_ui_form"); !packUi.empty())
            roots.push_back(packUi + "/ui");
        roots.push_back(data + "/ui");
        roots.push_back(data + "/ui_defaults");
        roots.push_back(UiAssets::Instance().VanillaRoot() + "/ui");
        {
            // the old text-row stand-ins for form controls; the real controls draw now
            std::error_code ec;
            std::filesystem::remove(data + "/ui_defaults/perlin_form.json", ec);
        }
        worker = std::thread([this, roots]() {
            library.SetRoots(roots);
            tree.reset();
            modalTree.reset();
            problem.clear();
            if (!library.Scan()) {
                problem = "no ui folder";
                state = 3;
                return;
            }
            BuildReport report;
            tree = BuildTree(library, "server_form.third_party_server_screen", EngineRootVariables(), report);
            if (!tree) {
                problem = "server_form.third_party_server_screen could not be built";
                state = 3;
                return;
            }
            // modal forms use the game's two-button popup
            Vars modalVars = EngineRootVariables();
            for (const char* flag :
                 {"$no_buttons_visible", "$single_button_visible", "$single_button_checkbox_visible",
                  "$two_buttons_checkbox_visible", "$destructive_two_buttons_visible",
                  "$three_buttons_visible", "$destructive_three_buttons_visible", "$show_close_button"})
                modalVars[flag] = Json(false);
            modalVars["$two_buttons_visible"] = Json(true);
            modalVars["$button_text_binding_type"] = Json("global");
            BuildReport modalReport;
            modalTree = BuildTree(library, "popup_dialog.modal_dialog_popup", modalVars, modalReport);
            for (const std::string& missing : report.missingControls)
                Log("form", "missing control {}", missing);
            for (const std::string& missing : modalReport.missingControls)
                Log("form", "modal missing control {}", missing);
            if (!modalTree)
                Log("form", "modal tree is null");
            state = 2;
        });
    }

    bool FormUi::WaitReady(int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (state.load() == 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return Ready();
    }

    bool FormUi::Build(const FormModel& form, int guiW, int guiH, float scroll, std::string& rml,
                       float& contentHeight, float& viewHeight) {
        if (state.load() != 2 || !tree)
            return false;
        FormData data(form);
        const auto started = std::chrono::steady_clock::now();
        anim.now = std::chrono::duration<double>(started - animStart).count();
        const bool wantModal = form.type == FormModel::Type::Modal;
        const Node& screen = wantModal && modalTree ? *modalTree : *tree;
        if (wantModal && !modalTree)
            Log("form", "modal requested but no modal tree, using simple screen");
        const std::unique_ptr<LNode> root =
            EvaluateTree(screen, data, services, static_cast<float>(guiW), static_cast<float>(guiH));
        if (!root) {
            Log("form", "evaluate failed for '{}'", form.title);
            return false;
        }
        emitter.SetEditText(
            [&form](const LNode& n, std::string& text, std::string& placeholder, bool& focused) {
                if (n.index < 0 || static_cast<size_t>(n.index) >= form.elements.size())
                    return false;
                text = form.elements[static_cast<size_t>(n.index)].value;
                placeholder = form.elements[static_cast<size_t>(n.index)].placeholder;
                focused = form.focus == n.index;
                return true;
            });
        emitter.SetScroll(scroll);
        emitter.SetTemplates(ScreenTemplate("server_form_screen.rml", kFormTemplate), std::string());
        rml = emitter.Emit(*root, static_cast<float>(guiW), static_cast<float>(guiH), std::string(), false);
        contentHeight = emitter.ContentHeight();
        viewHeight = emitter.ViewportHeight();
        for (size_t i = 0; i < form.elements.size(); i++)
            if (const auto track = emitter.SliderTracks().find("k:" + std::to_string(i));
                track != emitter.SliderTracks().end()) {
                form.elements[i].trackX = track->second.first;
                form.elements[i].trackW = track->second.second;
            }
        // server form designs may draw text as images or encode data in titles: text check only guards vanilla-only layouts
        if (!ServerStyled()) {
            std::vector<std::string> samples;
            if (form.type == FormModel::Type::Modal) {
                samples.push_back(form.button1);
                samples.push_back(form.button2);
                samples.push_back(form.content);
            } else if (form.type == FormModel::Type::Custom) {
                for (const FormElement& e : form.elements)
                    samples.push_back(e.text);
            } else {
                for (const FormButton& b : form.buttons)
                    samples.push_back(b.text);
                samples.push_back(form.content);
            }
            std::string word;
            for (const std::string& raw : samples) {
                const std::string plain = StripFormatCodes(raw);
                std::string current;
                for (const char c : plain) {
                    if (std::isalnum(static_cast<unsigned char>(c))) {
                        current += c;
                    } else {
                        if (current.size() >= 3 && word.empty())
                            word = current;
                        current.clear();
                    }
                }
                if (current.size() >= 3 && word.empty())
                    word = current;
                if (!word.empty())
                    break;
            }
            if (!word.empty() && !rml.contains(word)) {
                problem = "the ui files drew none of the form's text";
                return false;
            }
        }
        const size_t buttons = form.type == FormModel::Type::Modal    ? 2
                               : form.type == FormModel::Type::Custom ? 0
                                                                      : form.buttons.size();
        // metadata rows can hide index zero while later collection entries produce the interactive controls
        if (buttons > 0 && !rml.contains("id=\"fb:")) {
            problem = "form buttons were not produced by the ui files";
            Log("form", "no buttons in rml for '{}' (modal={} hasModalTree={}), rml head: {}", form.title,
                wantModal, (modalTree != nullptr), rml.substr(0, 600));
            {
                std::error_code ec;
                std::filesystem::create_directories(paths::DataDir() + "/ui_generated", ec);
                std::ofstream(paths::DataDir() + "/ui_generated/last_form_json_failed.rml",
                              std::ios::binary | std::ios::trunc)
                    << rml;
            }
            return false;
        }
        return true;
    }
}
