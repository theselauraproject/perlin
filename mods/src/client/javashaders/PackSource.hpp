#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin::jshader {
    class PackSource {
    public:
        virtual ~PackSource() = default;
        virtual bool Exists(const std::string& path) const = 0;
        virtual bool Read(const std::string& path, std::string& out) const = 0;
        virtual std::vector<std::string> List(const std::string& dir) const = 0;
        virtual std::string Name() const = 0;
    };

    std::unique_ptr<PackSource> OpenDirectoryPack(const std::string& root);
    std::unique_ptr<PackSource> OpenZipPack(const std::string& zipPath);
    std::unique_ptr<PackSource> OpenPack(const std::string& path);

    bool LooksLikeJavaPack(const std::string& path);
    std::map<std::string, std::string> ReadJavaPackOptions(const std::string& path);
    void WriteJavaPackOptions(const std::string& path, const std::map<std::string, std::string>& options);

    std::string NormalizePath(const std::string& path);
    std::string DirectoryOf(const std::string& path);
    std::string JoinPath(const std::string& dir, const std::string& rel);
}
