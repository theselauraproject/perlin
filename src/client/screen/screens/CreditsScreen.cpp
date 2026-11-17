#include "client/ui/GuiViewport.hpp"
#include "client/screen/screens/CreditsScreen.hpp"
#include "util/Resources.hpp"
#include "client/screen/screens/ScreenSupport.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/Hud.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"
#include "util/Strings.hpp"

#include "client/systems/resources/ClientAssets.hpp"
#include "util/JsonValue.hpp"
#include "client/ui/SceneEmit.hpp"
#include "client/ui/SceneKit.hpp"
#include "util/Paths.hpp"
#include "util/FileIo.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <vector>

namespace perlin::ui {
    namespace {
        struct Line {
            std::string text;
            std::string cls;
            int Height() const { return cls == "title" ? 24 : 12; }
        };

        bool ReadText(const char* name, std::string& out) {
            std::string contents;
            if (!file::Read(std::filesystem::path(clientassets::Directory(paths::DataDir())) / name, contents))
                return false;
            out = contents;
            return true;
        }

        void AppendWrapped(std::vector<Line>& lines, const std::string& text, const std::string& cls) {
            constexpr size_t kColumns = 46;
            std::string line;
            std::istringstream words(text);
            for (std::string word; words >> word;) {
                if (!line.empty() && line.size() + 1 + word.size() > kColumns) {
                    lines.push_back({line, cls});
                    line.clear();
                }
                line += (line.empty() ? "" : " ") + word;
            }
            if (!line.empty())
                lines.push_back({line, cls});
        }

        bool AppendStory(std::vector<Line>& lines, const char* name, const std::string& player) {
            std::string text;
            if (!ReadText(name, text))
                return false;
            std::istringstream in(text);
            for (std::string raw; std::getline(in, raw);) {
                if (!raw.empty() && raw.back() == '\r')
                    raw.pop_back();
                std::string cls = "poem";
                if (raw.starts_with("\u00a73")) {
                    cls = "poem3";
                    raw.erase(0, 3);
                } else if (raw.starts_with("\u00a72")) {
                    cls = "poem2";
                    raw.erase(0, 3);
                }
                for (size_t at; (at = raw.find("PLAYERNAME")) != std::string::npos;)
                    raw.replace(at, 10, player);
                if (raw.empty())
                    lines.push_back({"", ""});
                else
                    AppendWrapped(lines, raw, cls);
            }
            lines.push_back({"", ""});
            lines.push_back({"", ""});
            return true;
        }

        bool AppendJavaCredits(std::vector<Line>& lines) {
            std::string text;
            if (!ReadText("credits.json", text))
                return false;
            json::Value root;
            std::string error;
            if (!json::Value::Parse(text, root, error) || !root.IsArray())
                return false;
            lines.push_back({"Minecraft: Java Edition", "head"});
            lines.push_back({"", ""});
            for (const json::Value& section : root.array) {
                if (const json::Value* name = section.Find("section"))
                    lines.push_back({name->StringOr(""), "head"});
                lines.push_back({"", ""});
                const json::Value* disciplines = section.Find("disciplines");
                if (!disciplines || !disciplines->IsArray())
                    continue;
                for (const json::Value& discipline : disciplines->array) {
                    if (const json::Value* name = discipline.Find("discipline"); name && !name->StringOr("").empty())
                        lines.push_back({name->StringOr(""), "head"});
                    const json::Value* titles = discipline.Find("titles");
                    if (!titles || !titles->IsArray())
                        continue;
                    for (const json::Value& title : titles->array) {
                        if (const json::Value* name = title.Find("title"); name && !name->StringOr("").empty())
                            lines.push_back({name->StringOr(""), "sub"});
                        if (const json::Value* names = title.Find("names"); names && names->IsArray())
                            for (const json::Value& person : names->array)
                                lines.push_back({person.StringOr(""), "name"});
                        lines.push_back({"", ""});
                    }
                }
                lines.push_back({"", ""});
            }
            return true;
        }

