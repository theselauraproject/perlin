#include "client/javashaders/PackSource.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace perlin::jshader {
    std::string NormalizePath(const std::string& path) {
        std::vector<std::string> parts;
        std::string cur;
        auto flush = [&] {
            if (cur.empty() || cur == ".") {
            } else if (cur == "..") {
                if (!parts.empty())
                    parts.pop_back();
            } else {
                parts.push_back(cur);
            }
            cur.clear();
        };
        for (char c : path) {
            if (c == '/' || c == '\\')
                flush();
            else
                cur += c;
        }
        flush();
        std::string out;
        for (size_t i = 0; i < parts.size(); i++) {
            if (i)
                out += '/';
            out += parts[i];
        }
        return out;
    }

    std::string DirectoryOf(const std::string& path) {
        const size_t at = path.find_last_of('/');
        return at == std::string::npos ? std::string() : path.substr(0, at);
    }

    std::string JoinPath(const std::string& dir, const std::string& rel) {
        if (!rel.empty() && rel[0] == '/')
            return NormalizePath(rel);
        return NormalizePath(dir.empty() ? rel : dir + "/" + rel);
    }

    namespace {
        class DirectoryPack final : public PackSource {
        public:
            explicit DirectoryPack(fs::path shadersRoot) : root(std::move(shadersRoot)) {}

            bool Exists(const std::string& path) const override {
                std::error_code ec;
                return fs::is_regular_file(root / NormalizePath(path), ec);
            }

            bool Read(const std::string& path, std::string& out) const override {
                std::ifstream f(root / NormalizePath(path), std::ios::binary);
                if (!f)
                    return false;
                std::ostringstream ss;
                ss << f.rdbuf();
                out = ss.str();
                return true;
            }

            std::vector<std::string> List(const std::string& dir) const override {
                std::vector<std::string> out;
                std::error_code ec;
                const fs::path base = dir.empty() ? root : root / NormalizePath(dir);
                if (!fs::is_directory(base, ec))
                    return out;
                for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec))
                    if (it->is_regular_file(ec))
                        out.push_back(fs::relative(it->path(), root, ec).generic_string());
                std::sort(out.begin(), out.end());
                return out;
            }

            std::string Name() const override {
                const fs::path parent = root.parent_path();
                return (root.filename() == "shaders" ? parent.filename() : root.filename()).string();
            }

        private:
            fs::path root;
        };
    }

    std::unique_ptr<PackSource> OpenDirectoryPack(const std::string& root) {
        std::error_code ec;
        fs::path base = root;
        if (fs::is_directory(base / "shaders", ec))
            base /= "shaders";
        if (!fs::is_directory(base, ec))
            return nullptr;
        return std::make_unique<DirectoryPack>(base);
    }

    std::unique_ptr<PackSource> OpenPack(const std::string& path) {
        std::error_code ec;
        if (fs::is_directory(path, ec))
            return OpenDirectoryPack(path);
        return OpenZipPack(path);
    }

    bool LooksLikeJavaPack(const std::string& path) {
        std::error_code ec;
        if (!fs::exists(path, ec))
            return false;
        auto source = OpenPack(path);
        if (!source)
            return false;
        for (const char* marker : {"shaders.properties", "final.fsh", "composite.fsh", "gbuffers_terrain.fsh",
                                   "gbuffers_basic.fsh", "gbuffers_textured.fsh", "shadow.vsh"})
            if (source->Exists(marker))
                return true;
        return false;
    }

    std::map<std::string, std::string> ReadJavaPackOptions(const std::string& path) {
        std::map<std::string, std::string> out;
        std::string file = path;
        if (file.size() > 4 && file.compare(file.size() - 4, 4, ".zip") == 0)
            file.resize(file.size() - 4);
        file += ".txt";
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0)
                continue;
            out[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return out;
    }

    void WriteJavaPackOptions(const std::string& path, const std::map<std::string, std::string>& options) {
        std::string file = path;
        if (file.size() > 4 && file.compare(file.size() - 4, 4, ".zip") == 0)
            file.resize(file.size() - 4);
        file += ".txt";
        std::ofstream out(file, std::ios::trunc);
        for (const auto& kv : options)
            out << kv.first << '=' << kv.second << '\n';
    }
}
