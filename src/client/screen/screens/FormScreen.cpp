#include "client/ui/GuiViewport.hpp"
#include "client/screen/screens/FormScreen.hpp"
#include "client/screen/screens/ScreenSupport.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "util/Env.hpp"
#include "util/FileIo.hpp"
#include "util/Strings.hpp"

#include "client/systems/text/FormatCodes.hpp"
#include "util/JsonValue.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/ui/Hud.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/HudData.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/ui/SceneEmit.hpp"
#include "client/ui/SceneKit.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/ui/AssetServices.hpp"
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
#include "util/TaskGroup.hpp"

namespace perlin::ui {
    namespace {
        std::string Str(const json::Value* j) {
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
            json::Value j(s);
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

        const ScreenTemplateDefault kBuiltinFormDefault("ui/screens/server_form_builtin.rml");

        std::mutex gImageMutex;
        std::map<std::string, std::string> gImages;
        std::set<std::string> gImagePending;
        std::atomic<uint32_t> gImageVersion{0};
        uint64_t gImageEpoch = 0;

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

        SceneNode& Button(SceneBuilder& b, const std::string& id, const std::string& label, float x, float y,
                          float w, float h, const std::function<void()>& extra = {}) {
            std::string shown = label;
            NormalizeFormText(shown);
            const int perLine = std::max(8, static_cast<int>(w / 5.2f));
            const int lines = std::max(1, LineCount(shown, perLine));
            const float hh = ButtonHeight(label, w, h);
            SceneNode& box = kit::Begin(b, x, y, w, hh, "btn", id, "5");
            kit::Box(b, 1, 1, w - 3, hh - 4, "rim");
            kit::Box(b, 2, 2, w - 4, hh - 5, "face");
            kit::Box(b, 1, hh - 3, w - 2, 2, "shade");
            kit::Box(b, w - 2, 1, 1, hh - 4, "edge");
            SceneNode& lbl = kit::Label(b, 0, (hh - static_cast<float>(lines) * 9.0f) / 2.0f, w, "lbl",
                                        ParseFormatLines(shown, ""));
            lbl.h = static_cast<float>(lines * 9);
            if (extra)
                extra();
            b.End();
            return box;
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
        ProcessTasks().Run([url, epoch]() {
            std::string body;
            try {
                const http::Response r = http::GetUrl(url, {}, 20);
                if (r.status >= 200 && r.status < 300 && r.body.size() < (8u << 20))
                    body = r.body;
            } catch (const std::exception& e) {
                Log("form", "image fetch failed for {}: {}", url, e.what());
            }
            {
                std::lock_guard lock(gImageMutex);

                if (epoch != gImageEpoch)
                    return;
                gImages[url] = std::move(body);
                gImageVersion++;
            }
        });
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
        json::Value root;
        std::string error;
        if (!json::Value::Parse(json, root, error) || !root.IsObject())
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
            const json::Value* content = root.Find("content");
            if (content && content->IsArray()) {
                for (const json::Value& e : content->array) {
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
                        if (const json::Value* d = e.Find("default"))
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
                        const json::Value* list = e.Find(kind == "dropdown" ? "options" : "steps");
                        if (list && list->IsArray())
                            for (const json::Value& o : list->array)
                                el.options.push_back(o.IsString() ? o.string : std::string());
                        if (const json::Value* d = e.Find("default"))
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

        const json::Value* buttons = root.Find("buttons");
        if (!buttons || !buttons->IsArray())
            buttons = root.Find("elements");
        if (buttons && buttons->IsArray())
            for (const json::Value& b : buttons->array) {
                FormButton fb;
                fb.text = Str(b.Find("text"));
                fb.kind = Str(b.Find("type"));
                if (const json::Value* img = b.Find("image")) {
                    fb.imageType = Str(img->Find("type"));
                    fb.imageData = Str(img->Find("data"));
                }
                out.buttons.push_back(std::move(fb));
            }
        return true;
    }

    SceneDoc BuildFormScene(const FormModel& form, int guiW, int guiH, float scroll, float& contentHeight,
                            float& viewHeight) {
        const float panelW = static_cast<float>(std::min(guiW - 16, 260));
        const float inner = panelW - 20;
        const int perLine = std::max(8, static_cast<int>(inner / 5.2f));
        const float panelX = (guiW - panelW) / 2.0f;
        const float viewTop = 30.0f;
        viewHeight = static_cast<float>(std::max(60, guiH - 60));
        std::vector<SceneNode> body;
        SceneBuilder b(body);
        float y = 0.0f;
        const auto text = [&](const std::string& t, bool center) {
            std::string shown = t;
            NormalizeFormText(shown);
            const int lines = LineCount(shown, perLine);
            const float h = static_cast<float>(lines * 9);
            SceneNode& node = kit::Label(b, 10, y, inner, center ? "t c" : "t", ParseFormatLines(shown, ""));
            node.h = h;
            y += h + 4.0f;
        };
        if (!form.content.empty() && form.type != FormModel::Type::Custom)
            text(form.content, false);
        if (form.type == FormModel::Type::Simple) {
            for (size_t i = 0; i < form.buttons.size(); i++) {
                const std::string src = ImageSource(form.buttons[i]);
                float h = src.empty() ? 20.0f : 32.0f;
                h = ButtonHeight(form.buttons[i].text, inner, h);
                Button(b, "b:" + std::to_string(i), form.buttons[i].text, 10, y, inner, h, [&] {
                    if (!src.empty())
                        kit::Image(b, 4, 4, 24, 24, src, "8");
                });
                y += h + 4.0f;
            }
        } else if (form.type == FormModel::Type::Modal) {
            const float half = (inner - 4) / 2;
            Button(b, "m:1", form.button1, 10, y, half, 20);
            Button(b, "m:2", form.button2, 10 + half + 4, y, half, 20);
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
                    kit::Rect(b, 10, y + 2, inner, 1, "#707070");
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
                    kit::Begin(b, 10, y, inner, 20, focus ? "field focus" : "field", "i:" + n, "5");
                    kit::Box(b, 1, 1, inner - 2, 18, "in");
                    SceneNode& txt = kit::Label(b, 4, 5, inner - 8, "txt", ParseFormatLines(shown, ""));
                    txt.h = 9;
                    b.End();
                    y += 24.0f;
                    break;
                }
                case FormElement::Kind::Toggle:
                    Button(b, "t:" + n,
                           e.text + ": " +
                               (e.flag ? "\xC2\xA7"
                                         "aON"
                                       : "\xC2\xA7"
                                         "cOFF"),
                           10, y, inner, 20);
                    y += 24.0f;
                    break;
                case FormElement::Kind::Dropdown:
                    Button(b, "d:" + n, e.text + ": " + e.options[static_cast<size_t>(e.index)], 10, y, inner, 20);
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
                    const float knobX = 10.0f + 1.0f + static_cast<float>(frac) * (inner - 10.0f);
                    Button(b, "k:" + n,
                           e.text + ": " + (plain ? Num(e.number) : e.options[static_cast<size_t>(e.index)]), 10, y,
                           inner, 20, [&] {
                               kit::Begin(b, knobX - 10.0f, 0, 8, 20, "knob", {}, "7");
                               kit::Rect(b, 0, 0, 8, 20, "#000000");
                               kit::Rect(b, 1, 1, 6, 18, "#c6c6c6");
                               kit::Rect(b, 1, 1, 5, 17, "#ffffff");
                               kit::Rect(b, 2, 2, 4, 16, "#8b8b8b");
                               b.End();
                           });
                    y += 24.0f;
                    break;
                }
                }
            }
            Button(b, "submit", form.submit.empty() ? "Submit" : form.submit, 10, y, inner, 20);
            y += 24.0f;
        }
        contentHeight = y + 6.0f;
        const float maxScroll = std::max(0.0f, contentHeight - viewHeight);
        const float s = std::clamp(scroll, 0.0f, maxScroll);
        const float visible = std::min(contentHeight, viewHeight);

