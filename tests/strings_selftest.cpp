#include "util/Strings.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace perlin;

namespace {
    int failures = 0;

    void Expect(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    template <class T> void ExpectEq(const T& got, const T& want, const char* what) {
        Expect(got == want, what);
    }

    static_assert(str::TrimView("  \t a b \r\n") == "a b");
    static_assert(str::TrimView("   ").empty());
    static_assert(str::AsciiLower('Q') == 'q' && str::AsciiLower('q') == 'q' && str::AsciiLower('1') == '1');
    static_assert(str::StripNamespace("minecraft:stone") == "stone");
    static_assert(str::StripNamespace("custom:stone") == "custom:stone");
    static_assert(str::TrimRightView("0.500", "0") == "0.5");
    static_assert(str::ContainsAny("abc", {"x", "bc"}) && !str::ContainsAny("abc", {"x", "y"}));
}

int main() {
    ExpectEq<std::string>(str::Lower("MiXeD 123 \xC3\x89"), "mixed 123 \xC3\x89",
                          "lower leaves non-ascii bytes alone");
    ExpectEq<std::string>(str::Upper("MiXeD 123"), "MIXED 123", "upper");
    ExpectEq<std::string>(str::Trim("\t x y \n"), "x y", "trim keeps inner whitespace");

    ExpectEq<std::string>(str::ReplaceAll("a-b-c", "-", "+"), "a+b+c", "replace all");
    ExpectEq<std::string>(str::ReplaceAll("aaa", "a", "aa"), "aaaaaa", "replacement text is not rescanned");
    ExpectEq<std::string>(str::ReplaceAll("abc", "", "x"), "abc", "empty needle changes nothing");
    ExpectEq<std::string>(str::ReplaceAll("abc", "abc", ""), "", "replace with nothing");
    ExpectEq<std::string>(str::ReplaceAll("none", "z", "y"), "none", "no match");
    ExpectEq<std::string>(str::ReplaceFirst("a-b-c", "-", "+"), "a+b-c", "replace first");
    ExpectEq<std::string>(str::ReplaceFirst("abc", "z", "y"), "abc", "replace first without a match");

    ExpectEq(str::Split("a,b,,c", ','), std::vector<std::string>{"a", "b", "", "c"},
             "split keeps empty parts");
    ExpectEq(str::Split("a,b,,c", ',', true), std::vector<std::string>{"a", "b", "c"},
             "split skips empty parts");
    ExpectEq(str::Split("", ','), std::vector<std::string>{""}, "split of empty input is one empty part");
    ExpectEq(str::Split("", ',', true), std::vector<std::string>{}, "skipping split of empty input is empty");
    ExpectEq(str::Split(",", ','), std::vector<std::string>{"", ""}, "lone separator gives two empty parts");

    std::vector<std::string> seen;
    str::ForEachSplit("x;y", ';', [&](std::string_view part) { seen.emplace_back(part); });
    ExpectEq(seen, std::vector<std::string>{"x", "y"}, "for-each split visits every part");

    ExpectEq<std::string>(str::Join(std::vector<std::string>{"a", "b", "c"}, ", "), "a, b, c", "join");
    ExpectEq<std::string>(str::Join(std::vector<std::string>{}, ","), "", "join of nothing");
    ExpectEq<std::string>(str::Join(std::vector<std::string_view>{"solo"}, ","), "solo", "join of one view");

    ExpectEq<std::string>(str::Cat("n=", 7, " ", 1.5f > 1.0f, 'c', std::string_view(" tail")), "n=7 1c tail",
                          "cat mixes strings, integers, bool and char");
    ExpectEq<std::string>(str::Key("a", 2, 'c'), "a|2|c", "key separates with a bar");

    int i = 0;
    Expect(str::Parse(" 42 ", i) && i == 42, "parse trims");
    Expect(!str::Parse("4 2", i), "parse rejects inner whitespace");
    Expect(!str::Parse("12x", i), "parse rejects trailing junk");
    Expect(!str::Parse("", i), "parse rejects empty input");
    unsigned char small = 0;
    Expect(!str::Parse("300", small), "parse rejects overflow");

    uint32_t hex = 0;
    Expect(str::Parse("ff", hex, 16) && hex == 255, "parse hex");
    Expect(!str::Parse("fg", hex, 16), "parse rejects a bad hex digit");

    Expect(str::ParseDouble("1.5") == 1.5, "parse double");
    Expect(str::ParseDouble("  2") == 2.0, "parse double skips leading space");
    Expect(!str::ParseDouble("1.5x"), "parse double rejects trailing junk");
    Expect(str::ParseDouble("1.5x", false) == 1.5, "parse double prefix mode");
    Expect(!str::ParseDouble(""), "parse double rejects empty input");
    Expect(!str::ParseDouble("abc", false), "parse double prefix mode still needs a number");

    std::printf(failures ? "%d failure(s)\n" : "strings_selftest ok\n", failures);
    return failures ? 1 : 0;
}
