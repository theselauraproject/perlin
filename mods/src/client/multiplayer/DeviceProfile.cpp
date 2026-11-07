#include "client/multiplayer/DeviceProfile.hpp"

#include "util/Paths.hpp"

#include <openssl/evp.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace perlin {
    namespace {
        const char* const kAndroidModels[] = {"GOOGLE Pixel 9 Pro", "GOOGLE Pixel 9",   "GOOGLE Pixel 8 Pro",
                                              "GOOGLE Pixel 8",     "SAMSUNG SM-S928B", "SAMSUNG SM-S926B",
                                              "SAMSUNG SM-S921B",   "SAMSUNG SM-S918B", "SAMSUNG SM-S916B",
                                              "SAMSUNG SM-S911B"};

        bool ValidId(const std::string& id) {
            if (id.size() != 32)
                return false;
            if (id[12] != '4' || !(id[16] == '8' || id[16] == '9' || id[16] == 'a' || id[16] == 'b'))
                return false;
            for (char c : id)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    return false;
            return true;
        }

        std::string Field(const std::string& text, const std::string& key) {
            const std::string needle = "\"" + key + "\"";
            size_t p = text.find(needle);
            if (p == std::string::npos)
                return {};
            p = text.find('"', text.find(':', p));
            if (p == std::string::npos)
                return {};
            const size_t e = text.find('"', p + 1);
            return e == std::string::npos ? std::string() : text.substr(p + 1, e - p - 1);
        }
    }

    DeviceProfile LoadDeviceProfile() {
        const std::string path = paths::DataDir() + "/device.json";
        DeviceProfile profile;
        {
            std::ifstream f(path);
            std::stringstream ss;
            ss << f.rdbuf();
            profile.id = Field(ss.str(), "id");
            profile.model = Field(ss.str(), "model");
        }
        bool known = false;
        for (const char* m : kAndroidModels)
            known = known || profile.model == m;
        if (ValidId(profile.id) && known)
            return profile;
        std::random_device rd;
        char buf[33];
        std::snprintf(buf, sizeof(buf), "%08x%04x4%03x%04x%08x%04x", rd(), rd() & 0xFFFF, rd() & 0xFFF,
                      0x8000 | (rd() & 0x3FFF), rd(), rd() & 0xFFFF);
        profile.id = buf;
        profile.model = kAndroidModels[rd() % (sizeof(kAndroidModels) / sizeof(kAndroidModels[0]))];
        paths::EnsureDir(paths::DataDir());
        std::ofstream out(path, std::ios::trunc);
        out << "{\"id\":\"" << profile.id << "\",\"model\":\"" << profile.model << "\"}\n";
        return profile;
    }

    std::string DeriveSelfSignedId(int64_t clientRandomId, const std::string& deviceId) {
        std::string input(8, '\0');
        const uint64_t v = static_cast<uint64_t>(clientRandomId);
        for (int i = 0; i < 8; i++)
            input[static_cast<size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF);
        input += deviceId;
        std::array<unsigned char, 16> digest{};
        unsigned int len = 0;
        EVP_Digest(input.data(), input.size(), digest.data(), &len, EVP_md5(), nullptr);
        std::array<unsigned char, 16> id{};
        for (size_t i = 0; i < 8; i++) {
            id[i] = digest[7 - i];
            id[8 + i] = digest[15 - i];
        }
        id[6] = static_cast<unsigned char>((id[6] & 0x0f) | 0x30);
        id[8] = static_cast<unsigned char>((id[8] & 0x3f) | 0x80);
        char out[37];
        std::snprintf(out, sizeof(out),
                      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", id[0], id[1],
                      id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12], id[13],
                      id[14], id[15]);
        return out;
    }
}