        const std::vector<Line>& Lines(const std::string& player = "") {

            static std::vector<Line> lines;
            static std::string builtFor;
            static bool java = false;
            if (lines.empty() || !java || (!player.empty() && player != builtFor)) {
                if (!player.empty())
                    builtFor = player;
                lines.clear();
                java = AppendStory(lines, "end.txt", builtFor);
                java = AppendJavaCredits(lines) && java;
                AppendStory(lines, "postcredits.txt", builtFor);
                lines.push_back({"Perlin is not affiliated with Mojang Studios or Microsoft.", "note"});
            }
            return lines;
        }

        const char* ColorFor(const std::string& cls) {
            if (cls == "title" || cls == "head")
                return "#ffff55";
            if (cls == "sub")
                return "#aaaaaa";
            if (cls == "note" )
                return "#555555";
            if (cls == "poem3")
                return "#00aaaa";
            if (cls == "poem2")
                return "#00aa00";
            return "#ffffff";
        }
    }

    int CreditsHeight() {
        int h = 0;
        for (const Line& l : Lines())
            h += l.Height();
        return h;
    }

    SceneDoc BuildCreditsScreen(int guiW, int guiH, int scroll, const std::string& player) {

        const int top = guiH - scroll;
        int y = 0;
        SceneDoc doc;
        doc.scene.theme = Theme::Credits;
        SceneBuilder b(doc.scene.nodes);
        const float w = static_cast<float>(guiW), h = static_cast<float>(guiH);
        kit::Rect(b, 0, 0, w, h, "#000000", "1");
        const float left = static_cast<float>(std::max(0, (guiW - 274) / 2));
        for (const Line& l : Lines(player)) {
            const int lh = l.Height();
            const bool poem = l.cls.starts_with("poem");
            const int ly = top + y;
            if (ly + lh >= 0 && ly <= guiH && !l.text.empty()) {
                SceneNode& n = kit::Label(b, poem ? left : 0, static_cast<float>(ly), poem ? 274.0f : w,
                                          std::string(poem ? "nowrap" : "ctr nowrap") + (l.cls == "title" ? " big" : ""),
                                          kit::Plain(l.text, ColorFor(l.cls)), "3");
                n.h = static_cast<float>(lh);
            }
            y += lh;
        }
        kit::Rect(b, 0, h - 12, w, 12, "#000000", "4");
        kit::Label(b, 0, h - 12, w, "ctr nowrap", kit::Plain("Press Escape to skip", "#555555"), "5");
        return doc;
    }
}

namespace perlin::screen {
    Claim CreditsScreen::Update(Frame& frame) {
        UpdateCredits(frame.hud, frame.live, frame.fbW, frame.fbH);
        return IsOpen() ? Claim::Frame : Claim::None;
    }

    void CreditsScreen::UpdateCredits(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {

        const bool preview = !live && PERLIN_ENV("PERLIN_PREVIEW_CREDITS") != nullptr;
        if (!live && !preview) {
            host.screens.Close(*this);
            return;
        }
        if ((preview || live->TakeCreditsRequest()) && !IsOpen()) {
            host.screens.Open(*this);
            scroll = 0;
            last = std::chrono::steady_clock::now();
            host.input.SetTextMode(true);
        }
        if (!IsOpen())
            return;
        const ui::GuiViewport gui = ui::OverlayViewport(hud, fbW, fbH);
        const auto now = std::chrono::steady_clock::now();

        scroll += std::chrono::duration<double>(now - last).count() * 20.0;
        last = now;

        const std::string signature = str::Key(static_cast<int>(scroll), str::Cat(gui.w, "x", gui.h));
        if (signature != this->signature || !hud.Overlay().HasOverlay()) {
            this->signature = signature;
            std::string error;
            const std::string& player = live ? live->PlayerName() : host.options.playerName;
            const ui::SceneDoc doc = ui::BuildCreditsScreen(gui.w, gui.h, static_cast<int>(scroll), player);
            if (!hud.Overlay().SetOverlay(doc, error))
                Log("ui", "credits screen: {}", error);
        }
        hud.UpdateOverlay(fbW, fbH, host.options.guiScale);
        const bool escaped = EscapeTyped(host.inputManager.TakeTyped());
        const bool done = escaped || scroll > gui.h + ui::CreditsHeight();
        if (!done)
            return;
        host.screens.Close(*this);
        hud.Overlay().ClearOverlay();
        if (!host.chat.IsOpen())
            host.input.SetTextMode(false);
        if (live)
            live->SendCreditsFinished();
    }
}
