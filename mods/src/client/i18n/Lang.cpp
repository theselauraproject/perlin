#include "client/i18n/Lang.hpp"

#include "client/i18n/BuiltinEnglish.hpp"
#include "client/ui/Json.hpp"
#include "client/resources/ServerPacks.hpp"
#include "util/Resources.hpp"
#include "util/FileIo.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "util/Log.hpp"

namespace perlin::i18n {
    namespace fs = std::filesystem;

    namespace {

        size_t LoadLayers(const std::string& code, const std::string& root, const std::string& data,
                          std::unordered_map<std::string, std::string>& out) {
            size_t files = 0;
            std::string text;
            // our strings first so bedrock's win on shared keys: menus read like the game
            if (code == "en_US") {
                Lang::Parse(BuiltinEnglish(), out);
                files++;
            }
            if (!root.empty() && file::Read(fs::path(root) / "texts" / (code + ".lang"), text)) {
                Lang::Parse(text, out);
                files++;
            }
            for (const std::string& dir : ServerPacks::Instance().Directories())
                if (file::Read(fs::path(dir) / "texts" / (code + ".lang"), text)) {
                    Lang::Parse(text, out);
                    files++;
                }
            if (!data.empty() && file::Read(fs::path(data) / "lang" / (code + ".lang"), text)) {
                Lang::Parse(text, out);
                files++;
            }
            return files;
        }
    }

    Lang& Lang::Get() {
        static Lang instance;
        return instance;
    }

    void Lang::Parse(const std::string& text, std::unordered_map<std::string, std::string>& out) {
        size_t pos = 0;
        if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF &&
            static_cast<uint8_t>(text[1]) == 0xBB && static_cast<uint8_t>(text[2]) == 0xBF)
            pos = 3;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line.starts_with("##"))
                continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string value = line.substr(eq + 1);
            if (const size_t tab = value.find("\t#"); tab != std::string::npos)
                value.resize(tab);
            out[line.substr(0, eq)] = std::move(value);
        }
    }

    void Lang::Configure(const std::string& resourceRoot, const std::string& dataDir,
                         const std::string& language) {
        std::lock_guard lock(mutex);
        const std::string wanted = language.empty() ? std::string("en_US") : language;
        if (configured && root == resourceRoot && data == dataDir && configuredCode == wanted)
            return;
        configured = true;
        root = resourceRoot;
        data = dataDir;
        configuredCode = wanted;
        code = wanted;
        Load();
    }

    void Lang::Load() {
        primary.clear();
        fallback.clear();
        const size_t fallbackFiles = LoadLayers("en_US", root, data, fallback);
        size_t files = fallbackFiles;
        if (code != "en_US") {
            files = LoadLayers(code, root, data, primary);
            if (files == 0 && !root.empty()) {
                Log("lang", "no strings for {} (texts/{}.lang missing), using en_US", code, code);
                code = "en_US";
            }
        }
        languages.clear();
        std::string text;
        if (!root.empty() && file::Read(fs::path(root) / "texts" / "language_names.json", text)) {
            ui::Json doc;
            std::string error;
            if (ui::Json::Parse(text, doc, error) && doc.IsArray())
                for (const ui::Json& entry : doc.array)
                    if (entry.IsArray() && entry.array.size() >= 2 && entry.array[0].IsString() &&
                        entry.array[1].IsString())
                        languages.push_back({entry.array[0].string, entry.array[1].string});
        }
        if (languages.empty())
            languages.push_back({"en_US", "English (United States)"});
        version++;
        Log("lang", "language {}: {} strings ({} file(s)), {} languages available", code,
            primary.size() + fallback.size(), files, languages.size());
    }

    void Lang::LoadFromText(const std::string& primaryText, const std::string& fallbackText) {
        std::lock_guard lock(mutex);
        primary.clear();
        fallback.clear();
        Parse(primaryText, primary);
        Parse(fallbackText, fallback);
        version++;
    }

    bool Lang::Find(const std::string& key, std::string& out) const {
        std::lock_guard lock(mutex);
        if (const auto it = primary.find(key); it != primary.end()) {
            out = it->second;
            return true;
        }
        if (const auto it = fallback.find(key); it != fallback.end()) {
            out = it->second;
            return true;
        }
        return false;
    }

    bool Lang::Has(const std::string& key) const {
        std::string unused;
        return Find(key, unused);
    }

    std::string Lang::Tr(const std::string& key) const {
        std::string out;
        return Find(key, out) ? out : key;
    }

    std::string Lang::Tr(const std::string& key, const std::vector<std::string>& args) const {
        std::string pattern;
        if (!Find(key, pattern))
            return key;
        return Format(pattern, args);
    }

    std::string Lang::Format(const std::string& pattern, const std::vector<std::string>& args) {
        std::string out;
        size_t next = 0;
        for (size_t i = 0; i < pattern.size(); i++) {
            const char c = pattern[i];
            if (c != '%' || i + 1 >= pattern.size()) {
                out.push_back(c);
                continue;
            }
            const char n = pattern[i + 1];
            if (n == '%') {
                out.push_back('%');
                i++;
            } else if (n == 's' || n == 'd') {
                if (next < args.size())
                    out += args[next];
                next++;
                i++;
            } else if (std::isdigit(static_cast<unsigned char>(n))) {
                size_t j = i + 1;
                size_t index = 0;
                while (j < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[j])))
                    index = index * 10 + static_cast<size_t>(pattern[j++] - '0');
                if (j + 1 < pattern.size() && pattern[j] == '$' &&
                    (pattern[j + 1] == 's' || pattern[j + 1] == 'd')) {
                    if (index >= 1 && index <= args.size())
                        out += args[index - 1];
                    i = j + 1;
                } else if (index >= 1 && index <= args.size()) {
                    out += args[index - 1];
                    i = j - 1;
                } else {
                    out.push_back(c);
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    std::vector<LanguageInfo> Lang::Languages() const {
        std::lock_guard lock(mutex);
        return languages;
    }
}
