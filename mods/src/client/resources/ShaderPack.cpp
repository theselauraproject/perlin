#include "client/resources/ShaderPack.hpp"
#include "client/ui/Json.hpp"
#include "client/resources/BundledShaderPack.hpp"
#include "util/Strings.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <set>
#include <unordered_map>
#include "util/Log.hpp"

namespace perlin {
    namespace fs = std::filesystem;

    namespace {
        const char* const kBackends[] = {"dxbc", "glsl", "spirv", "essl", "metal"};

        bool IsBackend(const std::string& s) {
            for (const char* b : kBackends)
                if (s == b)
                    return true;
            return false;
        }

        bool FolderHasBackend(const fs::path& dir) {
            std::error_code ec;
            for (const char* b : kBackends)
                if (fs::is_directory(dir / b, ec))
                    return true;
            return false;
        }

        std::string FindFolderRoot(const fs::path& dir) {
            std::error_code ec;
            if (FolderHasBackend(dir))
                return dir.generic_string();
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_directory(ec)) {
                    if (FolderHasBackend(entry.path()))
                        return entry.path().generic_string();
                    for (const auto& inner : fs::directory_iterator(entry.path(), ec))
                        if (inner.is_directory(ec) && FolderHasBackend(inner.path()))
                            return inner.path().generic_string();
                }
            return {};
        }

