#include "client/resources/TexturePack.hpp"

#include "client/resources/JavaPack.hpp"
#include "util/ZipFile.hpp"
#include "util/Strings.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include "util/Log.hpp"

namespace perlin {
    namespace fs = std::filesystem;

    struct TexturePack::Impl {
        struct Layer {
            bool zipMode = false;
            bool java = false;

            bool legacyGui = false;
            std::string folder;
            mz_zip_archive zip{};
            FILE* zipFile = nullptr;
            bool zipOpen = false;
            std::unordered_map<std::string, mz_uint> index;
            std::string label;
            bool Active() const { return zipMode ? zipOpen : !folder.empty(); }
            void Reset() {
                if (zipOpen)
                    mz_zip_reader_end(&zip);
                if (zipFile)
                    std::fclose(zipFile);
                zipFile = nullptr;
                zip = {};
                zipOpen = false;
                zipMode = false;
                java = false;
                legacyGui = false;
                folder.clear();
                index.clear();
                label.clear();
            }
        };
        std::vector<std::unique_ptr<Layer>> layers;
        mutable std::mutex mutex;

        void Reset() {
            for (auto& l : layers)
                l->Reset();
            layers.clear();
        }
    };

    TexturePack::TexturePack() : impl(std::make_unique<Impl>()) {}
    TexturePack::~TexturePack() {
        impl->Reset();
    }

    TexturePack& TexturePack::Instance() {
        static TexturePack pack;
        return pack;
    }

    namespace {

        bool HasTextures(const fs::path& dir) {
            std::error_code ec;
            return fs::is_directory(dir / "textures", ec);
        }

