#include "client/resources/SampleDownload.hpp"
#include "util/Log.hpp"
#include "util/Platform.hpp"

#ifndef __EMSCRIPTEN__
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <glaze/glaze.hpp>
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "client/resources/AssetJobs.hpp"
#include "client/ui/Json.hpp"
#include "network/FastDownload.hpp"
#include "network/Http.hpp"

#include <miniz.h>

namespace fs = std::filesystem;

namespace perlin::sample_manifest {
    struct ManifestFiles {
        std::vector<std::string> core;
        std::vector<std::string> sounds;
    };

    struct Manifest {
        std::string repository;
        std::string ref;
        std::string baseUrl;
        ManifestFiles files;
    };
}

namespace {
    using perlin::sample_manifest::Manifest;

    constexpr const char* kRepository = "Mojang/bedrock-samples";
    constexpr const char* kBranch = "main";

    const std::vector<std::string>& Directories() {
        static const std::vector<std::string> dirs = {
            "resource_pack/textures",
            "metadata/vanilladata_modules",
            "behavior_pack/blocks",
            "behavior_pack/biomes",
            "resource_pack/models",
            "resource_pack/ui",
            "resource_pack/texts",
            "resource_pack/font",
            "resource_pack/attachables",
            "resource_pack/animations",
            "resource_pack/animation_controllers",
            "resource_pack/render_controllers",
            "resource_pack/entity",
            "behavior_pack/entities",
            "resource_pack/sounds",
            "resource_pack/biomes",
        };
        return dirs;
    }

