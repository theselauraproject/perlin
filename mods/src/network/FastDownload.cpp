#include "network/FastDownload.hpp"
#include "network/Http.hpp"
#include "util/Log.hpp"

#ifndef __EMSCRIPTEN__
#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace perlin::download {
    namespace {
        struct Url {
            std::string origin;
            std::string path;
        };

        bool Split(const std::string& url, Url& out) {
            const size_t scheme = url.find("://");
            if (scheme == std::string::npos)
                return false;
            const size_t slash = url.find('/', scheme + 3);
            out.origin = url.substr(0, slash);
            out.path = slash == std::string::npos ? "/" : url.substr(slash);
            return !out.origin.empty();
        }

        std::string Absolute(const Url& base, const std::string& location) {
            if (location.starts_with("http://") || location.starts_with("https://"))
                return location;
            if (!location.empty() && location[0] == '/')
                return base.origin + location;
            const size_t slash = base.path.rfind('/');
            return base.origin + base.path.substr(0, slash + 1) + location;
        }

        std::unique_ptr<httplib::Client> MakeClient(const std::string& origin, int timeoutSecs) {
            auto cli = std::make_unique<httplib::Client>(origin);
            cli->set_connection_timeout(timeoutSecs);
            cli->set_read_timeout(timeoutSecs);
            cli->set_write_timeout(timeoutSecs);
            cli->set_follow_location(false);
            cli->set_keep_alive(true);
            if (origin.starts_with("https://"))
                perlin::http::UseSystemCa(*cli);
            cli->set_default_headers({{"User-Agent", "Perlin"}});
#ifdef PERLIN_FASTDL_TEST
            if (const char* proxy = std::getenv("PERLIN_TEST_PROXY_HOST"))
                cli->set_proxy(proxy, std::atoi(std::getenv("PERLIN_TEST_PROXY_PORT")));
            if (const char* ca = std::getenv("PERLIN_TEST_CA"))
                cli->set_ca_cert_path(ca);
#endif
            return cli;
        }

        bool IsRedirect(int status) {
            return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        }

        uint64_t TotalFromContentRange(const std::string& value) {
            const size_t slash = value.rfind('/');
            if (slash == std::string::npos || value.compare(slash + 1, std::string::npos, "*") == 0)
                return 0;
            return std::strtoull(value.c_str() + slash + 1, nullptr, 10);
        }

        bool WriteAll(const fs::path& path, const std::string& body) {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(body.data(), static_cast<std::streamsize>(body.size()));
            return out.good();
        }
    }

    std::string Resolve(const std::string& url, int timeoutSecs) {
        std::string current = url;
        for (int hop = 0; hop < 8; hop++) {
            Url u;
            if (!Split(current, u))
                return {};
            auto cli = MakeClient(u.origin, timeoutSecs);
            httplib::Result res = cli->Head(u.path);
            if (!res)
                return {};
            if (IsRedirect(res->status) && res->has_header("Location")) {
                current = Absolute(u, res->get_header_value("Location"));
                continue;
            }
            return current;
        }
        return {};
    }

    bool Fetch(const std::string& url, const fs::path& target, Progress* progress, const Options& options) {
        Progress local;
        Progress& prog = progress ? *progress : local;
        prog.done = 0;
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        const fs::path part = target.string() + ".part";

        std::string current = url;
        uint64_t total = 0;
        bool ranges = false;
        Url dest;
        for (int hop = 0, failures = 0; hop < 10;) {
            if (!Split(current, dest))
                return false;
            auto cli = MakeClient(dest.origin, options.timeoutSecs);
            httplib::Result res = cli->Get(dest.path, httplib::Headers{{"Range", "bytes=0-0"}});
            if (!res) {
                if (++failures >= 3) {
                    Log("download", "could not reach {}: {}", dest.origin, perlin::http::Describe(res));
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(400 * failures));
                continue;
            }
            if (IsRedirect(res->status) && res->has_header("Location")) {
                current = Absolute(dest, res->get_header_value("Location"));
                hop++;
                continue;
            }
            if (res->status == 206) {
                total = TotalFromContentRange(res->get_header_value("Content-Range"));
                ranges = total > 0;
                break;
            }
            if (res->status == 200) {
                prog.total = res->body.size();
                prog.done = res->body.size();
                if (!WriteAll(part, res->body)) {
                    fs::remove(part, ec);
                    return false;
                }
                fs::rename(part, target, ec);
                return !ec;
            }
            Log("download", "{} answered {}", current, res->status);
            return false;
        }
        if (!ranges)
            return false;
        prog.total = total;

        {
            std::ofstream create(part, std::ios::binary | std::ios::trunc);
            if (!create)
                return false;
        }
        fs::resize_file(part, total, ec);
        if (ec)
            return false;

        const uint64_t chunk = std::max<uint64_t>(options.chunkBytes, 256 * 1024);
        const uint64_t chunks = (total + chunk - 1) / chunk;
        std::atomic<uint64_t> next{0};
        std::atomic<bool> failed{false};
        const int workers = static_cast<int>(
            std::clamp<uint64_t>(chunks, 1, static_cast<uint64_t>(std::max(1, options.connections))));
        auto work = [&] {
            auto cli = MakeClient(dest.origin, options.timeoutSecs);
            std::fstream file(part, std::ios::binary | std::ios::in | std::ios::out);
            if (!file) {
                failed = true;
                return;
            }
            for (;;) {
                if (failed || prog.cancel)
                    return;
                const uint64_t index = next.fetch_add(1);
                if (index >= chunks)
                    return;
                const uint64_t begin = index * chunk;
                const uint64_t end = std::min(total, begin + chunk) - 1;
                bool ok = false;
                for (int attempt = 0; attempt < 5 && !ok && !prog.cancel; attempt++) {
                    uint64_t written = 0;
                    int status = 0;
                    file.clear();
                    file.seekp(static_cast<std::streamoff>(begin));
                    httplib::Result res = cli->Get(
                        dest.path,
                        httplib::Headers{
                            {"Range", "bytes=" + std::to_string(begin) + "-" + std::to_string(end)}},
                        [&](const httplib::Response& r) {
                            status = r.status;
                            return r.status == 206;
                        },
                        [&](const char* data, size_t length) {
                            if (written + length > end - begin + 1 || prog.cancel)
                                return false;
                            file.write(data, static_cast<std::streamsize>(length));
                            written += length;
                            prog.done += length;
                            return file.good();
                        });
                    ok = res && status == 206 && written == end - begin + 1;
                    if (!ok) {
                        prog.done -= written;
                        cli = MakeClient(dest.origin, options.timeoutSecs);
                        std::this_thread::sleep_for(std::chrono::milliseconds(300 * (attempt + 1)));
                    }
                }
                if (!ok) {
                    Log("download", "chunk {} of {} failed", index, current);
                    failed = true;
                    return;
                }
            }
        };
        std::vector<std::thread> threads;
        threads.reserve(workers);
        for (int i = 0; i < workers; i++)
            threads.emplace_back(work);
        for (std::thread& t : threads)
            t.join();
        if (failed || prog.cancel) {
            fs::remove(part, ec);
            return false;
        }
        fs::rename(part, target, ec);
        return !ec;
    }
}
#else
namespace perlin::download {
    bool Fetch(const std::string&, const std::filesystem::path&, Progress*, const Options&) {
        return false;
    }
    std::string Resolve(const std::string&, int) {
        return {};
    }
}
#endif
