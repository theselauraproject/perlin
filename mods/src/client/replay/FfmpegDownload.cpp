#include "client/replay/FfmpegDownload.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "network/Http.hpp"
#include "util/Paths.hpp"

#ifndef __EMSCRIPTEN__
#include <httplib.h>
#endif
#include <miniz.h>

namespace perlin::replay::ffmpeg {
    namespace fs = std::filesystem;

    bool Downloader::Supported() {
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
        return true;
#else
        return false;
#endif
    }

    Downloader::~Downloader() {
        cancel = true;
        Join();
    }

    void Downloader::Join() {
        if (worker.joinable())
            worker.join();
    }

    float Downloader::Progress() const {
        const long long t = total.load();
        return t > 0 ? static_cast<float>(done.load()) / static_cast<float>(t) : -1.0f;
    }

    std::string Downloader::Status() {
        std::lock_guard<std::mutex> lk(mutex);
        return status;
    }

    void Downloader::Start() {
        if (running.load())
            return;
        Join();
        running = true;
        finished = false;
        succeeded = false;
        cancel = false;
        done = 0;
        total = 0;
        {
            std::lock_guard<std::mutex> lk(mutex);
            status = "Connecting...";
        }
        worker = std::thread([this] { Run(); });
    }

    void Downloader::Run() {
        const auto setStatus = [this](const std::string& s) {
            std::lock_guard<std::mutex> lk(mutex);
            status = s;
        };
        bool ok = false;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
        do {
            std::error_code ec;
            const fs::path dir = fs::path(paths::DataDir()) / "ffmpeg";
            fs::create_directories(dir, ec);
            const fs::path zip = dir / "download.zip.part";
            {
                std::ofstream out(zip, std::ios::binary | std::ios::trunc);
                if (!out) {
                    setStatus("Could not write to " + dir.string());
                    break;
                }
                httplib::SSLClient cli("github.com", 443);
                perlin::http::UseSystemCa(cli);
                cli.set_connection_timeout(20);
                cli.set_read_timeout(60);
                cli.set_follow_location(true);
                cli.set_default_headers({{"User-Agent", "Perlin"}});
                setStatus("Downloading ffmpeg...");
                bool first = true;
                auto res = cli.Get(
                    "/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip",
                    [&](const httplib::Response& r) {
                        if (r.status != 200)
                            return false;
                        if (first) {
                            first = false;
                            total = std::atoll(r.get_header_value("Content-Length").c_str());
                        }
                        return true;
                    },
                    [&](const char* data, size_t n) {
                        if (cancel.load())
                            return false;
                        out.write(data, static_cast<std::streamsize>(n));
                        done += static_cast<long long>(n);
                        return out.good();
                    });
                out.close();
                if (cancel.load()) {
                    setStatus("Cancelled");
                    break;
                }
                if (!res || res->status != 200) {
                    setStatus(res ? "Download failed (HTTP " + std::to_string(res->status) + ")"
                                  : "Download failed (no connection)");
                    break;
                }
            }
            setStatus("Unpacking...");
            mz_zip_archive za{};
            if (!mz_zip_reader_init_file(&za, zip.string().c_str(), 0)) {
                setStatus("The download was corrupt");
                break;
            }
            bool wrote = false;
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&za) && !wrote; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&za, i, &st) || st.m_is_directory)
                    continue;
                const std::string name = st.m_filename;
                const bool wanted =
                    name.size() > 15 && (name.compare(name.size() - 15, 15, "/bin/ffmpeg.exe") == 0);
                if (!wanted)
                    continue;
                const fs::path target = dir / "ffmpeg.exe";
                const fs::path temp = dir / "ffmpeg.exe.part";
                wrote = mz_zip_reader_extract_to_file(&za, i, temp.string().c_str(), 0) != 0;
                if (wrote) {
                    fs::remove(target, ec);
                    fs::rename(temp, target, ec);
                    wrote = !ec;
                }
            }
            mz_zip_reader_end(&za);
            fs::remove(zip, ec);
            if (!wrote) {
                setStatus("ffmpeg.exe was not found in the download");
                break;
            }
            ok = true;
            setStatus("ffmpeg installed");
        } while (false);
#else
        setStatus(
            "Automatic download is only available on Windows. Install ffmpeg with your package manager.");
#endif
        succeeded = ok;
        running = false;
        finished = true;
    }
}