    std::string Encode(const std::string& path) {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : path) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                c == '_' || c == '.' || c == '~' || c == '/') {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            }
        }
        return out;
    }

    bool ListTree(const std::string& dir, bool recursive, std::vector<std::string>& files,
                  std::vector<std::string>& subdirs, bool& truncated) {
        const std::string url = std::string("https://api.github.com/repos/") + kRepository + "/git/trees/" +
                                kBranch + ":" + Encode(dir) + (recursive ? "?recursive=1" : "");
        perlin::http::Response res;
        for (int attempt = 0; attempt < 3; attempt++) {
            res = perlin::http::GetUrl(url, {{"Accept", "application/vnd.github+json"}}, 60);
            if (res.status == 200)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
        }
        if (res.status == 404)
            return true;
        if (res.status != 200) {
            perlin::Log("samples", "listing {} failed ({})", dir, res.status);
            return false;
        }
        perlin::ui::Json json;
        std::string error;
        if (!perlin::ui::Json::Parse(res.body, json, error))
            return false;
        truncated = false;
        if (const perlin::ui::Json* t = json.Find("truncated"))
            truncated = t->BoolOr(false);
        const perlin::ui::Json* tree = json.Find("tree");
        if (!tree || !tree->IsArray())
            return false;
        for (const perlin::ui::Json& entry : tree->array) {
            const perlin::ui::Json* path = entry.Find("path");
            const perlin::ui::Json* type = entry.Find("type");
            if (!path || !type)
                continue;
            const std::string full = dir + "/" + path->StringOr("");
            if (type->StringOr("") == "blob")
                files.push_back(full);
            else if (type->StringOr("") == "tree")
                subdirs.push_back(full);
        }
        return true;
    }

    bool CollectDirectory(const std::string& dir, std::vector<std::string>& files) {
        std::vector<std::string> subdirs;
        bool truncated = false;
        if (!ListTree(dir, true, files, subdirs, truncated))
            return false;
        if (!truncated)
            return true;
        files.clear();
        subdirs.clear();
        if (!ListTree(dir, false, files, subdirs, truncated))
            return false;
        for (const std::string& sub : subdirs)
            if (!CollectDirectory(sub, files))
                return false;
        return true;
    }

    bool FetchAll(const std::string& prefix, const std::vector<std::string>& files, const fs::path& repo,
                  const char* label) {
        std::atomic<size_t> next{0}, done{0}, failed{0};
        const size_t total = files.size();
        perlin::assets::Job& job = perlin::assets::Samples();
        job.filesTotal = total;
        job.filesDone = 0;
        auto worker = [&] {
            httplib::SSLClient cli("raw.githubusercontent.com", 443);
            perlin::http::UseSystemCa(cli);
            cli.set_connection_timeout(20);
            cli.set_read_timeout(60);
            cli.set_keep_alive(true);
            cli.set_follow_location(true);
            for (;;) {
                const size_t i = next.fetch_add(1);
                if (i >= total)
                    break;
                const fs::path target = repo / files[i];
                std::error_code ec;
                if (fs::exists(target, ec) && fs::file_size(target, ec) > 0) {
                    done++;
                    continue;
                }
                bool ok = false;
                for (int attempt = 0; attempt < 4 && !ok; attempt++) {
                    httplib::Result res = cli.Get(prefix + Encode(files[i]));
                    if (!res || res->status != 200) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(300 * (attempt + 1)));
                        continue;
                    }
                    fs::create_directories(target.parent_path(), ec);
                    const fs::path temp = target.string() + ".part";
                    {
                        std::ofstream out(temp, std::ios::binary);
                        out.write(res->body.data(), static_cast<std::streamsize>(res->body.size()));
                        if (!out.good())
                            continue;
                    }
                    fs::rename(temp, target, ec);
                    ok = !ec;
                }
                if (!ok)
                    failed++;
                const size_t n = ++done;
                job.filesDone = n;
                if (n % 500 == 0)
                    perlin::Log("samples", "{} {}/{}", label, n, total);
            }
        };
        std::vector<std::thread> threads;
        threads.reserve(12);
        for (int i = 0; i < 12; i++)
            threads.emplace_back(worker);
        for (std::thread& t : threads)
            t.join();
        perlin::Log("samples", "{} done, {} failed of {}", label, failed.load(), total);
        return failed.load() == 0;
    }

    bool Wanted(const std::string& rel) {
        if (rel.empty() || rel.back() == '/')
            return false;
        if (rel.starts_with("resource_pack/") && rel.find('/', 14) == std::string::npos)
            return true;
        for (const std::string& dir : Directories())
            if (rel.size() > dir.size() && rel.compare(0, dir.size(), dir) == 0 && rel[dir.size()] == '/')
                return true;
        return false;
    }

    bool ExtractSamplesZip(const fs::path& zipPath, const fs::path& repo) {
        perlin::assets::Job& job = perlin::assets::Samples();
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, zipPath.string().c_str(), 0)) {
            perlin::Log("samples", "the downloaded archive is not a valid zip");
            return false;
        }
        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        std::string prefix;
        bool found = false;
        std::vector<std::pair<mz_uint, std::string>> entries;
        std::vector<std::string> names(count);
        for (mz_uint i = 0; i < count; i++) {
            char name[1024];
            if (mz_zip_reader_get_filename(&zip, i, name, sizeof(name)))
                names[i] = name;
            const std::string& n = names[i];
            const size_t at = n.find("resource_pack/blocks.json");
            if (!found && at != std::string::npos && at + 25 == n.size()) {
                prefix = n.substr(0, at);
                found = true;
            }
        }
        mz_zip_reader_end(&zip);
        if (!found) {
            perlin::Log("samples", "the archive has no resource_pack/blocks.json");
            return false;
        }
        for (mz_uint i = 0; i < count; i++) {
            const std::string& n = names[i];
            if (n.size() <= prefix.size() || n.compare(0, prefix.size(), prefix) != 0)
                continue;
            std::string rel = n.substr(prefix.size());
            if (Wanted(rel))
                entries.emplace_back(i, std::move(rel));
        }
        job.filesTotal = entries.size();
        job.filesDone = 0;
        job.stage = perlin::assets::Stage::Extracting;
        std::atomic<size_t> next{0};
        std::atomic<bool> failed{false};
        auto work = [&] {
            mz_zip_archive z{};
            if (!mz_zip_reader_init_file(&z, zipPath.string().c_str(), 0)) {
                failed = true;
                return;
            }
            std::error_code ec;
            for (;;) {
                const size_t k = next.fetch_add(1);
                if (k >= entries.size() || failed)
                    break;
                const fs::path target = repo / entries[k].second;
                fs::create_directories(target.parent_path(), ec);
                if (!mz_zip_reader_extract_to_file(&z, entries[k].first, target.string().c_str(), 0)) {
                    perlin::Log("samples", "could not extract {}", entries[k].second);
                    failed = true;
                    break;
                }
                job.filesDone++;
            }
            mz_zip_reader_end(&z);
        };
        const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 2u, 6u);
        std::vector<std::thread> pool;
        for (unsigned i = 0; i < threads; i++)
            pool.emplace_back(work);
        for (std::thread& t : pool)
            t.join();
        perlin::Log("samples", "extracted {} of {} files", job.filesDone.load(), entries.size());
        return !failed;
    }

    bool DownloadReleaseZip(const std::string& targetDir) {
        perlin::assets::Job& job = perlin::assets::Samples();
        job.stage = perlin::assets::Stage::Checking;
        const std::string latest =
            perlin::download::Resolve(std::string("https://github.com/") + kRepository + "/releases/latest");
        const size_t tagAt = latest.rfind("/tag/");
        const std::string tag =
            tagAt != std::string::npos ? latest.substr(tagAt + 5) : std::string("v1.26.30.5");
        if (tagAt == std::string::npos)
            perlin::Log("samples", "could not read the latest release; using {}", tag);
        const std::string url = std::string("https://github.com/") + kRepository + "/releases/download/" +
                                tag + "/bedrock-samples-" + tag + "-full.zip";
        const fs::path zipPath = fs::path(targetDir) / ("bedrock-samples-" + tag + ".zip");
        perlin::Log("samples", "downloading {} (one archive, parallel chunks)", tag);
        job.stage = perlin::assets::Stage::Downloading;
        const auto started = std::chrono::steady_clock::now();
        perlin::download::Options options;
        options.connections = 12;
        options.chunkBytes = 4ull << 20;
        if (!perlin::download::Fetch(url, zipPath, &job.bytes, options)) {
            perlin::Log("samples", "archive download failed");
            return false;
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        perlin::Log("samples", "{} MB in {} s", job.bytes.total.load() / (1024 * 1024), secs);
        const fs::path repo = fs::path(targetDir) / "repo";
        std::error_code ec;
        fs::create_directories(repo, ec);
        const bool ok = ExtractSamplesZip(zipPath, repo);
        fs::remove(zipPath, ec);
        return ok;
    }

    bool DownloadFromManifest(const std::string& targetDir) {
        size_t size = 0;
        void* data = SDL_LoadFile("assets-manifest.json", &size);
        if (!data)
            return false;
        Manifest manifest;
        const std::string text(static_cast<const char*>(data), size);
        SDL_free(data);
        if (glz::read<glz::opts{.error_on_unknown_keys = false}>(manifest, text)) {
            perlin::Log("samples", "assets-manifest.json is malformed");
            return false;
        }
        const std::string prefix = manifest.baseUrl.empty()
                                       ? "/" + manifest.repository + "/" + manifest.ref + "/"
                                       : manifest.baseUrl;
        const fs::path repo = fs::path(targetDir) / "repo";
        std::error_code ec;
        fs::create_directories(repo, ec);
        if (!FetchAll(prefix, manifest.files.core, repo, "core"))
            return false;
        FetchAll(prefix, manifest.files.sounds, repo, "sounds");
        return true;
    }
}

