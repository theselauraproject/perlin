#include "client/ui/FormatCodes.hpp"

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

int main() {

    Expect(FormatCodesToRml("\xC2\xA7l\xC2\xA7"
                            "cA\xC2\xA7"
                            "aB\xC2\xA7rC"),
           "<span style=\"color:#FF5555;font-weight:bold;\">A</span>"
           "<span style=\"color:#55FF55;font-weight:bold;\">B</span>C",
           "colour keeps bold");

    Expect(FormatCodesToRml("ab\xC2\xA7"
                            "ecd"),
           "ab<span style=\"color:#FFFF55;\">cd</span>", "mid-word");

    Expect(FormatCodesToRml("\xC2\xA7mx\xC2\xA7nY"),
           "<span style=\"color:#EE222C;\">x</span><span style=\"color:#C87363;\">Y</span>", "m and n");

    SetFormatColorOverrides({{'i', 0xD461FF}});
    Expect(FormatCodesToRml("\xC2\xA7ix"), "<span style=\"color:#D461FF;\">x</span>", "pack override");
    SetFormatColorOverrides({});
    Expect(FormatCodesToRml("\xC2\xA7ix"), "<span style=\"color:#A9B4B7;\">x</span>", "override cleared");
    Expect(FormatCodesToRml("\xC2\xA7ox"), "<span style=\"font-style:italic;\">x</span>", "italic");

    Expect(StripFormatCodes("\xC2\xA7\xC3\xA9x"), "x", "multibyte code strip");
    Expect(FormatCodesToRml("\xC2\xA7\xC3\xA9x"), "x", "multibyte code");
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