        std::string JsonString(const std::string& text, const std::string& key) {
            const size_t k = text.find("\"" + key + "\"");
            if (k == std::string::npos)
                return {};
            const size_t colon = text.find(':', k);
            const size_t open = colon == std::string::npos ? colon : text.find('"', colon);
            if (open == std::string::npos)
                return {};
            std::string out;
            for (size_t i = open + 1; i < text.size() && text[i] != '"'; i++) {
                if (text[i] == '\\' && i + 1 < text.size())
                    i++;
                out += text[i];
            }
            return out;
        }
    }

    struct ShaderPack::Impl {
        bool zipMode = false;
        bool bundled = false;
        std::string folder;
        mz_zip_archive zip{};
        bool zipOpen = false;
        std::unordered_map<std::string, mz_uint> index;
        std::set<std::string> backends;
        std::string label;
        std::string meta;
        mutable std::mutex mutex;

        void Reset() {
            if (zipOpen)
                mz_zip_reader_end(&zip);
            zip = {};
            zipOpen = false;
            zipMode = false;
            bundled = false;
            folder.clear();
            index.clear();
            backends.clear();
            label.clear();
            meta.clear();
        }
    };

    ShaderPack::ShaderPack() : impl(std::make_unique<Impl>()) {}
    ShaderPack::~ShaderPack() {
        impl->Reset();
    }

    ShaderPack& ShaderPack::Instance() {
        static ShaderPack pack;
        return pack;
    }

    void ShaderPack::Close() {
        std::lock_guard lock(impl->mutex);
        impl->Reset();
    }

    bool ShaderPack::Active() const {
        std::lock_guard lock(impl->mutex);
        return !impl->backends.empty();
    }

    bool ShaderPack::Bundled() const {
        std::lock_guard lock(impl->mutex);
        return impl->bundled;
    }

    bool ShaderPack::HasBackend(const std::string& backend) const {
        std::lock_guard lock(impl->mutex);
        return impl->backends.contains(backend);
    }

    std::string ShaderPack::Description() const {
        std::lock_guard lock(impl->mutex);
        return impl->label;
    }

    bool ShaderPack::Open(const std::string& packsDir, const std::string& name, std::string& error) {
        std::lock_guard lock(impl->mutex);
        impl->Reset();
        error.clear();
        if (name.empty())
            return false;
        std::error_code ec;
        fs::path source = fs::path(packsDir) / name;
        if (!fs::exists(source, ec) && fs::exists(source.string() + ".zip", ec))
            source = source.string() + ".zip";
        if (!fs::exists(source, ec) && IsBundledName(name)) {
            impl->bundled = true;
            for (const char* b : kBackends) {
                const uint8_t* data = nullptr;
                size_t size = 0;
                if (BundledShaderBytes(b, "vs_terrain", data, size))
                    impl->backends.insert(b);
            }
            if (impl->backends.empty()) {
                error = "the bundled Vibrant Visuals pack has no shaders for this renderer";
                impl->Reset();
                return false;
            }
            impl->meta = BundledShaderMeta();
            impl->label = kBundledPackName;
            Log("shaders", "opened bundled pack {}", kBundledPackName);
            return true;
        }
        if (!fs::exists(source, ec)) {
            error = "no such shader pack: " + source.generic_string();
            return false;
        }
        std::string meta;
        if (fs::is_directory(source, ec)) {
            impl->folder = FindFolderRoot(source);
            if (impl->folder.empty()) {
                error = "no backend folders (dxbc/glsl/spirv/...) inside " + source.generic_string();
                return false;
            }
            for (const char* b : kBackends) {
                bool any = false;
                for (const auto& f : fs::directory_iterator(fs::path(impl->folder) / b, ec))
                    if (f.path().extension() == ".bin") {
                        any = true;
                        break;
                    }
                if (any)
                    impl->backends.insert(b);
            }
            std::ifstream in(impl->folder + "/pack.json");
            meta.assign(std::istreambuf_iterator<char>(in), {});
        } else {
            if (!mz_zip_reader_init_file(&impl->zip, source.string().c_str(), 0)) {
                error = "not a readable zip archive: " + source.generic_string();
                impl->zip = {};
                return false;
            }
            impl->zipOpen = true;
            impl->zipMode = true;
            std::vector<std::pair<std::string, mz_uint>> entries;
            std::string prefix;
            bool found = false;
            const mz_uint count = mz_zip_reader_get_num_files(&impl->zip);
            for (mz_uint i = 0; i < count; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&impl->zip, i, &st) ||
                    mz_zip_reader_is_file_a_directory(&impl->zip, i))
                    continue;
                std::string path = st.m_filename;
                std::replace(path.begin(), path.end(), '\\', '/');
                const std::string lower = str::Lower(path);
                if (!found && lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".bin") == 0) {
                    const size_t slash = lower.rfind('/');
                    if (slash != std::string::npos) {
                        const size_t dirStart = lower.rfind('/', slash - 1);
                        const size_t from = dirStart == std::string::npos ? 0 : dirStart + 1;
                        if (IsBackend(lower.substr(from, slash - from))) {
                            prefix = lower.substr(0, from);
                            found = true;
                        }
                    }
                }
                entries.emplace_back(lower, i);
            }
            if (!found) {
                error = "no backend folders (dxbc/glsl/spirv/...) inside " + source.generic_string();
                impl->Reset();
                return false;
            }
            for (auto& [path, idx] : entries) {
                if (path.compare(0, prefix.size(), prefix) != 0)
                    continue;
                const std::string rel = path.substr(prefix.size());
                impl->index.emplace(rel, idx);
                const size_t slash = rel.find('/');
                if (slash != std::string::npos && IsBackend(rel.substr(0, slash)) && rel.size() > 4 &&
                    rel.compare(rel.size() - 4, 4, ".bin") == 0)
                    impl->backends.insert(rel.substr(0, slash));
            }
            if (const auto it = impl->index.find("pack.json"); it != impl->index.end()) {
                size_t size = 0;
                if (void* data = mz_zip_reader_extract_to_heap(&impl->zip, it->second, &size, 0)) {
                    meta.assign(static_cast<const char*>(data), size);
                    mz_free(data);
                }
            }
        }
        impl->meta = meta;
        std::string title = JsonString(meta, "name");
        impl->label = title.empty() ? name : title;
        const std::string desc = JsonString(meta, "description");
        std::cout << "[shaders] opened pack " << impl->label << (desc.empty() ? "" : " - " + desc)
                  << " (backends:";
        for (const std::string& b : impl->backends)
            std::cout << ' ' << b;
        std::cout << ")" << std::endl;
        return true;
    }

    namespace {
        std::string RawValue(const std::string& text, const std::string& key) {
            const size_t k = text.find("\"" + key + "\"");
            if (k == std::string::npos)
                return {};
            size_t i = text.find(':', k);
            if (i == std::string::npos)
                return {};
            i++;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                i++;
            size_t j = i;
            while (j < text.size() && text[j] != ',' && text[j] != '}' && text[j] != '\n' && text[j] != ' ')
                j++;
            return text.substr(i, j - i);
        }
    }

    bool ShaderPack::Feature(const std::string& name) const {
        std::lock_guard lock(impl->mutex);
        return RawValue(impl->meta, name) == "true";
    }

    bool ShaderPack::Flag(const std::string& name, bool fallback) const {
        std::lock_guard lock(impl->mutex);
        const std::string v = RawValue(impl->meta, name);
        return v == "true" ? true : (v == "false" ? false : fallback);
    }

    double ShaderPack::Number(const std::string& name, double fallback) const {
        std::lock_guard lock(impl->mutex);
        const std::string v = RawValue(impl->meta, name);
        if (v.empty())
            return fallback;
        char* end = nullptr;
        const double d = std::strtod(v.c_str(), &end);
        return end == v.c_str() ? fallback : d;
    }

    std::vector<ShaderSetting> ShaderPack::Settings() const {
        std::string meta;
        {
            std::lock_guard lock(impl->mutex);
            meta = impl->meta;
        }
        std::vector<ShaderSetting> out;
        ui::Json root;
        std::string error;
        if (!ui::Json::Parse(meta, root, error) || !root.IsObject())
            return out;
        const ui::Json* list = root.Find("settings");
        if (!list || !list->IsArray())
            return out;
        std::string group;
        int nextSlot = 0;
        for (const ui::Json& obj : list->array) {
            if (out.size() >= 32)
                break;
            if (!obj.IsObject())
                continue;
            if (const ui::Json* g = obj.Find("group"); g && g->IsString())
                group = g->string;
            ShaderSetting s;
            const ui::Json* id = obj.Find("id");
            s.id = id ? id->StringOr("") : "";
            if (s.id.empty())
                continue;
            const auto str = [&](const char* name) {
                const ui::Json* v = obj.Find(name);
                return v ? v->StringOr("") : std::string();
            };
            const auto num = [&](const char* name, double fallback) {
                const ui::Json* v = obj.Find(name);
                if (!v)
                    return fallback;
                if (v->IsBool())
                    return v->boolean ? 1.0 : 0.0;
                return v->NumberOr(fallback);
            };
            s.label = str("label");
            if (s.label.empty())
                s.label = s.id;
            s.group = group;
            s.toggle = str("type") == "toggle";
            s.min = s.toggle ? 0.0 : num("min", 0.0);
            s.max = s.toggle ? 1.0 : num("max", 1.0);
            if (s.max < s.min)
                std::swap(s.min, s.max);
            s.step = s.toggle ? 1.0 : std::max(1.0e-4, num("step", 0.1));
            s.def = std::clamp(num("default", s.min), s.min, s.max);
            s.slot = static_cast<int>(num("slot", static_cast<double>(nextSlot)));
            if (s.slot < 0 || s.slot >= 32)
                s.slot = -1;
            else
                nextSlot = s.slot + 1;
            if (const ui::Json* labels = obj.Find("labels"); labels && labels->IsArray())
                for (const ui::Json& l : labels->array)
                    s.labels.push_back(l.StringOr(""));
            out.push_back(std::move(s));
        }
        return out;
    }

    std::vector<ShaderPreset> ShaderPack::Presets() const {
        std::string meta;
        {
            std::lock_guard lock(impl->mutex);
            meta = impl->meta;
        }
        std::vector<ShaderPreset> out;
        ui::Json root;
        std::string error;
        if (!ui::Json::Parse(meta, root, error) || !root.IsObject())
            return out;
        const ui::Json* list = root.Find("presets");
        if (!list || !list->IsArray())
            return out;
        const auto values = [](const ui::Json* obj, std::map<std::string, double>& into) {
            if (!obj || !obj->IsObject())
                return;
            for (const auto& [key, v] : obj->object) {
                if (v.IsNumber())
                    into[key] = v.number;
                else if (v.IsBool())
                    into[key] = v.boolean ? 1.0 : 0.0;
            }
        };
        for (const ui::Json& obj : list->array) {
            if (!obj.IsObject() || out.size() >= 16)
                continue;
            ShaderPreset p;
            if (const ui::Json* n = obj.Find("name"))
                p.name = n->StringOr("");
            if (p.name.empty())
                continue;
            values(obj.Find("settings"), p.settings);
            values(obj.Find("engine"), p.engine);
            out.push_back(std::move(p));
        }
        return out;
    }

    bool ShaderPack::Read(const std::string& backend, const std::string& shader,
                          std::vector<char>& out) const {
        std::lock_guard lock(impl->mutex);
        if (impl->bundled) {
            const uint8_t* data = nullptr;
            size_t size = 0;
            if (!BundledShaderBytes(backend, shader, data, size))
                return false;
            out.assign(reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + size);
            return true;
        }
        const std::string rel = backend + "/" + shader + ".bin";
        if (impl->zipMode) {
            if (!impl->zipOpen)
                return false;
            const auto it = impl->index.find(str::Lower(rel));
            if (it == impl->index.end())
                return false;
            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&impl->zip, it->second, &size, 0);
            if (!data)
                return false;
            out.assign(static_cast<const char*>(data), static_cast<const char*>(data) + size);
            mz_free(data);
            return !out.empty();
        }
        if (impl->folder.empty())
            return false;
        std::ifstream file(impl->folder + "/" + rel, std::ios::binary);
        if (!file)
            return false;
        out.assign(std::istreambuf_iterator<char>(file), {});
        return !out.empty();
    }
}
