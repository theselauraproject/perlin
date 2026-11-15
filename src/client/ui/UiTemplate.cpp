#include "client/ui/UiTemplate.hpp"

#include "util/Paths.hpp"
#include "util/Resources.hpp"
#include "util/FileIo.hpp"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#include "util/Log.hpp"

namespace perlin::ui {
    namespace fs = std::filesystem;

    void ReplaceAll(std::string& text, const std::string& key, const std::string& value) {
        const std::string needle = "{{" + key + "}}";
        for (size_t at = text.find(needle); at != std::string::npos;
             at = text.find(needle, at + value.size()))
            text.replace(at, needle.size(), value);
    }

    std::string ReadMarkup(const std::string& resourceName) {
        constexpr std::string_view kInclude = "{{include:";
        std::string text = resources::ReadRequiredText(resourceName);
        for (size_t at = text.find(kInclude); at != std::string::npos; at = text.find(kInclude, at)) {
            const size_t end = text.find("}}", at);
            if (end == std::string::npos)
                break;
            std::string name = text.substr(at + kInclude.size(), end - at - kInclude.size());
            std::vector<std::pair<std::string, std::string>> params;
            for (size_t bar = name.find('|'); bar != std::string::npos; bar = name.find('|')) {
                const size_t next = name.find('|', bar + 1);
                const std::string pair = name.substr(bar + 1, next == std::string::npos ? next : next - bar - 1);
                if (const size_t eq = pair.find('='); eq != std::string::npos)
                    params.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
                name.erase(bar, next == std::string::npos ? std::string::npos : next - bar);
            }
            const size_t lineStart = text.rfind('\n', at);
            const std::string indent =
                text.substr(lineStart == std::string::npos ? 0 : lineStart + 1,
                            at - (lineStart == std::string::npos ? 0 : lineStart + 1));
            std::string included = resources::ReadRequiredText(name);
            for (const auto& [key, value] : params)
                ReplaceAll(included, key, value);
            std::string indented;
            for (size_t pos = 0; pos < included.size();) {
                const size_t nl = included.find('\n', pos);
                const std::string line = included.substr(pos, nl == std::string::npos ? nl : nl - pos);
                if (!indented.empty() && !line.empty())
                    indented += indent;
                indented += line;
                if (nl == std::string::npos)
                    break;
                indented += '\n';
                pos = nl + 1;
            }
            while (!indented.empty() && (indented.back() == '\n' || indented.back() == ' '))
                indented.pop_back();
            text.replace(at, end + 2 - at, indented);
            at += indented.size();
        }
        return text;
    }

    namespace {
        constexpr const char* kHeaderPrefix = "<!--perlin-ui-src:";

        uint64_t Fnv1a(const std::string& s) {
            uint64_t h = 1469598103934665603ull;
            for (const unsigned char c : s)
                h = (h ^ c) * 1099511628211ull;
            return h;
        }

        std::string Header(uint64_t hash) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s%016" PRIx64 "-->\n", kHeaderPrefix, hash);
            return buf;
        }

