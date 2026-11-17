#include "client/systems/text/FontProviderTables.hpp"

#include "util/JsonValue.hpp"
#include "util/ResourcePaths.hpp"
#include "util/Resources.hpp"

namespace perlin::ui {
    namespace {
        bool ReadTable(const json::Value& root, const char* key, size_t expected, std::vector<uint32_t>& out,
                       std::string& error) {
            const json::Value* list = root.Find(key);
            if (!list || !list->IsArray()) {
                error = std::string("glyph table '") + key + "' is missing";
                return false;
            }
            if (list->array.size() != expected) {
                error = std::string("glyph table '") + key + "' has " + std::to_string(list->array.size()) +
                        " entries, expected " + std::to_string(expected);
                return false;
            }
            out.clear();
            out.reserve(expected);
            for (const json::Value& v : list->array) {
                if (!v.IsNumber() || v.number < 0 || v.number > 0x10FFFF ||
                    v.number != static_cast<double>(static_cast<uint32_t>(v.number))) {
                    error = std::string("glyph table '") + key + "' holds a value that is not a code point";
                    return false;
                }
                out.push_back(static_cast<uint32_t>(v.number));
            }
            return true;
        }
    }

    bool ParseFontProviderTables(std::string_view text, FontProviderTables& out, std::string& error) {
        json::Value root;
        std::string parseError;
        if (!json::Value::Parse(text, root, parseError)) {
            error = "glyph tables are not valid JSON: " + parseError;
            return false;
        }
        FontProviderTables parsed;
        if (!ReadTable(root, "nonlatin", FontProviderTables::kNonlatinCells, parsed.nonlatin, error) ||
            !ReadTable(root, "accented", FontProviderTables::kAccentedCells, parsed.accented, error))
            return false;
        out = std::move(parsed);
        return true;
    }

    bool LoadFontProviderTables(FontProviderTables& out, std::string& error) {
        const std::string text = resources::ReadText(resources::path::kFontGlyphTables);
        if (text.empty()) {
            error = "resource missing: " + std::string(resources::path::kFontGlyphTables);
            return false;
        }
        return ParseFontProviderTables(text, out, error);
    }
}
