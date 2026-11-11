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
    };

    Response Request(const std::string& host, uint16_t port, bool tls, const std::string& method,
                     const std::string& path, const std::string& body = {},
                     const std::string& contentType = {},
                     const std::map<std::string, std::string>& headers = {}, int timeoutSecs = 15);

    bool SystemCaPaths(std::string& file, std::string& dir);

#if defined(__ANDROID__)
    const std::string& AndroidCaBundle();
#endif

    template <class HttpClient> void UseSystemCa(HttpClient& cli) {
#if defined(__ANDROID__)
        const std::string& pem = AndroidCaBundle();
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
    // the browser only lets a page call Microsoft, Xbox and Minecraft services through a relay that adds CORS
    // headers (tools/web/auth-relay): requests to those hosts go to <base>/<host>/<path>. Empty = no relay.
    void SetWebRelay(std::string base);
    bool WebRelayReady();
    // true for the hosts the relay forwards to (sign-in, Xbox Live, Realms, Minecraft services)
    bool NeedsWebRelay(std::string_view host);
#endif
}
