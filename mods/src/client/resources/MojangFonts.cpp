#include "client/resources/MojangFonts.hpp"

#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "client/resources/AssetJobs.hpp"
#include "client/resources/ClientAssets.hpp"
#include "client/ui/Json.hpp"
#include "network/FastDownload.hpp"
#include "network/Http.hpp"
#include "util/FileIo.hpp"
#include "util/Log.hpp"

namespace fs = std::filesystem;

namespace perlin {
    namespace {
        constexpr const char* kManifest = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
        constexpr const char* kResources = "https://resources.download.minecraft.net/";

#ifndef __EMSCRIPTEN__
        bool Download(const std::string& url, const fs::path& target) {
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            const fs::path part = target.string() + ".part";
            for (int attempt = 0; attempt < 3; attempt++) {
                const http::Response res = http::GetUrl(url, {}, 120);
                if (res.status != 200 || res.body.empty())
                    continue;
                {
                    std::ofstream out(part, std::ios::binary);
                    out.write(res.body.data(), static_cast<std::streamsize>(res.body.size()));
                    if (!out.good())
                        continue;
                }
                fs::rename(part, target, ec);
                if (!ec)
                    return true;
            }
            fs::remove(part, ec);
            return false;
        }

        bool ReadJson(const std::string& url, const fs::path& scratch, ui::Json& out) {
            std::string text, error;
            return Download(url, scratch) && file::Read(scratch, text) && ui::Json::Parse(text, out, error);
        }
#endif

