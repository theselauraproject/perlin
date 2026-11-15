#include "util/EnvVar.hpp"
#include "client/ui/UiTemplate.hpp"
#include "util/FileIo.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using perlin::ui::LoadUiTemplate;
using perlin::ui::ReadMarkup;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                         \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static std::string Slurp(const fs::path& p) {
    const std::string contents = perlin::file::Read(p);
    return contents;
}

static void Spit(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

static void Write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << text;
}

static void IncludeExpansion() {
    const fs::path res = fs::temp_directory_path() / "perlin_uitemplate_includes";
    fs::remove_all(res);
    Write(res / "a.rml", "<style>\n    .x {\n    }\n    {{include:b.rcss}}\n    .z {\n    }\n</style>\n");
    Write(res / "b.rcss", ".y {\n    top: 1px;\n}\n\n.w {\n    left: 2px;\n}\n");
    Write(res / "c.rml", "<p>{{include:missing_nothing}}</p>");
    perlin::env::Set("PERLIN_RESOURCES", res.string().c_str());
    const std::string out = ReadMarkup("a.rml");
    CHECK(out == "<style>\n    .x {\n    }\n    .y {\n        top: 1px;\n    }\n\n    .w {\n        left: 2px;\n    }\n    .z {\n    }\n</style>\n");
    CHECK(out.find("{{include") == std::string::npos);
    fs::remove_all(res);
}

int main() {
    IncludeExpansion();
    const fs::path dir = fs::temp_directory_path() / "perlin_uitemplate_selftest";
    fs::remove_all(dir);
    const fs::path file = dir / "ui" / "x.rml";
    uint64_t v = 0;

    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v1</p>", v) == "<p>v1</p>");
    CHECK(fs::exists(file));
    CHECK(v != 0);

    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v1</p>", v) == "<p>v1</p>");

    std::string onDisk = Slurp(file);
    onDisk.replace(onDisk.find("v1"), 2, "EDIT");
    Spit(file, onDisk);
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v1</p>", v) == "<p>EDIT</p>");

    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v2</p>", v) == "<p>v2</p>");
    CHECK(fs::exists(dir / "ui" / "x.rml.old"));
    CHECK(Slurp(dir / "ui" / "x.rml.old").contains("EDIT"));

    fs::remove(dir / "ui" / "x.rml.old");
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v3</p>", v) == "<p>v3</p>");
    CHECK(!fs::exists(dir / "ui" / "x.rml.old"));

    Spit(file, "<p>legacy</p>");
    CHECK(LoadUiTemplate(dir.string(), "x.rml", "<p>v3</p>", v) == "<p>v3</p>");
    CHECK(fs::exists(dir / "ui" / "x.rml.old"));

    fs::remove_all(dir);
    if (failures == 0)
        std::printf("uitemplate_selftest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
