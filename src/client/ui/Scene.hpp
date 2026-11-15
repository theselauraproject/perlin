#pragma once
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace perlin::ui {
    struct SceneRun {
        std::string text;
        std::string colored;
        std::string color = "#ffffff";
        std::string weight = "normal";
        std::string style = "normal";
        std::string background = "transparent";
        std::string display = "inline";
        std::string width = "auto";
        std::string overflow = "visible";
    };

    struct SceneNode {
        enum Kind { Box = 0, Image = 1, Item = 2, Text = 3, Rect = 4, Gradient = 5, Link = 6, Label = 7, Button = 8, Slider = 9 };

        int kind = Box;
        std::string id;
        std::string cls;
        std::string snd;
        std::string vol;
        std::string pit;
        float x = 0, y = 0, w = 0, h = 0;
        float knob = 0;
        std::string z = "auto";

        std::string src;
        std::string uv;
        std::string nine;
        std::string tiled;
        std::string tileScale;
        std::string clipDirection;
        std::string tint;
        std::string alpha;
        std::string keep;
        std::string glint;
        std::string key;

        std::string color = "#ffffffff";
        std::string decorator;

        std::string family = "minecraft";
        std::string align = "left";
        float size = 8.0f;
        float lineHeight = 9.0f;
        std::vector<SceneRun> runs;

        std::vector<SceneNode> children;
    };

    struct SceneRow {
        std::map<std::string, std::string> v;
        std::map<std::string, std::vector<SceneRun>> runs;
        std::map<std::string, std::vector<SceneRow>> rows;

        SceneRow() = default;
        SceneRow(std::initializer_list<std::pair<const std::string, std::string>> values) : v(values) {}
    };

    struct Scene {
        std::vector<SceneNode> nodes;
        std::map<std::string, std::vector<SceneNode>> lists;
        std::map<std::string, std::string> vars;
        std::map<std::string, bool> flags;
        std::map<std::string, std::vector<SceneRun>> texts;
        std::map<std::string, std::vector<SceneRow>> rows;
    };

    struct SceneDoc {
        std::string rml;
        Scene scene;
    };

    void EnsureSceneLists(SceneDoc& doc);
    bool SceneHasId(const SceneDoc& doc, const std::string& id, bool prefix = false);
    bool SceneHasText(const SceneDoc& doc, const std::string& needle);
    std::string DumpScene(const SceneDoc& doc);

    constexpr int kSceneMaxDepth = 6;

    class SceneBuilder {
    public:
        static constexpr int kAutoZ = INT32_MIN;

        explicit SceneBuilder(std::vector<SceneNode>& root) {
            stack.push_back(&root);
            flat.push_back(0);
        }

        SceneNode& Add(SceneNode node);
        SceneNode& Begin(SceneNode node);
        void End();
        int Depth() const {
            int d = 0;
            for (const char f : flat)
                d += f ? 0 : 1;
            return d - 1;
        }
        std::vector<SceneNode>& Current() { return *stack.back(); }

    private:
        std::vector<std::vector<SceneNode>*> stack;
        std::vector<char> flat;
        std::vector<std::pair<float, float>> shifts;
        float shiftX = 0, shiftY = 0;
    };

    std::string SceneNumber(float v);
    inline std::string SceneFlag(bool v) { return v ? "1" : ""; }
    std::vector<SceneRun> ParseFormatLines(const std::string& text, const std::string& baseColor);
    std::vector<SceneRun> ParseFormatRuns(const std::string& text, const std::string& baseColor,
                                          float alpha = 1.0f, bool breaks = false);
}