        SceneDoc doc;
        doc.rml = ScreenTemplate("ui/screens/server_form_builtin.rml");
        SceneBuilder top(doc.scene.lists["body"]);
        kit::Box(top, 0, 0, static_cast<float>(guiW), static_cast<float>(guiH), {}, "bg", "1");
        kit::Box(top, panelX, viewTop - 18, panelW, visible + 24, {}, "panel", "2");
        SceneNode& title = kit::Label(top, panelX, viewTop - 13, panelW, "t c", ParseFormatLines(form.title, ""), "4");
        title.h = 9;
        Button(top, kFormCloseId, "x", panelX + panelW - 14, viewTop - 16, 12, 12);
        SceneNode& clip = kit::Begin(top, panelX, viewTop, panelW, visible, "clip", "clip", "3");
        SceneNode& content = kit::Box(top, 0, -s, panelW, contentHeight);
        content.children = std::move(body);
        top.End();
        (void)clip;
        return doc;
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

            bool Global(const std::string& name, json::Value& out) const override {
                if (name == "#form_button_length") {
                    out = json::Value(static_cast<double>(items.size()));
                    return true;
                }
                if (name == "#title_text") {
                    out = json::Value(form.title);
                    return true;
                }
                if (name == "#form_text") {
                    out = json::Value(form.content);
                    return true;
                }
                if (name == "#custom_form_length") {
                    out = json::Value(static_cast<double>(form.elements.size()));
                    return true;
                }
                if (name == "#submit_text") {
                    out = json::Value(form.submit.empty() ? std::string("Submit") : form.submit);
                    return true;
                }
                if (name == "#submit_button_visible") {
                    out = json::Value(true);
                    return true;
                }
                if (name == "#is_using_gamepad") {
                    out = json::Value(false);
                    return true;
                }
                if (name == "#close_button_visible") {
                    out = json::Value(true);
                    return true;
                }
                if (name == "#form_button_contents") {
                    out = json::Value(static_cast<double>(items.size()));
                    return true;
                }
                if (form.type == FormModel::Type::Modal) {
                    if (name == "#modal_title_text") {
                        out = json::Value(form.title);
                        return true;
                    }
                    if (name == "#modal_label_text") {
                        out = json::Value(form.content);
                        return true;
                    }
                    if (name == "#modal_left_button_text") {
                        out = json::Value(form.button1);
                        return true;
                    }
                    if (name == "#modal_rightcancel_button_text") {
                        out = json::Value(form.button2);
                        return true;
                    }
                    if (name == "#modal_middle_button_text") {
                        out = json::Value(std::string());
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
                                 json::Value& out) const override {
                if (collection == "custom_form") {
                    if (index < 0 || static_cast<size_t>(index) >= form.elements.size())
                        return false;
                    const FormElement& e = form.elements[static_cast<size_t>(index)];
                    if (binding == "#custom_text") {
                        out = json::Value(e.text);
                    } else if (binding == "#custom_row_text") {
                        out = json::Value(ElementRowText(e, form.focus == index));
                    } else if (binding == "#custom_placeholder_text") {
                        out = json::Value(e.placeholder);
                    } else if (binding == "#custom_input_text") {
                        out = json::Value(e.value);
                    } else if (binding == "#custom_toggle_state") {
                        out = json::Value(e.flag);
                    } else if (binding == "#custom_dropdown") {
                        out = json::Value(form.openDropdown == index);
                    } else if (binding == "#custom_slider_value") {

                        const double span = e.max - e.min;
                        out = json::Value(span > 0 ? std::clamp((e.number - e.min) / span, 0.0, 1.0) : 0.0);
                    } else if (binding == "#custom_slider_step_value") {
                        out = json::Value(static_cast<double>(e.index));
                    } else if (binding == "#custom_slider_steps") {
                        out = json::Value(static_cast<double>(std::max<size_t>(e.options.size(), 1)));
                    } else if (binding == "#custom_slider_text" || binding == "#custom_slider_step_text" ||
                               binding == "#custom_slider_text_value" ||
                               binding == "#custom_slider_step_text_value") {
                        out = json::Value(ElementRowText(e, false));
                    } else if (binding == "#custom_tooltip_text") {
                        out = json::Value(std::string());
                    } else if (binding == "#custom_input_enabled" || binding == "#custom_toggle_enabled" ||
                               binding == "#custom_slider_enabled") {
                        out = json::Value(true);
                    } else if (binding == "#dropdown_option_text") {
                        out =
                            json::Value(e.options.empty() ? std::string() : e.options[static_cast<size_t>(e.index)]);
                    } else if (binding == "#custom_dropdown_length") {
                        out = json::Value(static_cast<double>(form.openDropdown == index ? e.options.size() : 0));
                    } else {
                        return false;
                    }
                    return true;
                }

                if (collection == "custom_dropdown") {
                    const int owner = form.openDropdown, option = index;
                    if (owner < 0 || static_cast<size_t>(owner) >= form.elements.size())
                        return false;
                    const FormElement& e = form.elements[static_cast<size_t>(owner)];
                    if (option < 0 || option >= static_cast<int>(e.options.size()))
                        return false;
                    if (binding == "#custom_radio_text")
                        out = json::Value(e.options[static_cast<size_t>(option)]);
                    else if (binding == "#custom_radio_toggled")
                        out = json::Value(option == e.index);
                    else
                        return false;
                    return true;
                }
                if (collection != "form_buttons" || index < 0 || static_cast<size_t>(index) >= items.size())
                    return false;
                const FormButton& b = items[static_cast<size_t>(index)];
                if (binding == "#form_button_text") {
                    out = json::Value(b.text);
                    return true;
                }
                if (binding == "#form_button_texture") {

                    if (b.imageType == "url") {
                        std::string bytes;
                        out = json::Value(FormImageBytes(b.imageData, bytes)
                                       ? (bytes.empty() ? std::string() : "mcurl:" + b.imageData)
                                       : std::string("loading"));
                    } else {
                        out = json::Value(b.imageType == "path" ? b.imageData : std::string());
                    }
                    return true;
                }
                if (binding == "#form_button_texture_file_system") {
                    out = json::Value(std::string(b.imageType == "url"    ? "Url"
                                           : b.imageType == "path" ? "InUserPackage"
                                                                   : ""));
                    return true;
                }
                if (binding == "#form_button_contents") {
                    out = json::Value(static_cast<double>(items.size()));
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
        const ScreenTemplateDefault kFormTemplateDefault("ui/screens/server_form_screen.rml");
    }

    FormUi::FormUi() {
        BindAssetServices(services);
        services.anim = &anim;
        services.animDef = [this](const std::string& ref, const std::string& ns) -> json::Value {
            std::string full = ref;
            if (!full.empty() && full[0] == '@')
                full.erase(0, 1);
            if (!full.contains('.'))
                full = ns + "." + full;
            if (auto cached = animCache.find(full); cached != animCache.end())
                return cached->second;
            BuildReport report;
            json::Value props = json::Value::MakeObject();
            if (auto node = BuildTree(library, full, EngineRootVariables(), report))
                props = node->props;
            animCache[full] = props;
            return props;
        };
        emitter.SetTemplates(ui::BuiltinTemplate("ui/screens/server_form_screen.rml"), std::string());
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
        const std::string wanted = WarmKey(packDirs);
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

            Vars modalVars = EngineRootVariables();
            for (const char* flag :
                 {"$no_buttons_visible", "$single_button_visible", "$single_button_checkbox_visible",
                  "$two_buttons_checkbox_visible", "$destructive_two_buttons_visible",
                  "$three_buttons_visible", "$destructive_three_buttons_visible", "$show_close_button"})
                modalVars[flag] = json::Value(false);
            modalVars["$two_buttons_visible"] = json::Value(true);
            modalVars["$button_text_binding_type"] = json::Value("global");
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

    bool FormUi::Build(const FormModel& form, int guiW, int guiH, float scroll, SceneDoc& doc,
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
        emitter.SetTemplates(ScreenTemplate("ui/screens/server_form_screen.rml"), std::string());
        doc = emitter.Emit(*root, static_cast<float>(guiW), static_cast<float>(guiH), std::string());
        contentHeight = emitter.ContentHeight();
        viewHeight = emitter.ViewportHeight();
        for (size_t i = 0; i < form.elements.size(); i++)
            if (const auto track = emitter.SliderTracks().find("k:" + std::to_string(i));
                track != emitter.SliderTracks().end()) {
                form.elements[i].trackX = track->second.first;
                form.elements[i].trackW = track->second.second;
            }

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
            if (!word.empty() && !SceneHasText(doc, word)) {
                problem = "the ui files drew none of the form's text";
                return false;
            }
        }
        const size_t buttons = form.type == FormModel::Type::Modal    ? 2
                               : form.type == FormModel::Type::Custom ? 0
                                                                      : form.buttons.size();

        if (buttons > 0 && !SceneHasId(doc, "fb:", true)) {
            problem = "form buttons were not produced by the ui files";
            Log("form", "no buttons in the scene for '{}' (modal={} hasModalTree={})", form.title, wantModal,
                (modalTree != nullptr));
            {
                std::error_code ec;
                std::filesystem::create_directories(paths::DataDir() + "/ui_generated", ec);
                std::ofstream(paths::DataDir() + "/ui_generated/last_form_json_failed.rml",
                              std::ios::binary | std::ios::trunc)
                    << DumpScene(doc);
            }
            return false;
        }
        return true;
    }
}

namespace perlin::screen {
    namespace {
        std::string FormDumpName(const std::string& title) {
            std::string name;
            for (const char c : str::StripFormatting(title))
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
                    name += c;
                else if (!name.empty() && name.back() != '_')
                    name += '_';
            if (name.size() > 60)
                name.resize(60);
            return name;
        }
    }

    Claim FormScreen::Update(Frame& frame) {
        UpdateForm(frame.hud, frame.live, frame.fbW, frame.fbH);
        return Claim::None;
    }

    void FormScreen::UpdateForm(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const auto now = std::chrono::steady_clock::now();

        const char* previewForm = live ? nullptr : PERLIN_ENV("PERLIN_PREVIEW_FORM");
        if (!live && !previewForm) {
            if (IsOpen())
                CloseOverlay(host, hud, *this);
            return;
        }
        const auto respond = [&](const std::string* response) {
            if (live)
                live->SendFormResponse(model.id, response, false);
            else
                Log("form", "preview answer: {}", (response ? *response : std::string("closed")));
        };
        const auto respondAndClose = [&](const std::string* response) {
            respond(response);
            CloseOverlay(host, hud, *this);
        };
        if (now - lastWarm > std::chrono::seconds(2)) {
            lastWarm = now;
            ui.Warm(host.serverPackDirectories());
        }
        if (live && live->TakeFormClosed() && IsOpen())
            CloseOverlay(host, hud, *this);
        if (IsOpen() && !engine && ui.Ready() && !engineFailed)
            engine = true;

        if (!IsOpen()) {
            if (host.screens.IsOpen(Id::Death) || host.screens.IsOpen(Id::Inventory) || host.screens.IsOpen(Id::Pause) || host.chat.IsOpen())
                return;
            BedrockSession::ServerForm sf;
            if (live && !live->TakeServerForm(sf))
                return;
            if (!live) {
                if (previewOpened)
                    return;
                previewOpened = true;
                sf.id = 1;
                sf.json = file::Read(previewForm);
            }
            if (!ui::ParseForm(sf.id, sf.json, model)) {
                Log("form", "cannot parse form {}", sf.id);
                if (live)
                    live->SendFormResponse(sf.id, nullptr, false);
                return;
            }

            if (!live)
                if (const char* clicks = PERLIN_ENV("PERLIN_PREVIEW_FORM_CLICKS")) {
                    std::stringstream list(clicks);
                    std::string ignored;

                    for (std::string id; std::getline(list, id, ',');)
                        if (!ui::FormDismissDropdown(model, id == "-" ? std::string() : id))
                            ui::FormClick(model, id, ignored);
                }
            Log("form", "opened form {} '{}'", sf.id, model.title);
            DumpGenerated("last_form.json", sf.json);
            if (const std::string name = FormDumpName(model.title); !name.empty())
                DumpGenerated("forms/" + name + ".json", sf.json);
            ui.Warm(host.serverPackDirectories());
            ui.WaitReady(150);

            engineFailed = false;
            engine = ui.Ready();
            host.screens.Open(*this);
            scroll = 0.0f;
            contentH = viewH = 0.0f;
            signature.clear();
            openedAt = now;
            leftDown = true;
            host.input.SetTextMode(true);
            CenterCursorForGui(host);
        }

        bool cancel = false;
        for (const auto& t : host.inputManager.TakeTyped()) {
            if (IsEscape(t))
                cancel = true;
            else
                ui::FormEdit(model, t);
        }
        scroll = std::max(0.0f, scroll - host.inputManager.Scroll() * 20.0f);
        if (cancel)
            return respondAndClose(nullptr);

        if (!engine && ui.Building() && now - openedAt < std::chrono::seconds(3))
            return;

        const ui::GuiViewport gui = ui::OverlayViewport(hud, fbW, fbH);
        scroll = std::clamp(scroll, 0.0f, std::max(0.0f, contentH - viewH));
        const std::string signature =
            str::Key(model.id, model.focus, model.openDropdown,
                     ui::FormCustomResponse(model), ui::FormImageVersion(), str::Cat(gui.w, "x", gui.h),
                     static_cast<int>(scroll), engine ? "e" : "b");

        if (engine && ui.Animating() && now - lastAnim >= std::chrono::milliseconds(80)) {
            this->signature.clear();
            lastAnim = now;
        }
        if (signature != this->signature || !hud.Overlay().HasOverlay()) {
            this->signature = signature;
            ui::SceneDoc doc;
            std::string error;
            float contentH = 0.0f, viewH = 0.0f;
            if (engine && !ui.Build(model, gui.w, gui.h, scroll, doc, contentH, viewH)) {
                Log("form", "json ui build failed: {}, using the built-in layout", ui.Problem());
                engine = false;
                engineFailed = true;
            }
            if (!engine)
                doc = ui::BuildFormScene(model, gui.w, gui.h, scroll, contentH, viewH);
            this->contentH = contentH;
            this->viewH = viewH;

            DumpGenerated("last_form.rml", ui::DumpScene(doc));
            if (!hud.Overlay().SetOverlay(doc, error)) {
                Log("form", "{}", error);
                if (!engine)
                    return respondAndClose(nullptr);
                engine = false;
                engineFailed = true;
                this->signature.clear();
                return;
            }
        }
        hud.Overlay().MouseMove(gui.ToGui(host.input.MouseX()), gui.ToGui(host.input.MouseY()));
        hud.UpdateOverlay(fbW, fbH, host.options.guiScale);

        const bool down = host.input.IsMouseDown(0);
        if (!down) {
            dragSlider = -1;
        } else {
            const int hoverSlider = ui::FormSliderAt(hud.Overlay().OverlayHover());
            if (dragSlider < 0 && hoverSlider >= 0 && !leftDown)
                dragSlider = hoverSlider;
            if (dragSlider >= 0)
                ui::FormDrag(model, dragSlider, gui.ToGui(host.input.MouseX()));
        }
        if (!down && leftDown && now - openedAt > std::chrono::milliseconds(200)) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kFormCloseId)
                return respondAndClose(nullptr);
            if (ui::FormDismissDropdown(model, hover)) {
                leftDown = down;
                return;
            }
            std::string response;
            if (!hover.empty() && ui::FormClick(model, hover, response))
                return respondAndClose(&response);

            if (!hover.empty() && !hover.starts_with("i:"))
                model.focus = -1;
        }
        leftDown = down;
    }
}
