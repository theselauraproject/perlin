// Label text layout: spaces are kept the way Bedrock draws them, and default label widths leave the
// shadow pixel, so server scoreboard rows padded with a leading space sit centred in their boxes.
#include "client/ui/LabelText.hpp"

#include <cstdio>
#include <string>

namespace {
    int failed = 0;
    void Check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL %s\n", what);
            failed++;
        }
    }
    // Every glyph is 6 px wide and a space 4 px, like the Minecraft bitmap font for these strings.
    float Measure(const std::string& s) {
        float w = 0;
        for (char c : s)
            w += c == ' ' ? 4.0f : 6.0f;
        return w;
    }
}

int main() {
    using perlin::ui::LabelDefaultWidth;
    using perlin::ui::WrapLabelText;
    const auto lines = WrapLabelText(" Points: 0", 0, Measure);
    Check(lines.size() == 1 && lines[0] == " Points: 0", "a leading space is kept");
    Check(Measure(lines[0]) == 4 + 7 * 6 + 4 + 6, "the kept line measures the leading space");

    const auto doubled = WrapLabelText("a  b", 0, Measure);
    Check(doubled.size() == 1 && doubled[0] == "a  b", "a doubled space is kept");

    const auto wrapped = WrapLabelText("aaa bbb", 20, Measure);
    Check(wrapped.size() == 2 && wrapped[0] == "aaa" && wrapped[1] == "bbb", "the space a wrap breaks on is dropped");

    const auto lead = WrapLabelText(" aaa bbb", 25, Measure);
    Check(lead.size() == 2 && lead[0] == " aaa" && lead[1] == "bbb", "a leading space survives wrapping");

    const auto paras = WrapLabelText(" a\n b", 0, Measure);
    Check(paras.size() == 2 && paras[0] == " a" && paras[1] == " b", "each paragraph keeps its leading space");

    // SwimGG: the row box is the label width + 4 px and the label sits at x = 1. With the shadow
    // pixel the ink has the same gap on both sides: 1 + 4 (space) on the left, 4 + 1 on the right
    // (the last glyph's advance already includes 1 px after its ink).
    const float text = Measure(" Points: 0");
    const float label = LabelDefaultWidth(text, true);
    const float box = label + 4;
    const float inkLeft = 1 + 4;
    const float inkRight = 1 + text - 1;
    Check(label == text + 1, "bitmap labels keep the shadow pixel");
    Check(inkLeft == box - inkRight, "scoreboard row text is centred");
    Check(LabelDefaultWidth(text, false) == text, "vector-font labels have no shadow pixel");
    Check(LabelDefaultWidth(0, true) == 0, "an empty label has no width");

    if (failed) {
        std::fprintf(stderr, "%d check(s) failed\n", failed);
        return 1;
    }
    std::puts("ui_label_selftest ok");
    return 0;
}
