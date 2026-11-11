#include "client/javashaders/PackSource.hpp"

#include <miniz.h>

#include <algorithm>
#include <unordered_map>

namespace perlin::jshader {
    namespace {
        class ZipPack final : public PackSource {
        public:
            ~ZipPack() override {
                if (open)
                    mz_zip_reader_end(&zip);
            }

            bool Open(const std::string& path) {
                zip = {};
                if (!mz_zip_reader_init_file(&zip, path.c_str(), 0))
                    return false;
                open = true;
                name = path;
                const size_t slash = name.find_last_of("/\\");
                if (slash != std::string::npos)
                    name = name.substr(slash + 1);
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".zip") == 0)
                    name.resize(name.size() - 4);
                std::vector<std::pair<std::string, mz_uint>> raw;
                const mz_uint count = mz_zip_reader_get_num_files(&zip);
                for (mz_uint i = 0; i < count; i++) {
                    mz_zip_archive_file_stat st;
                    if (!mz_zip_reader_file_stat(&zip, i, &st) || mz_zip_reader_is_file_a_directory(&zip, i))
                        continue;
                    raw.emplace_back(NormalizePath(st.m_filename), i);
                }
                std::string prefix;
                for (const auto& entry : raw) {
                    const size_t at = entry.first.find("shaders/");
                    if (at != std::string::npos && (at == 0 || entry.first[at - 1] == '/')) {
                        prefix = entry.first.substr(0, at + 8);
                        break;
                    }
                }
                if (prefix.empty())
                    return false;
                for (const auto& entry : raw)
                    if (entry.first.compare(0, prefix.size(), prefix) == 0)
                        files[entry.first.substr(prefix.size())] = entry.second;
                return !files.empty();
            }

            bool Exists(const std::string& path) const override {
                return files.contains(NormalizePath(path));
            }

            bool Read(const std::string& path, std::string& out) const override {
                const auto it = files.find(NormalizePath(path));
                if (it == files.end())
                    return false;
                size_t size = 0;
                void* data =
                    mz_zip_reader_extract_to_heap(const_cast<mz_zip_archive*>(&zip), it->second, &size, 0);
                if (!data)
                    return false;
                out.assign(static_cast<const char*>(data), size);
                mz_free(data);
                return true;
            }

            std::vector<std::string> List(const std::string& dir) const override {
                std::vector<std::string> out;
                const std::string d = NormalizePath(dir);
                for (const auto& entry : files)
                    if (d.empty() || entry.first.compare(0, d.size() + 1, d + "/") == 0)
                        out.push_back(entry.first);
                std::sort(out.begin(), out.end());
                return out;
            }

            std::string Name() const override { return name; }

        private:
            mz_zip_archive zip{};
            bool open = false;
            std::string name;
            std::unordered_map<std::string, mz_uint> files;
        };
    }

    std::unique_ptr<PackSource> OpenZipPack(const std::string& zipPath) {
        auto pack = std::make_unique<ZipPack>();
        if (!pack->Open(zipPath))
            return nullptr;
        return pack;
    }
}