namespace perlin {
    bool DownloadSamples(const std::string& targetDir) {
        const fs::path zipDone = fs::path(targetDir) / "repo" / ".from-release-zip";
        std::error_code zec;
        if (fs::exists(zipDone, zec) || DownloadReleaseZip(targetDir)) {
            std::ofstream(zipDone.string()).close();
            const fs::path skipped = fs::path(targetDir) / "repo" / ".extras-skipped";
            if (fs::exists(skipped, zec))
                return true;
            std::vector<std::string> extra;
            bool listed = true;
            for (const std::string& dir : Directories())
                if (!fs::exists(fs::path(targetDir) / "repo" / dir, zec) && !CollectDirectory(dir, extra))
                    listed = false;
            if (!extra.empty()) {
                perlin::Log("samples", "fetching {} files missing from the release archive", extra.size());
                perlin::assets::Samples().stage = perlin::assets::Stage::Downloading;
                perlin::assets::Samples().bytes.total = 0;
                if (!FetchAll(std::string("/") + kRepository + "/" + kBranch + "/", extra,
                              fs::path(targetDir) / "repo", "extra"))
                    listed = false;
            }
            if (!listed) {
                perlin::Log("samples", "some optional files could not be fetched; continuing with the archive");
                std::ofstream(skipped.string()).close();
            }
            return true;
        }
#ifdef PERLIN_MOBILE
        if (DownloadFromManifest(targetDir))
            return true;
#endif
        perlin::Log("samples", "falling back to downloading files one by one");
        perlin::assets::Samples().bytes.total = 0;
        perlin::assets::Samples().stage = perlin::assets::Stage::Downloading;
        const fs::path repo = fs::path(targetDir) / "repo";
        std::error_code ec;
        fs::create_directories(repo, ec);
        std::vector<std::string> files;
        std::vector<std::string> subdirs;
        bool truncated = false;
        if (!ListTree("resource_pack", false, files, subdirs, truncated))
            return false;
        std::vector<std::string> roots;
        roots.reserve(files.size());
        for (const std::string& f : files)
            roots.push_back(f);
        files = roots;
        for (const std::string& dir : Directories())
            if (!CollectDirectory(dir, files))
                return false;
        perlin::Log("samples", "downloading {} files", files.size());
        const std::string prefix = std::string("/") + kRepository + "/" + kBranch + "/";
        return FetchAll(prefix, files, repo, "samples");
    }
}
#else
namespace perlin {
    bool DownloadSamples(const std::string&) {
        return false;
    }
}
#endif
