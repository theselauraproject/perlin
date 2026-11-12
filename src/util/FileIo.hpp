#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::file {
    // the rest of a stream in one read: sized from the end position when the stream can seek, char by char
    // (istreambuf_iterator, an order of magnitude slower) only when it cannot
    template <class Container> inline void ReadRest(std::istream& in, Container& out) {
        const std::istream::pos_type start = in.tellg();
        if (start != std::istream::pos_type(-1) && in.seekg(0, std::ios::end)) {
            const std::istream::pos_type end = in.tellg();
            if (end != std::istream::pos_type(-1) && end >= start && in.seekg(start)) {
                const size_t n = static_cast<size_t>(end - start);
                out.resize(n);
                if (n) {
                    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
                    out.resize(static_cast<size_t>(in.gcount())); // shorter if the file shrank meanwhile
                }
                return;
            }
        }
        in.clear();
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    inline bool Read(const std::filesystem::path& path, std::string& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        ReadRest(in, out);
        return true;
    }

    inline std::string Read(const std::filesystem::path& path) {
        std::string out;
        Read(path, out);
        return out;
    }

    inline bool ReadBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        ReadRest(in, out);
        return true;
    }

    inline bool Write(const std::filesystem::path& path, std::string_view data) {
        std::error_code ec;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(out);
    }
}
