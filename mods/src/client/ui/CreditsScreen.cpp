#include "client/ui/CreditsScreen.hpp"

#include "client/resources/ClientAssets.hpp"
#include "client/ui/Json.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
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
            std::ifstream in(std::filesystem::path(clientassets::Directory(paths::DataDir())) / name,
                             std::ios::binary);
            if (!in)
                return false;
            std::stringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        // wrap to java poem width (274 gui px)
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

        // end.txt, postcredits.txt: blank-line paragraphs, §3/§2 mark the two speakers
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

        // java edition credits, extracted from the client jar with other assets
        bool AppendJavaCredits(std::vector<Line>& lines) {
            std::string text;
            if (!ReadText("credits.json", text))
                return false;
            Json root;
            std::string error;
            if (!Json::Parse(text, root, error) || !root.IsArray())
                return false;
            lines.push_back({"Minecraft: Java Edition", "head"});
            lines.push_back({"", ""});
            for (const Json& section : root.array) {
                if (const Json* name = section.Find("section"))
                    lines.push_back({name->StringOr(""), "head"});
                lines.push_back({"", ""});
                const Json* disciplines = section.Find("disciplines");
                if (!disciplines || !disciplines->IsArray())
                    continue;
                for (const Json& discipline : disciplines->array) {
                    if (const Json* name = discipline.Find("discipline"); name && !name->StringOr("").empty())
                        lines.push_back({name->StringOr(""), "head"});
                    const Json* titles = discipline.Find("titles");
                    if (!titles || !titles->IsArray())
                        continue;
                    for (const Json& title : titles->array) {
                        if (const Json* name = title.Find("title"); name && !name->StringOr("").empty())
                            lines.push_back({name->StringOr(""), "sub"});
                        if (const Json* names = title.Find("names"); names && names->IsArray())
                            for (const Json& person : names->array)
                                lines.push_back({person.StringOr(""), "name"});
                        lines.push_back({"", ""});
                    }
                }
                lines.push_back({"", ""});
            }
            return true;
        }

        // java order: end poem, credits, post-credits
        const std::vector<Line>& Lines(const std::string& player = "") {
            // jar extracted in background on first launch: retry until texts appear
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

        const char* kTemplate = R"PERLINRML(<rml>
<head>
<title>credits</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background-color: #000000; }
#text { position: absolute; left: 0px; top: {{top}}px; width: {{w}}px; }
.l { display: block; height: 12px; line-height: 12px; text-align: center; font-family: minecraft-shadow; font-size: 8px; color: #ffffff; }
.title { color: #ffff55; font-size: 16px; height: 24px; line-height: 24px; }
.sub { color: #aaaaaa; }
.head { color: #ffff55; }
.note { color: #555555; }
.poem { text-align: left; padding-left: {{poem_x}}px; width: 274px; }
.poem3 { text-align: left; padding-left: {{poem_x}}px; width: 274px; color: #00aaaa; }
.poem2 { text-align: left; padding-left: {{poem_x}}px; width: 274px; color: #00aa00; }
.hint { position: absolute; left: 0px; top: {{hint_y}}px; width: {{w}}px; height: 12px; line-height: 12px; background-color: #000000; text-align: center; font-family: minecraft-shadow; font-size: 8px; color: #555555; }
</style>
</head>
<body>
  <div id="text">{{lines}}</div>
  <div class="hint">{{hint}}</div>
</body>
</rml>
)PERLINRML";
    }

    int CreditsHeight() {
        int h = 0;
        for (const Line& l : Lines())
            h += l.Height();
        return h;
    }

    std::string BuildCreditsScreenRml(int guiW, int guiH, int scroll, const std::string& player) {
        // thousands of lines: emit only visible ones
        const int top = guiH - scroll;
        int y = 0, skipped = 0;
        std::string lines;
        for (const Line& l : Lines(player)) {
            const int h = l.Height();
            if (top + y + h < 0)
                skipped += h;
            else if (top + y <= guiH)
                lines += "<div class=\"l " + l.cls + "\">" +
                         (l.text.empty() ? std::string("&nbsp;") : EscapeRml(l.text)) + "</div>";
            y += h;
        }
        std::string d = kTemplate;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "top", std::to_string(top + skipped));
        ReplaceAll(d, "poem_x", std::to_string(std::max(0, (guiW - 274) / 2)));
        ReplaceAll(d, "hint_y", std::to_string(guiH - 12));
        ReplaceAll(d, "hint", "Press Escape to skip");
        ReplaceAll(d, "lines", lines);
        return d;
    }
}
