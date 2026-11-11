#include "util/Resources.hpp"
#include "util/Paths.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>

namespace perlin::resources {
    namespace {
        Provider gProvider = nullptr;

        bool ReadDisk(const std::string& path, std::string& out) {
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return false;
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return true;
        }
    }

    void SetProvider(Provider provider) {
        gProvider = provider;
    }

    bool Read(const std::string& name, std::string& out) {
        if (const char* dir = std::getenv("PERLIN_RESOURCES");
            dir && *dir && ReadDisk(std::string(dir) + "/" + name, out))
            return true;
        if (ReadDisk(paths::DataDir() + "/assets/" + name, out))
            return true;
        const uint8_t* data = nullptr;
        size_t size = 0;
        if (gProvider && gProvider(name.c_str(), &data, &size)) {
            out.assign(reinterpret_cast<const char*>(data), size);
            return true;
        }
        return ReadDisk(paths::ExeDir() + "/resources/" + name, out) || ReadDisk("resources/" + name, out);
    }

    std::string ReadText(const std::string& name) {
        std::string out;
        Read(name, out);
        return out;
    }
}
