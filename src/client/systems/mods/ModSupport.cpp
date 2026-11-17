#include "client/systems/mods/ModSupport.hpp"
#include "util/FileIo.hpp"

#include <chrono>
#include <cstring>
#include <fstream>

namespace perlin::mods::support {
    namespace fs = std::filesystem;

    int64_t UnixMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        file::ReadRest(in, out);
        return !out.empty();
    }

    std::string EmbeddedManifest(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        char magic[8];
        if (!in.read(magic, 8) || std::memcmp(magic, "\0asm", 4) != 0)
            return {};
        const auto leb = [&](uint64_t& value) {
            value = 0;
            for (int shift = 0; shift <= 63; shift += 7) {
                const int c = in.get();
                if (c == EOF)
                    return false;
                value |= static_cast<uint64_t>(c & 0x7F) << shift;
                if (!(c & 0x80))
                    return true;
            }
            return false;
        };
        for (int guard = 0; guard < 256; guard++) {
            const int id = in.get();
            if (id == EOF)
                break;
            uint64_t size = 0;
            if (!leb(size))
                break;
            const std::streamoff start = in.tellg();
            if (id == 0) {
                uint64_t nameLen = 0;
                if (!leb(nameLen) || nameLen > size || nameLen > 64)
                    break;
                std::string name(static_cast<size_t>(nameLen), '\0');
                if (nameLen && !in.read(name.data(), static_cast<std::streamsize>(nameLen)))
                    break;
                if (name == "perlin_manifest") {
                    const uint64_t rest = size - (static_cast<uint64_t>(in.tellg() - start));
                    if (rest == 0 || rest > (64u << 10))
                        return {};
                    std::string text(static_cast<size_t>(rest), '\0');
                    if (!in.read(text.data(), static_cast<std::streamsize>(rest)))
                        return {};
                    return text;
                }
            }
            in.clear();
            in.seekg(start + static_cast<std::streamoff>(size));
            if (!in)
                break;
        }
        return {};
    }

    bool SafeRelative(const std::string& rel, fs::path& out) {
        if (rel.empty() || rel.size() > 240 || rel.contains('\\') || rel.contains('\0'))
            return false;
        const fs::path path = fs::path(rel).lexically_normal();
        if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
            return false;
        for (const auto& part : path) {
            const std::string s = part.string();
            if (s == ".." || s.empty())
                return false;
        }
        out = path;
        return true;
    }
}
