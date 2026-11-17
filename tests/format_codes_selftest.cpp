#include "client/systems/text/FormatCodes.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace perlin::ui;

namespace {
    int failures = 0;
    void Expect(const std::string& got, const std::string& want, const char* what) {
        if (got == want)
            return;
        std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
        failures++;
    }
    void Expect(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }
}

namespace {
    std::string Spans(const std::string& text) {
        std::string out;
        char buf[32];
        for (const FormatRun& run : ParseFormatCodes(text, false)) {
            std::string style;
            if (run.hasColor) {
                std::snprintf(buf, sizeof buf, "#%06X", run.rgb & 0xFFFFFFu);
                style += buf;
            }
            if (run.bold)
                style += style.empty() ? "bold" : " bold";
            if (run.italic)
                style += style.empty() ? "italic" : " italic";
            out += style.empty() ? run.text : "[" + style + "]" + run.text + "[/]";
        }
        return out;
    }
}

int main() {

    Expect(Spans("\xC2\xA7l\xC2\xA7"
                            "cA\xC2\xA7"
                            "aB\xC2\xA7rC"),
           "[#FF5555 bold]A[/][#55FF55 bold]B[/]C",
           "colour keeps bold");

    Expect(Spans("ab\xC2\xA7"
                            "ecd"),
           "ab[#FFFF55]cd[/]", "mid-word");

    Expect(Spans("\xC2\xA7mx\xC2\xA7nY"),
           "[#EE222C]x[/][#C87363]Y[/]", "m and n");

    SetFormatColorOverrides({{'i', 0xD461FF}});
    Expect(Spans("\xC2\xA7ix"), "[#D461FF]x[/]", "pack override");
    SetFormatColorOverrides({});
    Expect(Spans("\xC2\xA7ix"), "[#A9B4B7]x[/]", "override cleared");
    Expect(Spans("\xC2\xA7ox"), "[italic]x[/]", "italic");

    Expect(StripFormatCodes("\xC2\xA7\xC3\xA9x"), "x", "multibyte code strip");
    Expect(Spans("\xC2\xA7\xC3\xA9x"), "x", "multibyte code");
    Expect(StripFormatCodes("a\xC2\xA7"
                            "bc"),
           "ac", "strip");

    FormatState st;
    st.Apply('l');
    st.Apply('C');
    Expect(st.bold && st.hasColor && st.rgb == 0xFF5555, "state after bold + colour");
    Expect(st.Codes(),
           "\xC2\xA7"
           "c\xC2\xA7l",
           "codes to carry over a wrap");
    Expect(!st.Apply('z'), "unknown code ignored");
    st.Apply('r');
    Expect(!st.bold && !st.hasColor && st.Codes().empty(), "reset");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
