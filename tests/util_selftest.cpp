#include "util/Angles.hpp"
#include "util/FileIo.hpp"
#include "util/JsonText.hpp"
#include "util/JsonValue.hpp"
#include "util/Strings.hpp"
#include "util/TextMetrics.hpp"
#include "util/Utf8.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>

using namespace perlin;

namespace {
    int failures = 0;

    void Expect(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    bool Near(float a, float b) {
        return std::fabs(a - b) < 1e-3f;
    }

    std::string Encode(char32_t cp) {
        std::string out;
        AppendUtf8(out, cp);
        return out;
    }

    static_assert(text::DefaultTextWidth("") == 0);
    static_assert(text::DefaultTextWidth("i") == 2);
    static_assert(text::DefaultTextWidth("l`") == 6);
    static_assert(text::DefaultTextWidth("It[] ") == 20);
    static_assert(text::DefaultTextWidth("fk<>{}()*\"") == 50);
    static_assert(text::DefaultTextWidth("@~") == 14);
    static_assert(text::DefaultTextWidth("a") == 6);
    static_assert(text::DefaultTextWidth("\xC3\xA9") == 6);
    static_assert(text::DefaultTextWidth("\xE2\x82\xAC") == 6);
    static_assert(str::StripAnyNamespace("minecraft:stone") == "stone");
    static_assert(str::StripAnyNamespace("custom:stone") == "stone");
    static_assert(str::StripAnyNamespace("stone") == "stone");
    static_assert(str::StripAnyNamespace("").empty());
    static_assert(str::StripAnyNamespace("a:b:c") == "b:c");

    void TestUtf8() {
        Expect(Encode(0) == std::string(1, '\0'), "utf8 nul");
        Expect(Encode(0x7F) == "\x7F", "utf8 0x7f");
        Expect(Encode(0x80) == "\xC2\x80", "utf8 0x80");
        Expect(Encode(0x7FF) == "\xDF\xBF", "utf8 0x7ff");
        Expect(Encode(0x800) == "\xE0\xA0\x80", "utf8 0x800");
        Expect(Encode(0xA7) == "\xC2\xA7", "utf8 section sign");
        Expect(Encode(0x20AC) == "\xE2\x82\xAC", "utf8 euro");
        Expect(Encode(0xFFFF) == "\xEF\xBF\xBF", "utf8 0xffff");
        Expect(Encode(0x10000) == "\xF0\x90\x80\x80", "utf8 0x10000");
        Expect(Encode(0x1F600) == "\xF0\x9F\x98\x80", "utf8 emoji");
        Expect(Encode(0x10FFFF) == "\xF4\x8F\xBF\xBF", "utf8 max");
        std::string all;
        for (char32_t cp : {0x24u, 0xA2u, 0x20ACu, 0x10348u})
            AppendUtf8(all, cp);
        Expect(all == "\x24\xC2\xA2\xE2\x82\xAC\xF0\x90\x8D\x88", "utf8 append accumulates");
        Expect(IsValidUtf8(all), "utf8 output is valid");
    }

    void TestJsonText() {
        Expect(json::Escape("").empty(), "escape empty");
        Expect(json::Quote("") == "\"\"", "quote empty");
        Expect(json::Escape("plain") == "plain", "escape plain");
        Expect(json::Escape("a\"b\\c") == "a\\\"b\\\\c", "escape quote backslash");
        Expect(json::Escape("\n\r\t") == "\\n\\r\\t", "escape whitespace controls");
        Expect(json::Escape(std::string("\x01\x1f", 2)) == "\\u0001\\u001f", "escape low controls");
        Expect(json::Escape(std::string(1, '\0')) == "\\u0000", "escape nul");
        Expect(json::Escape("\x7F") == "\x7F", "escape del untouched");
        Expect(json::Escape("\xC3\xA9\xE2\x82\xAC") == "\xC3\xA9\xE2\x82\xAC", "escape keeps utf8");
        Expect(json::Quote("x\"y") == "\"x\\\"y\"", "quote escapes");
        std::string appended = "k=";
        json::AppendEscaped(appended, "\"");
        Expect(appended == "k=\\\"", "append escaped");
        std::string every;
        for (int c = 0; c < 0x20; c++)
            every += static_cast<char>(c);
        every += "\"\\end";
        json::Value parsed;
        std::string error;
        Expect(json::Value::Parse(json::Quote(every), parsed, error) && parsed.IsString() && parsed.string == every,
               "quote round trips through the parser");
    }

    void TestStripFormatting() {
        Expect(str::StripFormatting("").empty(), "strip empty");
        Expect(str::StripFormatting("plain") == "plain", "strip plain");
        Expect(str::StripFormatting("\xC2\xA7" "aHi") == "Hi", "strip code");
        Expect(str::StripFormatting("\xC2\xA7" "aHi \xC2\xA7r ") == "Hi  ", "strip keeps surrounding space");
        Expect(str::StripFormatting("\xC2\xA7" "a") .empty(), "strip trailing code");
        Expect(str::StripFormatting("\xC2\xA7").empty(), "strip lone section sign");
        Expect(str::StripFormatting("\xC2").size() == 1, "strip lone lead byte kept");
        Expect(str::StripFormatting("\xC2\xA7" "a\xC2\xA7" "bX") == "X", "strip consecutive codes");
        Expect(str::StripFormatting("\xE2\x80\xA7x") == "\xE2\x80\xA7x", "strip leaves other 0xA7 continuation");
        Expect(str::StripFormatting("\xA7" "aX") == "\xA7" "aX", "latin1 not stripped by default");
        Expect(str::StripFormatting("\xA7" "aX", str::SectionEncoding::Utf8OrLatin1) == "X", "latin1 stripped on request");
        Expect(str::StripFormatting("\xC2\xA7" "bY", str::SectionEncoding::Utf8OrLatin1) == "Y", "utf8 stripped in latin1 mode");
        Expect(str::Trim(str::StripFormatting("\xC2\xA7" "aHi \xC2\xA7r ")) == "Hi", "strip then trim");
    }

    void TestTextMetrics() {
        Expect(text::DefaultTextWidth("Hello") == 6 + 6 + 3 + 3 + 6, "width hello");
        Expect(text::DefaultTextWidth("Zoom -") == 6 * 3 + 6 + 4 + 6, "width zoom");
        Expect(text::DefaultTextWidth("\xC2\xA7") == 6, "width section sign is one glyph");
        const std::string wide = Encode(0x1F600);
        Expect(text::DefaultTextWidth(wide) == 6, "width counts one glyph for four bytes");
    }

    void TestAngles() {
        Expect(Near(math::WrapDegrees(0), 0), "wrap zero");
        Expect(Near(math::WrapDegrees(90), 90), "wrap 90");
        Expect(Near(math::WrapDegrees(-90), -90), "wrap -90");
        Expect(Near(math::WrapDegrees(180), -180), "wrap 180");
        Expect(Near(math::WrapDegrees(-180), -180), "wrap -180");
        Expect(Near(math::WrapDegrees(181), -179), "wrap 181");
        Expect(Near(math::WrapDegrees(-181), 179), "wrap -181");
        Expect(Near(math::WrapDegrees(540), -180), "wrap 540");
        Expect(Near(math::WrapDegrees(-540), -180), "wrap -540");
        Expect(Near(math::WrapDegrees(720), 0), "wrap 720");
        Expect(Near(math::WrapDegrees(3600 + 45), 45), "wrap large");
        Expect(std::isnan(math::WrapDegrees(std::numeric_limits<float>::quiet_NaN())), "wrap nan");
        Expect(std::isnan(math::WrapDegrees(std::numeric_limits<float>::infinity())), "wrap inf");
        for (float a = -1000.0f; a < 1000.0f; a += 7.3f) {
            const float w = math::WrapDegrees(a);
            Expect(w >= -180.0f && w < 180.0f, "wrap range");
            Expect(Near(std::remainder(a - w, 360.0f), 0.0f) || std::fabs(std::remainder(a - w, 360.0f)) < 0.05f,
                   "wrap congruent");
        }
    }

    void TestFileRead() {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "perlin_util_selftest";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const auto path = dir / "a.txt";
        std::string text = "keep";
        Expect(!file::Read(dir / "missing.txt", text), "read missing reports failure");
        Expect(file::Read(dir / "missing.txt").empty(), "read missing text is empty");
        Expect(file::Write(path, std::string("a\r\nb\0c", 6)), "write");
        std::string out;
        Expect(file::Read(path, out) && out == std::string("a\r\nb\0c", 6), "read is binary exact");
        Expect(file::Write(dir / "empty.txt", ""), "write empty");
        Expect(file::Read(dir / "empty.txt", out) && out.empty(), "read empty");
        Expect(!file::Read(dir, out), "read directory fails");
        std::filesystem::remove_all(dir);
    }
}

int main() {
    TestUtf8();
    TestJsonText();
    TestStripFormatting();
    TestTextMetrics();
    TestAngles();
    TestFileRead();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("util_selftest: ok\n");
    return 0;
}
