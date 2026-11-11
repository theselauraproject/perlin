#include "client/resources/PackInfo.hpp"

#include "client/ui/Json.hpp"

#include "util/ZipFile.hpp"
#include "util/Strings.hpp"

#include <miniz.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace perlin {
    namespace fs = std::filesystem;

    namespace {

        void StripBom(std::string& text) {
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
                text.erase(0, 3);
        }

        struct Source {
            bool zip = false;
            fs::path folder;
            mz_zip_archive archive{};
            FILE* file = nullptr;
            bool open = false;
            std::string prefix;

            ~Source() {
                if (open)
                    mz_zip_reader_end(&archive);
                if (file)
                    std::fclose(file);
            }

            bool Read(const std::string& rel, std::string& out) {
                if (!zip) {
                    std::ifstream in(folder / rel, std::ios::binary);
                    if (!in)
                        return false;
                    out.assign(std::istreambuf_iterator<char>(in), {});
                    StripBom(out);
                    return true;
                }
                const int index = mz_zip_reader_locate_file(&archive, (prefix + rel).c_str(), nullptr, 0);
                if (index < 0)
                    return false;
                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&archive, static_cast<mz_uint>(index), &size, 0);
                if (!data)
                    return false;
                out.assign(static_cast<const char*>(data), size);
                mz_free(data);
                StripBom(out);
                return true;
            }
        };

        void Flatten(const ui::Json& node, std::string& out, const std::string& inheritedCode) {
            static const std::pair<const char*, char> kColors[] = {
                {"black", '0'},     {"dark_blue", '1'},    {"dark_green", '2'}, {"dark_aqua", '3'},
                {"dark_red", '4'},  {"dark_purple", '5'},  {"gold", '6'},       {"gray", '7'},
                {"dark_gray", '8'}, {"blue", '9'},         {"green", 'a'},      {"aqua", 'b'},
                {"red", 'c'},       {"light_purple", 'd'}, {"yellow", 'e'},     {"white", 'f'}};
            if (node.IsString()) {
                out += node.string;
                return;
            }
            if (node.IsArray()) {
                for (const ui::Json& item : node.array)
                    Flatten(item, out, inheritedCode);
                return;
            }
            if (!node.IsObject())
                return;
            std::string code = inheritedCode;
            if (const ui::Json* color = node.Find("color"))
                for (const auto& [name, letter] : kColors)
                    if (color->StringOr("") == name)
                        code = std::string("\xC2\xA7") + letter;
            std::string styles;
            for (const auto& [key, letter] :
                 {std::pair<const char*, char>{"bold", 'l'}, {"italic", 'o'}, {"obfuscated", 'k'}})
                if (const ui::Json* flag = node.Find(key);
                    flag && flag->type == ui::Json::Type::Bool && flag->boolean)
                    styles += std::string("\xC2\xA7") + letter;
            const bool formatted = !code.empty() || !styles.empty();
            if (formatted)
                out += code + styles;
            if (const ui::Json* text = node.Find("text"))
                out += text->StringOr("");
            if (const ui::Json* extra = node.Find("extra"))
                Flatten(*extra, out, code);
            if (formatted)
                out += "\xC2\xA7r";
        }

        std::string TrimExt(std::string name) {
            const size_t dot = name.rfind('.');
            return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
        }
    }

    PackMeta ReadPackMeta(const std::string& path) {
        PackMeta meta;
        const fs::path p(path);
        meta.name = p.filename().string();
        Source src;
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            src.folder = p;
        } else {
            if (!OpenZipReader(src.archive, p, src.file))
                return meta;
            src.zip = true;
            src.open = true;
            const mz_uint count = mz_zip_reader_get_num_files(&src.archive);
            size_t bestDepth = std::string::npos;
            for (mz_uint i = 0; i < count; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&src.archive, i, &st))
                    continue;
                std::string file = st.m_filename;
                std::replace(file.begin(), file.end(), '\\', '/');
                const std::string lower = str::Lower(file);
                for (const char* marker : {"manifest.json", "pack.mcmeta", "pack.json"}) {
                    const std::string m = marker;
                    if (lower.size() < m.size() || lower.compare(lower.size() - m.size(), m.size(), m) != 0)
                        continue;
                    if (lower.size() > m.size() && lower[lower.size() - m.size() - 1] != '/')
                        continue;
                    const size_t depth = static_cast<size_t>(std::count(lower.begin(), lower.end(), '/'));
                    if (depth < bestDepth && depth <= 2) {
                        bestDepth = depth;
                        src.prefix = file.substr(0, file.size() - m.size());
                    }
                }
            }
        }
        std::string text;
        if (src.Read("manifest.json", text)) {
            ui::Json json;
            std::string error;
            if (ui::Json::Parse(text, json, error))
                if (const ui::Json* header = json.Find("header")) {
                    if (const ui::Json* n = header->Find("name"))
                        meta.name = n->StringOr(meta.name);
                    if (const ui::Json* d = header->Find("description"))
                        meta.description = d->StringOr("");
                    if (const ui::Json* v = header->Find("version"))
                        if (v->IsArray()) {
                            for (size_t i = 0; i < v->array.size(); i++)
                                meta.version += (i ? "." : "") +
                                                std::to_string(static_cast<int>(v->array[i].NumberOr(0)));
                        }
                }
        } else if (src.Read("pack.json", text)) {
            ui::Json json;
            std::string error;
            if (ui::Json::Parse(text, json, error)) {
                if (const ui::Json* n = json.Find("name"))
                    meta.name = n->StringOr(meta.name);
                if (const ui::Json* d = json.Find("description"))
                    meta.description = d->StringOr("");
            }
        } else if (src.Read("pack.mcmeta", text)) {
            ui::Json json;
            std::string error;
            if (ui::Json::Parse(text, json, error))
                if (const ui::Json* pack = json.Find("pack"))
                    if (const ui::Json* d = pack->Find("description"))
                        Flatten(*d, meta.description, "");
        }
        if (meta.name.empty() || meta.name.contains("pack.name"))
            meta.name = TrimExt(p.filename().string());
        for (const char* icon : {"pack_icon.png", "pack.png", "icon.png"}) {
            std::string bytes;
            if (src.Read(icon, bytes) &&
                LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(),
                                  meta.icon)) {
                meta.hasIcon = true;
                break;
            }
        }
        return meta;
    }
}