        bool ExtractHexFromZipMemory(const void* data, size_t size, const fs::path& hexOut) {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_mem(&zip, data, size, 0))
                return false;
            bool ok = false;
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < count && !ok; i++) {
                char name[512];
                if (!mz_zip_reader_get_filename(&zip, i, name, sizeof(name)))
                    continue;
                const std::string n = name;
                if (n.size() > 4 && n.compare(n.size() - 4, 4, ".hex") == 0) {
                    size_t outSize = 0;
                    void* bytes = mz_zip_reader_extract_to_heap(&zip, i, &outSize, 0);
                    if (bytes) {
                        std::error_code ec;
                        fs::create_directories(hexOut.parent_path(), ec);
                        std::ofstream out(hexOut, std::ios::binary);
                        out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(outSize));
                        ok = out.good();
                        mz_free(bytes);
                    }
                }
            }
            mz_zip_reader_end(&zip);
            return ok;
        }

        bool ExtractFromJar(const fs::path& jar, const fs::path& hexOut, const fs::path& asciiOut) {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, jar.string().c_str(), 0))
                return false;
            bool hexOk = false;
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < count; i++) {
                char name[512];
                if (!mz_zip_reader_get_filename(&zip, i, name, sizeof(name)))
                    continue;
                const std::string n = name;
                const bool vanillaSheet = n == "assets/minecraft/textures/font/ascii.png" ||
                                          n == "assets/minecraft/textures/font/accented.png" ||
                                          n == "assets/minecraft/textures/font/nonlatin_european.png";
                if (vanillaSheet) {
                    size_t size = 0;
                    void* bytes = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
                    if (bytes) {
                        std::error_code ec;
                        fs::create_directories(asciiOut.parent_path(), ec);
                        std::ofstream out(asciiOut.parent_path() / fs::path(n).filename(), std::ios::binary);
                        out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
                        mz_free(bytes);
                    }
                } else if (!hexOk && n.contains("font/unifont_all_no_pua") && n.size() > 4 &&
                           n.compare(n.size() - 4, 4, ".zip") == 0) {
                    size_t size = 0;
                    void* bytes = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
                    if (bytes) {
                        hexOk = ExtractHexFromZipMemory(bytes, size, hexOut);
                        mz_free(bytes);
                    }
                }
            }
            mz_zip_reader_end(&zip);
            return hexOk;
        }
    }

    std::string MojangFonts::UnihexPath(const std::string& dataDir) {
        return (fs::path(dataDir) / "fonts" / "unifont.hex").string();
    }

    bool MojangFonts::Available(const std::string& dataDir) {
        std::error_code ec;
        return fs::exists(UnihexPath(dataDir), ec) && fs::file_size(UnihexPath(dataDir), ec) > 1024 &&
               fs::exists(fs::path(dataDir) / "font" / "ascii.png", ec) &&
               fs::exists(fs::path(dataDir) / "font" / "accented.png", ec) && clientassets::Ready(dataDir);
    }

    bool MojangFonts::Ensure(const std::string& dataDir) {
        assets::Job& job = assets::Client();
        if (Available(dataDir)) {
            job.stage = assets::Stage::Done;
            return true;
        }
#ifdef __EMSCRIPTEN__
        {
            job.stage = assets::Stage::Checking;
            const fs::path jar = fs::path(dataDir) / "fonts" / "client.jar";
            std::error_code ec;
            if (!fs::exists(jar, ec)) {
                Log("fonts", "the page did not provide Mojang's client jar");
                job.stage = assets::Stage::Failed;
                return false;
            }
            job.stage = assets::Stage::Extracting;
            const bool ok =
                ExtractFromJar(jar, UnihexPath(dataDir), fs::path(dataDir) / "font" / "ascii.png");
            clientassets::BuildFromJar(jar, dataDir);
            fs::remove(jar, ec);
            std::cout << (ok ? "[fonts] Unicode font ready" : "[fonts] Unicode font extraction failed")
                      << std::endl;
            job.stage = (ok || clientassets::Ready(dataDir)) ? assets::Stage::Done : assets::Stage::Failed;
            return ok;
        }
#else
        job.stage = assets::Stage::Checking;
        struct Finish {
            assets::Job& job;
            bool ok = false;
            ~Finish() { job.stage = ok ? assets::Stage::Done : assets::Stage::Failed; }
        } finish{job};
        const fs::path root = fs::path(dataDir) / "fonts";
        const fs::path hexOut = UnihexPath(dataDir);
        std::error_code ec;
        fs::create_directories(root, ec);
        ui::Json manifest;
        if (!ReadJson(kManifest, root / "manifest.tmp", manifest)) {
            Log("fonts", "could not reach the Mojang version manifest");
            return false;
        }
        std::string release;
        if (const ui::Json* latest = manifest.Find("latest"))
            if (const ui::Json* r = latest->Find("release"))
                release = r->StringOr("");
        std::string versionUrl;
        if (const ui::Json* versions = manifest.Find("versions"); versions && versions->IsArray())
            for (const ui::Json& v : versions->array) {
                const ui::Json* id = v.Find("id");
                const ui::Json* url = v.Find("url");
                if (id && url && id->StringOr("") == release) {
                    versionUrl = url->StringOr("");
                    break;
                }
            }
        fs::remove(root / "manifest.tmp", ec);
        ui::Json version;
        if (versionUrl.empty() || !ReadJson(versionUrl, root / "version.tmp", version)) {
            Log("fonts", "could not read the Mojang version metadata");
            return false;
        }
        fs::remove(root / "version.tmp", ec);
        std::string indexUrl, clientUrl;
        if (const ui::Json* index = version.Find("assetIndex"))
            if (const ui::Json* u = index->Find("url"))
                indexUrl = u->StringOr("");
        if (const ui::Json* downloads = version.Find("downloads"))
            if (const ui::Json* client = downloads->Find("client"))
                if (const ui::Json* u = client->Find("url"))
                    clientUrl = u->StringOr("");
        bool ok = false;
        const fs::path asciiOut = fs::path(dataDir) / "font" / "ascii.png";
        if (!clientUrl.empty()) {
            const fs::path jar = root / "client.tmp";
            job.stage = assets::Stage::Downloading;
            download::Options options;
            options.connections = 8;
            options.chunkBytes = 2ull << 20;
            if (download::Fetch(clientUrl, jar, &job.bytes, options) || Download(clientUrl, jar)) {
                job.stage = assets::Stage::Extracting;
                ok = ExtractFromJar(jar, hexOut, asciiOut);
                clientassets::BuildFromJar(jar, dataDir);
            }
            fs::remove(jar, ec);
        }
        ui::Json assets;
        if (!ok && !indexUrl.empty() && ReadJson(indexUrl, root / "assets.tmp", assets)) {
            if (const ui::Json* objects = assets.Find("objects"); objects && objects->IsObject())
                for (const auto& [key, value] : objects->object) {
                    const bool zipped = key.size() > 4 && key.compare(key.size() - 4, 4, ".zip") == 0;
                    const bool raw = key.size() > 4 && key.compare(key.size() - 4, 4, ".hex") == 0;
                    if (!key.contains("unifont") || key.contains("_pua") && !key.contains("no_pua") ||
                        (!zipped && !raw))
                        continue;
                    if (key.contains("unifont_jp"))
                        continue;
                    const ui::Json* hash = value.Find("hash");
                    if (!hash || hash->StringOr("").size() < 2)
                        continue;
                    const std::string h = hash->StringOr("");
                    const fs::path zip = root / "unifont.zip";
                    std::string bytes;
                    if (Download(std::string(kResources) + h.substr(0, 2) + "/" + h, zip) &&
                        file::Read(zip, bytes)) {
                        if (raw) {
                            std::ofstream out(hexOut, std::ios::binary);
                            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                            ok = out.good();
                        } else {
                            ok = ExtractHexFromZipMemory(bytes.data(), bytes.size(), hexOut);
                        }
                    } else {
                        Log("fonts", "could not download {}", key);
                    }
                    fs::remove(zip, ec);
                    if (ok)
                        break;
                }
        }
        fs::remove(root / "assets.tmp", ec);
        std::cout << (ok ? "[fonts] Unicode font ready" : "[fonts] Unicode font download failed")
                  << std::endl;
        finish.ok = ok || clientassets::Ready(dataDir);
        return ok;
#endif
    }
}
