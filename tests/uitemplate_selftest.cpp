#include "client/ui/UiTemplate.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using perlin::ui::LoadUiTemplate;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                         \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static std::string Slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void Spit(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

int main() {
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
