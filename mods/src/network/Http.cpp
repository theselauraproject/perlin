#include "network/Http.hpp"
#include <httplib.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <mutex>
#include "util/Log.hpp"
#include "util/Paths.hpp"
#include "util/Platform.hpp"
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace perlin::http {
#if !defined(__ANDROID__) && !defined(_WIN32) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    namespace {
        bool IsFile(const char* path) {
            struct stat st {};
            return path && *path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
        }
        bool IsDir(const char* path) {
            struct stat st {};
            return path && *path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        }
    }
#endif

#if defined(__ANDROID__)
    const std::string& AndroidCaBundle() {
        static std::once_flag once;
        static std::string bundle;
        std::call_once(once, [] {
            namespace fs = std::filesystem;
            static const char* const kDirs[] = {"/apex/com.android.conscrypt/cacerts",
                                                "/system/etc/security/cacerts"};
            int count = 0;
            for (const char* dir : kDirs) {
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(dir, ec)) {
                    std::ifstream in(entry.path(), std::ios::binary);
                    if (!in)
                        continue;
                    const std::string text((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
                    const size_t begin = text.find("-----BEGIN CERTIFICATE-----");
                    const size_t end = text.find("-----END CERTIFICATE-----");
                    if (begin == std::string::npos || end == std::string::npos || end < begin)
                        continue;
                    bundle.append(text, begin, end + 25 - begin);
                    bundle.push_back('\n');
                    count++;
                }
                if (count > 0)
                    break;
            }
            Log("tls", "{} system CA certificates loaded", count);
        });
        return bundle;
    }
#endif

    bool SystemCaPaths(std::string& file, std::string& dir) {
#if defined(__ANDROID__)
        file.clear();
        dir = "/system/etc/security/cacerts";
        return true;
#elif defined(PERLIN_IOS)
        dir.clear();
        file = paths::ExeDir() + "/cacert.pem";
        return true;
#elif defined(_WIN32) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
        (void)file;
        (void)dir;
        return false;
#else
        static std::once_flag once;
        static std::string caFile, caDir;
        std::call_once(once, [] {
            if (IsFile(std::getenv("SSL_CERT_FILE")))
                caFile = std::getenv("SSL_CERT_FILE");
            if (IsDir(std::getenv("SSL_CERT_DIR")))
                caDir = std::getenv("SSL_CERT_DIR");
            static const char* const kFiles[] = {
                "/etc/ssl/certs/ca-certificates.crt",
                "/etc/pki/tls/certs/ca-bundle.crt",
                "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
                "/etc/ca-certificates/extracted/tls-ca-bundle.pem",
                "/etc/ssl/ca-bundle.pem",
                "/var/lib/ca-certificates/ca-bundle.pem",
                "/etc/ssl/cert.pem",
                "/usr/local/share/certs/ca-root-nss.crt",
                "/etc/certs/ca-certificates.crt",
            };
            static const char* const kDirs[] = {"/etc/ssl/certs", "/etc/pki/tls/certs",
                                                "/system/etc/security/cacerts"};
            for (const char* f : kFiles)
                if (caFile.empty() && IsFile(f))
                    caFile = f;
            for (const char* d : kDirs)
                if (caDir.empty() && IsDir(d))
                    caDir = d;
            if (caFile.empty() && caDir.empty())
                Log("tls", "no CA certificates found (set SSL_CERT_FILE); HTTPS downloads will fail");
            else
                Log("tls", "CA certificates: {}, {}", (caFile.empty() ? "-" : caFile),
                    (caDir.empty() ? "-" : caDir));
        });
        file = caFile;
        dir = caDir;
        return !file.empty() || !dir.empty();
#endif
    }

    template <class HttpResult> std::string Describe(const HttpResult& res) {
        std::string out = httplib::to_string(res.error());
        if (res.ssl_error() != 0 || res.ssl_backend_error() != 0)
            out += " (ssl error " + std::to_string(res.ssl_error()) + ", backend " +
                   std::to_string(res.ssl_backend_error()) + ")";
        return out;
    }
    template std::string Describe<httplib::Result>(const httplib::Result&);

    Response Request(const std::string& host, uint16_t port, bool tls, const std::string& method,
                     const std::string& path, const std::string& body, const std::string& contentType,
                     const std::map<std::string, std::string>& extra, int timeoutSecs) {
        auto run = [&](auto& cli) -> Response {
            cli.set_connection_timeout(timeoutSecs);
            cli.set_read_timeout(timeoutSecs);
            cli.set_write_timeout(timeoutSecs);
            cli.set_follow_location(true);
            httplib::Headers headers;
            for (auto& [k, v] : extra)
                headers.emplace(k, v);
            httplib::Result res = method == "GET"      ? cli.Get(path, headers)
                                  : method == "PUT"    ? cli.Put(path, headers, body, contentType)
                                  : method == "DELETE" ? cli.Delete(path, headers, body, contentType)
                                                       : cli.Post(path, headers, body, contentType);
            if (!res) {
                Log("http", "{} {}{} failed: {}", method, host, path.substr(0, path.find('?')),
                    Describe(res));
                return {};
            }
            Response out{res->status, res->body, {}};
            for (auto& [k, v] : res->headers) {
                std::string key = k;
                for (char& c : key)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                out.headers.emplace(std::move(key), v);
            }
            return out;
        };
        if (tls) {
            httplib::SSLClient cli(host, port);
            UseSystemCa(cli);
            return run(cli);
        }
        httplib::Client cli(host, port);
        return run(cli);
    }

    Response GetUrl(const std::string& url, const std::map<std::string, std::string>& headers,
                    int timeoutSecs) {
        size_t start = url.find("://");
        const bool tls = url.compare(0, 5, "https") == 0;
        start = start == std::string::npos ? 0 : start + 3;
        const size_t slash = url.find('/', start);
        std::string host = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        const std::string path = slash == std::string::npos ? "/" : url.substr(slash);
        uint16_t port = tls ? 443 : 80;
        if (const size_t colon = host.find(':'); colon != std::string::npos) {
            port = static_cast<uint16_t>(std::atoi(host.c_str() + colon + 1));
            host.resize(colon);
        }
        std::map<std::string, std::string> extra = headers;
        extra.emplace("User-Agent", "Perlin");
        return Request(host, port, tls, "GET", path, {}, {}, extra, timeoutSecs);
    }
}
