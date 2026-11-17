#include "client/platform/EmbeddedProvider.hpp"
#include "client/systems/entity/CapeGeometry.hpp"
#include "client/systems/text/FontProviderTables.hpp"
#include "util/JsonValue.hpp"
#include "util/ResourcePaths.hpp"
#include "util/Resources.hpp"

#include <cstdio>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
using namespace perlin;

namespace {
    int failures = 0;

    void Check(bool ok, const std::string& what) {
        if (!ok) {
            failures++;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    std::string Slurp(const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), {}};
    }

    std::string LegacyRawString(const std::string& inc, const std::string& name) {
        const std::regex re(name + R"re( = R"CAPE\(([\s\S]*?)\)CAPE")re");
        std::smatch m;
        return std::regex_search(inc, m, re) ? m[1].str() : std::string();
    }

    std::vector<uint32_t> LegacyTable(const std::string& inc, const std::string& name) {
        const size_t at = inc.find(name);
        const size_t open = inc.find('{', at);
        const size_t close = inc.find("};", open);
        std::vector<uint32_t> out;
        const std::regex hex("0x[0-9A-Fa-f]+");
        const std::string body = inc.substr(open, close - open);
        for (auto it = std::sregex_iterator(body.begin(), body.end(), hex); it != std::sregex_iterator(); ++it)
            out.push_back(static_cast<uint32_t>(std::stoul(it->str(), nullptr, 16)));
        return out;
    }

    std::string Canonical(const std::string& text) {
        json::Value v;
        std::string err;
        if (!json::Value::Parse(text, v, err))
            return "<invalid: " + err + ">";
        return v.Dump();
    }
}

int main(int argc, char** argv) {
    if (argc < 3)
        return 2;
    const fs::path resourcesRoot = argv[1], legacyRoot = argv[2];
    RegisterEmbeddedResources();

    const std::string capeInc = Slurp(legacyRoot / "CapeGeometry.inc");
    const std::string fontInc = Slurp(legacyRoot / "FontProviderTables.inc");
    Check(!capeInc.empty() && !fontInc.empty(), "legacy fixtures readable");

    {
        const std::string plainOld = LegacyRawString(capeInc, "kNoShoulder");
        const std::string shoulderOld = LegacyRawString(capeInc, "kShoulder");
        Check(!plainOld.empty() && !shoulderOld.empty(), "legacy cape strings extracted");
        const std::string plainNew = resources::ReadText(resources::path::kCapeGeometryPlain);
        const std::string shoulderNew = resources::ReadText(resources::path::kCapeGeometryShoulder);
        Check(!plainNew.empty() && !shoulderNew.empty(), "cape geometry embedded and readable");
        Check(Canonical(plainOld) == Canonical(plainNew), "plain cape geometry equals original data");
        Check(Canonical(shoulderOld) == Canonical(shoulderNew), "shoulder cape geometry equals original data");
        Check(Slurp(resourcesRoot / resources::path::kCapeGeometryPlain) == plainNew,
              "embedded plain cape equals source file");

        attachable::GeometryLibrary library;
        std::string error;
        Check(entity::LoadCapeGeometries(library, error), "cape geometries load: " + error);
        const attachable::Geometry* plain = library.Find(entity::kCapeGeometryPlainId);
        const attachable::Geometry* shoulder = library.Find(entity::kCapeGeometryShoulderId);
        Check(plain && shoulder, "both cape geometries registered");
        if (plain) {
            Check(plain->textureWidth == 64 && plain->textureHeight == 32, "plain cape texture size preserved");
            Check(!plain->bones.empty() && plain->bones.size() >= 5, "plain cape hierarchy preserved");
        }

        attachable::GeometryLibrary rejected;
        Check(!entity::LoadCapeGeometryText(rejected, "{", entity::kCapeGeometryPlainId, error),
              "malformed cape JSON rejected");
        Check(!entity::LoadCapeGeometryText(rejected, "{\"minecraft:geometry\":[]}", entity::kCapeGeometryPlainId,
                                            error),
              "empty geometry list rejected");
        Check(!entity::LoadCapeGeometryText(rejected, shoulderNew, entity::kCapeGeometryPlainId, error),
              "geometry missing the expected identifier rejected");
    }
    {
        const std::vector<uint32_t> nonlatinOld = LegacyTable(fontInc, "kNonlatinChars");
        const std::vector<uint32_t> accentedOld = LegacyTable(fontInc, "kAccentedChars");
        ui::FontProviderTables tables;
        std::string error;
        Check(ui::LoadFontProviderTables(tables, error), "glyph tables load: " + error);
        Check(nonlatinOld.size() == ui::FontProviderTables::kNonlatinCells, "legacy nonlatin size");
        Check(accentedOld.size() == ui::FontProviderTables::kAccentedCells, "legacy accented size");
        Check(tables.nonlatin == nonlatinOld, "nonlatin glyph table equals original data");
        Check(tables.accented == accentedOld, "accented glyph table equals original data");

        ui::FontProviderTables scratch;
        Check(!ui::ParseFontProviderTables("not json", scratch, error), "non-JSON glyph tables rejected");
        Check(!ui::ParseFontProviderTables("{\"nonlatin\":[1],\"accented\":[]}", scratch, error),
              "wrong-size glyph table rejected");
        std::string bad = resources::ReadText(resources::path::kFontGlyphTables);
        const size_t at = bad.find("[") + 1;
        bad.replace(at, 0, "-5, ");
        Check(!ui::ParseFontProviderTables(bad, scratch, error), "negative code point rejected");
    }
    if (failures == 0)
        std::puts("asset_data_test: ok");
    return failures == 0 ? 0 : 1;
}
