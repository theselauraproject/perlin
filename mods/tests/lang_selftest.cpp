#include "client/i18n/Lang.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using perlin::i18n::Lang;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                 \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main(int argc, char** argv) {
    CHECK(Lang::Format("%s joined the game", {"Steve"}) == "Steve joined the game");
    CHECK(Lang::Format("%2$s then %1$s", {"a", "b"}) == "b then a");
    CHECK(Lang::Format("Downloading packs %1", {"1/2"}) == "Downloading packs 1/2");
    CHECK(Lang::Format("%1 of %3", {"a"}) == "a of %3");
    CHECK(Lang::Format("100%% of %d", {"5"}) == "100% of 5");
    CHECK(Lang::Format("%s and %s", {"x"}) == "x and ");

    std::unordered_map<std::string, std::string> map;
    Lang::Parse("\xEF\xBB\xBF## comment\r\na.b=Hello\t# note\r\nc=d=e\r\n", map);
    CHECK(map["a.b"] == "Hello");
    CHECK(map["c"] == "d=e");

    Lang& lang = Lang::Get();
    lang.LoadFromText("hello=Hallo\n", "hello=Hello\nonly.english=Only\n");
    CHECK(lang.Tr("hello") == "Hallo");
    CHECK(lang.Tr("only.english") == "Only");
    CHECK(lang.Tr("missing.key") == "missing.key");
    CHECK(lang.Has("hello") && !lang.Has("missing.key"));

    if (argc > 1) {
        std::ifstream f(std::string(argv[1]) + "/perlin_en_US.lang", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        CHECK(!ss.str().empty());
        std::unordered_map<std::string, std::string> shipped;
        Lang::Parse(ss.str(), shipped);
        CHECK(shipped.size() > 50);
        lang.LoadFromText(ss.str());
        CHECK(lang.Tr("perlin.debug.block", {"1", "2", "3"}) == "Block: 1 2 3");
        CHECK(lang.Tr("perlin.debug.memory", {"50", "10", "20"}) == "Mem: 50% 10/20MB");
        CHECK(lang.Tr("perlin.title.fps", {"60"}) == "60 FPS");
        for (const auto& [k, v] : shipped)
            if (v.empty()) {
                std::printf("empty value for %s\n", k.c_str());
                failures++;
            }
    }
    std::printf(failures ? "lang_selftest FAILED (%d)\n" : "lang_selftest ok\n", failures);
    return failures ? 1 : 0;
}