        std::string FindFolderRoot(const fs::path& dir) {
            std::error_code ec;
            if (HasTextures(dir))
                return dir.generic_string();
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_directory(ec)) {
                    if (HasTextures(entry.path()))
                        return entry.path().generic_string();
                    for (const auto& inner : fs::directory_iterator(entry.path(), ec))
                        if (inner.is_directory(ec) && HasTextures(inner.path()))
                            return inner.path().generic_string();
                }
            return {};
        }
    }

    void TexturePack::Close() {
        std::lock_guard lock(impl->mutex);
        impl->Reset();
    }

    bool TexturePack::Active() const {
        std::lock_guard lock(impl->mutex);
        return !impl->layers.empty();
    }

    std::string TexturePack::Description() const {
        std::lock_guard lock(impl->mutex);
        std::string out;
        for (const auto& l : impl->layers)
            out += (out.empty() ? "" : ", ") + l->label;
        return out;
    }

    namespace {
        bool OpenLayer(TexturePack::Impl::Layer& layer, const fs::path& source, const std::string& name,
                       std::string& error) {
            std::error_code ec;
            if (!fs::exists(source, ec)) {
                error = "no such texture pack: " + source.generic_string();
                return false;
            }
            if (fs::is_directory(source, ec)) {
                layer.folder = FindFolderRoot(source);
                if (layer.folder.empty()) {
                    error = "no textures folder inside " + source.generic_string();
                    return false;
                }
                layer.label = name;
                layer.java = fs::path(layer.folder).parent_path().filename() == "assets";
                layer.legacyGui = fs::is_directory(fs::path(layer.folder) / "textures" / "gui", ec);
                if (layer.java)
                    Log("textures", "{} is a Java Edition pack", name);
                return true;
            }
            if (!OpenZipReader(layer.zip, source, layer.zipFile)) {
                error = "not a readable zip/mcpack archive: " + source.generic_string();
                layer.zip = {};
                return false;
            }
            layer.zipOpen = true;
            layer.zipMode = true;
            std::string prefix;
            bool prefixFound = false;
            const mz_uint count = mz_zip_reader_get_num_files(&layer.zip);
            std::vector<std::pair<std::string, mz_uint>> entries;
            entries.reserve(count);
            for (mz_uint i = 0; i < count; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&layer.zip, i, &st) ||
                    mz_zip_reader_is_file_a_directory(&layer.zip, i))
                    continue;
                std::string path = st.m_filename;
                std::replace(path.begin(), path.end(), '\\', '/');
                entries.emplace_back(str::Lower(path), i);
            }
            for (const auto& [lower, idx] : entries) {
                const size_t java = lower.find("assets/minecraft/textures/");
                if (java != std::string::npos && (java == 0 || lower[java - 1] == '/') &&
                    (!prefixFound || java + 17 < prefix.size())) {
                    prefix = lower.substr(0, java) + "assets/minecraft/";
                    prefixFound = true;
                }
            }
            if (!prefixFound)
                for (const auto& [lower, idx] : entries) {
                    const size_t at = lower.find("textures/");
                    if (at != std::string::npos && (at == 0 || lower[at - 1] == '/') &&
                        (!prefixFound || at < prefix.size())) {
                        prefix = lower.substr(0, at);
                        prefixFound = true;
                    }
                }
            if (!prefixFound) {
                error = "no textures folder inside " + source.generic_string();
                layer.Reset();
                return false;
            }
            for (auto& [path, idx] : entries)
                if (path.compare(0, prefix.size(), prefix) == 0)
                    layer.index.emplace(path.substr(prefix.size()), idx);
            layer.label = name;
            layer.java =
                prefix.size() >= 17 && prefix.compare(prefix.size() - 17, 17, "assets/minecraft/") == 0;
            for (const auto& entry : layer.index)
                if (entry.first.starts_with("textures/gui/")) {
                    layer.legacyGui = true;
                    break;
                }
            Log("textures", "opened {} ({} files{})", name, layer.index.size(),
                (layer.java ? ", Java Edition" : ""));
            return true;
        }
    }

    bool TexturePack::OpenStack(const std::string& packsDir, const std::vector<std::string>& names,
                                std::string& error) {
        std::lock_guard lock(impl->mutex);
        generation++;
        impl->Reset();
        error.clear();
        for (const std::string& name : names) {
            if (name.empty())
                continue;
            auto layer = std::make_unique<Impl::Layer>();
            std::string one;
            if (OpenLayer(*layer, fs::path(packsDir) / name, name, one))
                impl->layers.push_back(std::move(layer));
            else
                error += (error.empty() ? "" : "; ") + one;
        }
        return !impl->layers.empty();
    }

    bool TexturePack::Open(const std::string& packsDir, const std::string& name, std::string& error) {
        return OpenStack(packsDir, name.empty() ? std::vector<std::string>{} : std::vector<std::string>{name},
                         error);
    }

    bool TexturePack::Has(const std::string& rel) const {
        std::lock_guard lock(impl->mutex);
        for (const auto& l : impl->layers) {
            if (l->java || l->legacyGui)
                for (const javapack::Source& source : javapack::Sources(rel)) {
                    if (l->zipMode ? (l->zipOpen && l->index.contains(str::Lower(source.path)))
                                   : (!l->folder.empty() && fs::exists(fs::path(l->folder) / source.path)))
                        return true;
                }
            if (l->zipMode) {
                if (l->zipOpen && l->index.contains(str::Lower(rel)))
                    return true;
                if (const size_t dot = rel.find_last_of('.');
                    l->zipOpen && dot != std::string::npos &&
                    l->index.contains(str::Lower(rel.substr(0, dot) + ".texture_set.json")))
                    return true;
            } else if (!l->folder.empty()) {
                std::error_code ec;
                if (fs::exists(fs::path(l->folder) / rel, ec))
                    return true;
                if (const size_t dot = rel.find_last_of('.');
                    dot != std::string::npos &&
                    fs::exists(fs::path(l->folder) / (rel.substr(0, dot) + ".texture_set.json"), ec))
                    return true;
            }
        }
        return false;
    }

    namespace {
        bool LoadDirect(TexturePack::Impl::Layer* l, const std::string& rel, Texture& out) {
            {
                if (l->zipMode) {
                    if (!l->zipOpen)
                        return false;
                    const auto it = l->index.find(str::Lower(rel));
                    if (it == l->index.end()) {
                        const size_t dot = rel.find_last_of('.');
                        if (dot == std::string::npos)
                            return false;
                        const std::string base = rel.substr(0, dot);
                        const auto setIt = l->index.find(str::Lower(base + ".texture_set.json"));
                        if (setIt == l->index.end())
                            return false;
                        size_t setSize = 0;
                        void* setData = mz_zip_reader_extract_to_heap(&l->zip, setIt->second, &setSize, 0);
                        if (!setData)
                            return false;
                        std::string image;
                        unsigned char rgba[4] = {255, 255, 255, 255};
                        const bool parsed = TextureSetColor(
                            std::string(static_cast<const char*>(setData), setSize), image, rgba);
                        mz_free(setData);
                        if (!parsed)
                            return false;
                        if (image.empty()) {
                            MakeFlatTexture(rgba, 16, out);
                            return true;
                        }
                        const size_t slash = rel.find_last_of('/');
                        const std::string dir =
                            slash == std::string::npos ? std::string() : rel.substr(0, slash + 1);
                        for (const char* extension : {".png", ".tga"}) {
                            const auto imageIt = l->index.find(str::Lower(dir + image + extension));
                            if (imageIt == l->index.end())
                                continue;
                            size_t imageSize = 0;
                            void* imageData =
                                mz_zip_reader_extract_to_heap(&l->zip, imageIt->second, &imageSize, 0);
                            if (!imageData)
                                continue;
                            const bool loaded = LoadTextureMemory(
                                static_cast<const unsigned char*>(imageData), imageSize, out);
                            mz_free(imageData);
                            if (loaded && out.width > 0 && out.height > 0)
                                return true;
                        }
                        return false;
                    }
                    size_t size = 0;
                    void* data = mz_zip_reader_extract_to_heap(&l->zip, it->second, &size, 0);
                    if (!data)
                        return false;
                    const bool ok = LoadTextureMemory(static_cast<const unsigned char*>(data), size, out);
                    mz_free(data);
                    if (ok && out.width > 0 && out.height > 0)
                        return true;
                } else if (!l->folder.empty()) {
                    if (LoadTexture(l->folder + "/" + rel, out) && out.width > 0 && out.height > 0)
                        return true;
                }
            }
            return false;
        }

        bool LoadJavaFile(TexturePack::Impl::Layer& l, const std::string& path, Texture& out) {
            if (l.zipMode) {
                if (!l.zipOpen)
                    return false;
                const auto it = l.index.find(str::Lower(path));
                if (it == l.index.end())
                    return false;
                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&l.zip, it->second, &size, 0);
                if (!data)
                    return false;
                const bool ok = LoadTextureMemory(static_cast<const unsigned char*>(data), size, out);
                mz_free(data);
                return ok && out.width > 0 && out.height > 0;
            }
            return !l.folder.empty() && LoadTexture(l.folder + "/" + path, out) && out.width > 0 &&
                   out.height > 0;
        }

        bool LoadJava(TexturePack::Impl::Layer& l, const std::string& rel, Texture& out) {
            if (javapack::Compose(rel, [&](const std::string& path, Texture& sheet) { return LoadJavaFile(l, path, sheet); },
                                  out))
                return true;
            for (const javapack::Source& source : javapack::Sources(rel)) {
                Texture sheet;
                if (LoadJavaFile(l, source.path, sheet) && javapack::Crop(sheet, source, out))
                    return true;
            }
            return false;
        }
    }

    bool TexturePack::Load(const std::string& rel, Texture& out) const {
        std::lock_guard lock(impl->mutex);

        const auto finish = [&]() {
            if (rel.contains("entity/steve") || rel.contains("entity/alex") || rel.contains("entity/player/"))
                UpgradeLegacySkinTexture(out);
            return true;
        };
        for (const auto& l : impl->layers) {
            if (LoadDirect(l.get(), rel, out))
                return finish();
            if ((l->java || l->legacyGui) && LoadJava(*l, rel, out))
                return finish();
        }
        return false;
    }

    size_t TexturePack::LayerCount() const {
        std::lock_guard lock(impl->mutex);
        return impl->layers.size();
    }

    bool TexturePack::LayerJava(size_t layer) const {
        std::lock_guard lock(impl->mutex);
        return layer < impl->layers.size() && impl->layers[layer]->java;
    }

    bool TexturePack::LayerReadText(size_t layer, const std::string& rel, std::string& out) const {
        std::lock_guard lock(impl->mutex);
        if (layer >= impl->layers.size())
            return false;
        const auto& l = impl->layers[layer];
        if (l->zipMode) {
            if (!l->zipOpen)
                return false;
            const auto found = l->index.find(str::Lower(rel));
            if (found == l->index.end())
                return false;
            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&l->zip, found->second, &size, 0);
            if (!data)
                return false;
            out.assign(static_cast<const char*>(data), size);
            mz_free(data);
            return true;
        }
        if (l->folder.empty())
            return false;
        std::ifstream in(fs::path(l->folder) / rel, std::ios::binary);
        if (!in)
            return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }

    bool TexturePack::LayerLoad(size_t layer, const std::string& rel, Texture& out) const {
        std::lock_guard lock(impl->mutex);
        return layer < impl->layers.size() && LoadDirect(impl->layers[layer].get(), rel, out);
    }

    std::vector<std::string> TexturePack::ReadTextLayers(const std::string& rel) const {
        std::lock_guard lock(impl->mutex);
        std::vector<std::string> out;
        for (auto it = impl->layers.rbegin(); it != impl->layers.rend(); ++it) {
            const auto& l = *it;
            if (l->zipMode) {
                if (!l->zipOpen)
                    continue;
                const auto found = l->index.find(str::Lower(rel));
                if (found == l->index.end())
                    continue;
                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&l->zip, found->second, &size, 0);
                if (!data)
                    continue;
                out.emplace_back(static_cast<const char*>(data), size);
                mz_free(data);
            } else if (!l->folder.empty()) {
                std::ifstream in(fs::path(l->folder) / rel, std::ios::binary);
                if (!in)
                    continue;
                out.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
        }
        return out;
    }

    std::vector<std::string> TexturePack::ListFiles(const std::string& dirRel,
                                                    const std::string& suffix) const {
        std::lock_guard lock(impl->mutex);
        std::vector<std::string> out;
        std::string prefix = str::Lower(dirRel);
        if (!prefix.empty() && prefix.back() != '/')
            prefix += '/';
        const std::string lowerSuffix = str::Lower(suffix);
        const auto matches = [&](const std::string& lowerPath) {
            return lowerPath.compare(0, prefix.size(), prefix) == 0 &&
                   lowerPath.find('/', prefix.size()) == std::string::npos &&
                   lowerPath.size() >= lowerSuffix.size() &&
                   lowerPath.compare(lowerPath.size() - lowerSuffix.size(), lowerSuffix.size(),
                                     lowerSuffix) == 0;
        };
        for (auto it = impl->layers.rbegin(); it != impl->layers.rend(); ++it) {
            const auto& l = *it;
            if (l->zipMode) {
                for (const auto& [path, idx] : l->index)
                    if (matches(path))
                        out.push_back(path);
            } else if (!l->folder.empty()) {
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(fs::path(l->folder) / dirRel, ec))
                    if (entry.is_regular_file(ec)) {
                        const std::string rel =
                            (dirRel.empty() || dirRel.back() == '/' ? dirRel : dirRel + "/") +
                            entry.path().filename().string();
                        if (matches(str::Lower(rel)))
                            out.push_back(rel);
                    }
            }
        }
        return out;
    }
}
