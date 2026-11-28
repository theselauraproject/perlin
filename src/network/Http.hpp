#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace perlin::http {
    struct Response {
        int status = 0;
        std::string body;
        std::map<std::string, std::string> headers;
        std::string error;
    };

    Response Request(const std::string& host, uint16_t port, bool tls, const std::string& method,
                     const std::string& path, const std::string& body = {},
                     const std::string& contentType = {},
                     const std::map<std::string, std::string>& headers = {}, int timeoutSecs = 15);

    bool SystemCaPaths(std::string& file, std::string& dir);

#if defined(_WIN32)
    const std::string& CombinedCaPem();
#endif

#if defined(__ANDROID__)
    const std::string& AndroidCaBundle();

    // Android's own HTTP stack, reached over JNI (Http_android.cpp). Request returns false only when the
    // bridge cannot be reached at all; otherwise `out` holds the result, with status 0 and `error` set when
    // the request did not complete.
    namespace android {
        bool Available();
        bool Request(const std::string& url, const std::string& method,
                     const std::map<std::string, std::string>& headers, const std::string& body, int timeoutSecs,
                     Response& out);
        // Streams a GET into `path`. `state` (optional) is shared with the Java side: uint64 bytes done at
        // offset 0, uint64 total at 8, and a cancel byte at 16 that may be set while the download runs.
        bool DownloadFile(const std::string& url, const std::string& path, void* state, size_t stateBytes,
                          int timeoutSecs, std::string& error);
    }
#endif

    template <class HttpClient> void UseSystemCa(HttpClient& cli) {
#if defined(__ANDROID__)
        const std::string& pem = AndroidCaBundle();
        if (!pem.empty()) {
            cli.load_ca_cert_store(pem.c_str(), pem.size());
            return;
        }
#endif
#if defined(_WIN32)
        const std::string& pem = CombinedCaPem();
        if (!pem.empty()) {
            cli.load_ca_cert_store(pem.c_str(), pem.size());
            return;
        }
#endif
        std::string file, dir;
        if (SystemCaPaths(file, dir))
            cli.set_ca_cert_path(file, dir);
    }

    template <class HttpResult> std::string Describe(const HttpResult& res);

    Response GetUrl(const std::string& url, const std::map<std::string, std::string>& headers = {},
                    int timeoutSecs = 60);

#if defined(__EMSCRIPTEN__)

    void SetWebRelay(std::string base);
    bool WebRelayReady();

    bool NeedsWebRelay(std::string_view host);
#endif
}