        bool SplitHeader(const std::string& content, uint64_t& hash, std::string& body) {
            const size_t prefix = std::char_traits<char>::length(kHeaderPrefix);
            if (content.compare(0, prefix, kHeaderPrefix) != 0)
                return false;
            const size_t nl = content.find('\n');
            if (nl == std::string::npos)
                return false;
            hash = std::strtoull(content.c_str() + prefix, nullptr, 16);
            body = content.substr(nl + 1);
            return true;
        }
    }

    std::string LoadUiTemplate(const std::string& dataDir, const std::string& fileName,
                               const std::string& defaultText, uint64_t& version) {
        const fs::path file = fs::path(dataDir) / "ui" / fs::path(fileName).filename();
        const uint64_t builtIn = Fnv1a(defaultText);
        std::error_code ec;

        bool rewrite = true;
        std::string content, body;
        if (fs::exists(file, ec) && file::Read(file, content) && !content.empty()) {
            uint64_t fileHash = 0;
            if (SplitHeader(content, fileHash, body)) {
                if (fileHash == builtIn) {
                    rewrite = false;
                } else if (Fnv1a(body) != fileHash) {
                    fs::copy_file(file, fs::path(file).concat(".old"), fs::copy_options::overwrite_existing,
                                  ec);
                    Log("ui", "{} changed in this build; your edited copy was saved as {}.old", fileName,
                        fileName);
                }
            } else if (content != defaultText) {
                fs::copy_file(file, fs::path(file).concat(".old"), fs::copy_options::overwrite_existing, ec);
            }
        }

        if (rewrite) {
            fs::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << Header(builtIn) << defaultText;
            out.close();
            body = defaultText;
        }

        const auto stamp = fs::last_write_time(file, ec);
        if (ec || body.empty()) {
            version = 0;
            return defaultText;
        }
        version = static_cast<uint64_t>(stamp.time_since_epoch().count());
        return body;
    }

    std::string ExpandSceneTokens(std::string text) {
        constexpr std::string_view kToken = "{{scene:";
        static const std::string tree = resources::ReadRequiredText("ui/components/scene_tree.rml");
        static const std::string sceneStyle = resources::ReadRequiredText("ui/styles/scene.rcss");
        if (text.find(kToken) != std::string::npos)
            if (const size_t style = text.find("</style>"); style != std::string::npos)
                text.insert(style, sceneStyle);
        for (size_t at = text.find(kToken); at != std::string::npos; at = text.find(kToken, at)) {
            const size_t end = text.find("}}", at);
            if (end == std::string::npos)
                break;
            const std::string name = text.substr(at + kToken.size(), end - at - kToken.size());
            std::string expanded = tree;
            ReplaceAll(expanded, "list", name);
            text.replace(at, end + 2 - at, expanded);
            at += expanded.size();
        }
        return text;
    }

    const std::string& BuiltinTemplate(const std::string& resourceName) {
        static std::mutex mutex;
        static std::unordered_map<std::string, std::string> cache;
        std::lock_guard lock(mutex);
        auto it = cache.find(resourceName);
        if (it == cache.end())
            it = cache.emplace(resourceName, ReadMarkup(resourceName)).first;
        return it->second;
    }

    std::string ScreenTemplate(const std::string& fileName) {
        struct Entry {
            std::string text;
            std::chrono::steady_clock::time_point checked;
        };
        static std::mutex mutex;
        static std::unordered_map<std::string, Entry> cache;
        const std::string& builtin = BuiltinTemplate(fileName);
        std::lock_guard lock(mutex);
        Entry& e = cache[fileName];
        const auto now = std::chrono::steady_clock::now();
        if (e.text.empty() || now - e.checked > std::chrono::seconds(1)) {
            e.checked = now;
            uint64_t version = 0;
            e.text = LoadUiTemplate(paths::DataDir(), fileName, builtin, version);
        }
        return e.text;
    }

    namespace {
        std::vector<std::string>& Registry() {
            static std::vector<std::string> registry;
            return registry;
        }
    }

    ScreenTemplateDefault::ScreenTemplateDefault(const char* fileName) {
        Registry().emplace_back(fileName);
    }

    void ExportScreenTemplates() {
        for (const std::string& fileName : Registry())
            ScreenTemplate(fileName);
    }

    bool TemplateSlot::Refresh(const char* fileName, const std::string& defaultText,
                               std::chrono::milliseconds every, std::chrono::steady_clock::time_point now,
                               const char* logLabel) {
        if (now - checkAt <= every && !text.empty())
            return false;
        checkAt = now;
        uint64_t latest = 0;
        std::string loaded = LoadUiTemplate(paths::DataDir(), fileName, defaultText, latest);
        if (latest == version && !text.empty())
            return false;
        version = latest;
        text = std::move(loaded);
        if (logLabel)
            Log("ui", "{}: loaded {}/ui/{}", logLabel, paths::DataDir(), fileName);
        return true;
    }
}
