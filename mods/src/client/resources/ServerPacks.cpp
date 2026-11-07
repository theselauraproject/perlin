#include "client/resources/ServerPacks.hpp"

#include "client/ui/Json.hpp"

#include <miniz.h>
#include <openssl/evp.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "util/Log.hpp"

namespace perlin {
    namespace fs = std::filesystem;

    namespace {
        bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
            std::ifstream in(p, std::ios::binary);
            if (!in)
                return false;
            std::stringstream ss;
            ss << in.rdbuf();
            const std::string s = ss.str();
            out.assign(s.begin(), s.end());
            return true;
        }

        bool WriteFileBytes(const fs::path& p, const std::vector<uint8_t>& data) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            return static_cast<bool>(out);
        }

        bool DecryptCfb8(const std::string& key, const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
            if (key.size() != 32)
                return false;
            EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
            if (!ctx)
                return false;
            out.assign(n + 16, 0);
            int len = 0, total = 0;
            bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_cfb8(), nullptr,
                                         reinterpret_cast<const uint8_t*>(key.data()),
                                         reinterpret_cast<const uint8_t*>(key.data())) == 1 &&
                      EVP_DecryptUpdate(ctx, out.data(), &len, in, static_cast<int>(n)) == 1;
            total = len;
            if (ok && EVP_DecryptFinal_ex(ctx, out.data() + total, &len) == 1)
                total += len;
            EVP_CIPHER_CTX_free(ctx);
            out.resize(static_cast<size_t>(total));
            return ok;
        }

        bool SafeRel(const std::string& rel) {
            if (rel.empty() || rel[0] == '/' || rel.contains(':'))
                return false;
            return !rel.contains("..");
        }

        std::string LowerName(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        fs::path FindPackRoot(const fs::path& dir) {
            std::error_code ec;
            if (fs::exists(dir / "manifest.json", ec))
                return dir;
            for (const auto& e : fs::directory_iterator(dir, ec))
                if (e.is_directory() && fs::exists(e.path() / "manifest.json", ec))
                    return e.path();
            return dir;
        }

        void DecryptPack(const fs::path& dir, const std::string& contentKey) {
            const fs::path contents = dir / "contents.json";
            std::vector<uint8_t> raw;
            if (!ReadFileBytes(contents, raw) || raw.size() < 0x100 + 2)
                return;
            static const uint8_t magic[4] = {0xfc, 0xb9, 0xcf, 0x9b};
            if (std::memcmp(raw.data() + 4, magic, 4) != 0)
                return;
            std::vector<uint8_t> plain;
            if (!DecryptCfb8(contentKey, raw.data() + 0x100, raw.size() - 0x100, plain)) {
                Log("packs", "could not decrypt contents.json in {}", dir.string());
                return;
            }
            ui::Json doc;
            std::string error;
            if (!ui::Json::Parse(std::string(plain.begin(), plain.end()), doc, error)) {
                Log("packs", "contents.json is not valid after decrypting (wrong content key?) in {}",
                    dir.string());
                return;
            }
            const ui::Json* list = doc.Find("content");
            if (!list || !list->IsArray())
                return;
            int decrypted = 0;
            for (const ui::Json& entry : list->array) {
                const ui::Json* path = entry.Find("path");
                const ui::Json* key = entry.Find("key");
                if (!path || !path->IsString() || !key || !key->IsString() || key->string.empty() ||
                    !SafeRel(path->string))
                    continue;
                const fs::path file = dir / path->string;
                std::vector<uint8_t> enc, dec;
                if (!ReadFileBytes(file, enc) || enc.empty())
                    continue;
                if (DecryptCfb8(key->string, enc.data(), enc.size(), dec) && WriteFileBytes(file, dec))
                    decrypted++;
            }
            Log("packs", "decrypted {} files in {}", decrypted, dir.filename().string());
        }
    }

    bool ServerPacks::Mount(const std::string& zipPath, const std::string& id,
                            const std::string& contentKey) {
        std::string base;
        {
            std::lock_guard lock(mutex);
            if (root.empty())
                return false;
            base = root;
            for (const std::string& d : dirs)
                if (d.contains(LowerName(id)))
                    return true;
        }
        std::error_code ec;
        const fs::path out =
            fs::path(base) /
            (LowerName(id) + "_" +
             std::to_string(
                 std::hash<std::string>{}(zipPath + contentKey + std::to_string(fs::file_size(zipPath, ec))) &
                 0xffffff));
        const fs::path ready = out / ".mounted";
        if (!fs::exists(ready, ec)) {
            fs::remove_all(out, ec);
            fs::create_directories(out, ec);
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) {
                Log("packs", "cannot open {}", zipPath);
                return false;
            }
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            size_t extracted = 0, failed = 0;
            for (mz_uint i = 0; i < count; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&zip, i, &st) || mz_zip_reader_is_file_a_directory(&zip, i))
                    continue;
                std::string name = st.m_filename;
                std::replace(name.begin(), name.end(), '\\', '/');
                if (!SafeRel(name))
                    continue;
                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
                if (!data) {
                    if (failed++ < 3)
                        Log("packs", "cannot extract {} from {}", name, zipPath);
                    continue;
                }
                extracted++;
                WriteFileBytes(out / name, std::vector<uint8_t>(static_cast<uint8_t*>(data),
                                                                static_cast<uint8_t*>(data) + size));
                mz_free(data);
            }
            mz_zip_reader_end(&zip);
            Log("packs", "extracted {} of {} entries ({} failed)", extracted, count, failed);
            const fs::path packRoot = FindPackRoot(out);
            {
                std::error_code ls;
                std::string top;
                for (const auto& e : fs::directory_iterator(packRoot, ls))
                    top += (top.empty() ? "" : ", ") + e.path().filename().string() +
                           (e.is_directory() ? "/" : "");
                Log("packs", "pack root holds: {}", top);
            }
            if (!contentKey.empty())
                DecryptPack(packRoot, contentKey);
            std::ofstream(ready) << packRoot.string();
        }
        std::string packRoot;
        {
            std::ifstream in(ready);
            std::getline(in, packRoot);
        }
        if (packRoot.empty())
            packRoot = out.string();
        std::lock_guard lock(mutex);
        dirs.push_back(packRoot);
        generation++;
        Log("packs", "mounted {}", packRoot);
        return true;
    }

}
