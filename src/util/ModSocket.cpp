#include "util/ModSocket.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) || defined(PERLIN_SOCKET_TLS)
#define PERLIN_MODSOCKET_TLS 1
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace perlin::util {
    namespace {
        using Clock = std::chrono::steady_clock;

#ifdef _WIN32
        using Handle = SOCKET;
        constexpr Handle kBad = INVALID_SOCKET;
        void CloseHandle_(Handle h) { closesocket(h); }
        bool WouldBlock() {
            const int e = WSAGetLastError();
            return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEINTR;
        }
        void SetNonBlocking(Handle h) {
            u_long on = 1;
            ioctlsocket(h, FIONBIO, &on);
        }
        void InitNetwork() {
            static const bool once = [] {
                WSADATA data;
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            (void)once;
        }
        int PollOne(Handle h, bool write, int ms) {
            WSAPOLLFD p{};
            p.fd = h;
            p.events = write ? POLLWRNORM : POLLRDNORM;
            return WSAPoll(&p, 1, ms);
        }
#else
        using Handle = int;
        constexpr Handle kBad = -1;
        void CloseHandle_(Handle h) { ::close(h); }
        bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
        void SetNonBlocking(Handle h) {
            const int flags = fcntl(h, F_GETFL, 0);
            fcntl(h, F_SETFL, flags | O_NONBLOCK);
        }
        void InitNetwork() {}
        int PollOne(Handle h, bool write, int ms) {
            pollfd p{};
            p.fd = h;
            p.events = write ? POLLOUT : POLLIN;
            return poll(&p, 1, ms);
        }
#endif

        std::string LastError() {
#ifdef _WIN32
            return "network error " + std::to_string(WSAGetLastError());
#else
            return std::strerror(errno);
#endif
        }

        struct Url {
            std::string scheme, host, path;
            int port = 0;
        };

        bool ParseUrl(const std::string& url, Url& out) {
            const size_t schemeEnd = url.find("://");
            if (schemeEnd == std::string::npos || url.size() > 2048)
                return false;
            out.scheme = url.substr(0, schemeEnd);
            for (char& c : out.scheme)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            size_t at = schemeEnd + 3;
            const size_t pathAt = url.find_first_of("/?#", at);
            std::string authority = url.substr(at, pathAt == std::string::npos ? std::string::npos : pathAt - at);
            out.path = pathAt == std::string::npos ? "/" : url.substr(pathAt);
            if (out.path[0] != '/')
                out.path.insert(out.path.begin(), '/');
            const size_t hash = out.path.find('#');
            if (hash != std::string::npos)
                out.path.erase(hash);
            if (authority.empty() || authority.find('@') != std::string::npos)
                return false;
            std::string portText;
            if (authority[0] == '[') {
                const size_t close = authority.find(']');
                if (close == std::string::npos)
                    return false;
                out.host = authority.substr(1, close - 1);
                if (close + 1 < authority.size()) {
                    if (authority[close + 1] != ':')
                        return false;
                    portText = authority.substr(close + 2);
                }
            } else {
                const size_t colon = authority.rfind(':');
                if (colon == std::string::npos) {
                    out.host = authority;
                } else {
                    out.host = authority.substr(0, colon);
                    portText = authority.substr(colon + 1);
                }
            }
            if (out.host.empty() || out.host.size() > 253)
                return false;
            if (!portText.empty()) {
                if (portText.size() > 5 || !std::all_of(portText.begin(), portText.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    return false;
                out.port = std::atoi(portText.c_str());
            } else if (out.scheme == "ws") {
                out.port = 80;
            } else if (out.scheme == "wss") {
                out.port = 443;
            }
            return out.port > 0 && out.port <= 65535;
        }

        std::array<uint8_t, 20> Sha1(const std::string& text) {
            uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
            std::vector<uint8_t> msg(text.begin(), text.end());
            const uint64_t bits = static_cast<uint64_t>(msg.size()) * 8;
            msg.push_back(0x80);
            while (msg.size() % 64 != 56)
                msg.push_back(0);
            for (int i = 7; i >= 0; i--)
                msg.push_back(static_cast<uint8_t>(bits >> (i * 8)));
            const auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
            for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
                uint32_t w[80];
                for (int i = 0; i < 16; i++)
                    w[i] = static_cast<uint32_t>(msg[chunk + i * 4]) << 24 | static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16 |
                           static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8 | static_cast<uint32_t>(msg[chunk + i * 4 + 3]);
                for (int i = 16; i < 80; i++)
                    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
                for (int i = 0; i < 80; i++) {
                    uint32_t f, k;
                    if (i < 20) {
                        f = (b & c) | (~b & d);
                        k = 0x5A827999u;
                    } else if (i < 40) {
                        f = b ^ c ^ d;
                        k = 0x6ED9EBA1u;
                    } else if (i < 60) {
                        f = (b & c) | (b & d) | (c & d);
                        k = 0x8F1BBCDCu;
                    } else {
                        f = b ^ c ^ d;
                        k = 0xCA62C1D6u;
                    }
                    const uint32_t t = rol(a, 5) + f + e + k + w[i];
                    e = d;
                    d = c;
                    c = rol(b, 30);
                    b = a;
                    a = t;
                }
                h[0] += a;
                h[1] += b;
                h[2] += c;
                h[3] += d;
                h[4] += e;
            }
            std::array<uint8_t, 20> out{};
            for (int i = 0; i < 5; i++)
                for (int j = 0; j < 4; j++)
                    out[static_cast<size_t>(i * 4 + j)] = static_cast<uint8_t>(h[i] >> (24 - j * 8));
            return out;
        }

        std::string Base64(const uint8_t* p, size_t n) {
            static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            for (size_t i = 0; i < n; i += 3) {
                const uint32_t v = static_cast<uint32_t>(p[i]) << 16 | (i + 1 < n ? static_cast<uint32_t>(p[i + 1]) << 8 : 0u) |
                                   (i + 2 < n ? static_cast<uint32_t>(p[i + 2]) : 0u);
                out.push_back(kAlphabet[(v >> 18) & 63]);
                out.push_back(kAlphabet[(v >> 12) & 63]);
                out.push_back(i + 1 < n ? kAlphabet[(v >> 6) & 63] : '=');
                out.push_back(i + 2 < n ? kAlphabet[v & 63] : '=');
            }
            return out;
        }

        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string Trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t'))
                a++;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
                b--;
            return s.substr(a, b - a);
        }

        bool Clean(const std::string& s) { return s.find_first_of("\r\n") == std::string::npos; }

        uint32_t RandomWord() {
            thread_local std::mt19937 rng{std::random_device{}()};
            return rng();
        }

        std::vector<uint8_t> MakeFrame(uint8_t opcode, const uint8_t* p, size_t n) {
            std::vector<uint8_t> f;
            f.reserve(n + 14);
            f.push_back(static_cast<uint8_t>(0x80 | opcode));
            if (n < 126) {
                f.push_back(static_cast<uint8_t>(0x80 | n));
            } else if (n <= 0xFFFF) {
                f.push_back(0x80 | 126);
                f.push_back(static_cast<uint8_t>(n >> 8));
                f.push_back(static_cast<uint8_t>(n));
            } else {
                f.push_back(0x80 | 127);
                for (int i = 7; i >= 0; i--)
                    f.push_back(static_cast<uint8_t>(static_cast<uint64_t>(n) >> (i * 8)));
            }
            const uint32_t key = RandomWord();
            const uint8_t mask[4] = {static_cast<uint8_t>(key), static_cast<uint8_t>(key >> 8), static_cast<uint8_t>(key >> 16),
                                     static_cast<uint8_t>(key >> 24)};
            f.insert(f.end(), mask, mask + 4);
            for (size_t i = 0; i < n; i++)
                f.push_back(p[i] ^ mask[i & 3]);
            return f;
        }

        struct Conn {
            Handle fd = kBad;
#ifdef PERLIN_MODSOCKET_TLS
            SSL_CTX* ctx = nullptr;
            SSL* ssl = nullptr;
#endif
            ~Conn() {
#ifdef PERLIN_MODSOCKET_TLS
                if (ssl) {
                    SSL_free(ssl);
                    ssl = nullptr;
                }
                if (ctx) {
                    SSL_CTX_free(ctx);
                    ctx = nullptr;
                }
#endif
                if (fd != kBad)
                    CloseHandle_(fd);
            }

            bool Connect(const std::string& host, int port, int timeoutMs, const std::atomic<bool>& stop, std::string& err) {
                InitNetwork();
                addrinfo hints{};
                hints.ai_family = AF_UNSPEC;
                hints.ai_socktype = SOCK_STREAM;
                addrinfo* list = nullptr;
                const std::string portText = std::to_string(port);
                if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &list) != 0 || !list) {
                    err = "cannot resolve " + host;
                    return false;
                }
                const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
                std::string lastErr = "cannot connect";
                for (addrinfo* a = list; a && !stop.load(); a = a->ai_next) {
                    Handle s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
                    if (s == kBad)
                        continue;
                    SetNonBlocking(s);
                    int rc = ::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
                    bool ok = rc == 0;
                    if (!ok && WouldBlock()) {
                        while (!stop.load() && Clock::now() < deadline) {
                            if (PollOne(s, true, 50) > 0) {
                                int soErr = 0;
                                socklen_t len = sizeof(soErr);
                                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len);
                                ok = soErr == 0;
                                if (!ok)
                                    lastErr = "connection refused";
                                break;
                            }
                        }
                    } else if (!ok) {
                        lastErr = LastError();
                    }
                    if (ok) {
                        int one = 1;
                        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_NOSIGPIPE
                        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
                        fd = s;
                        freeaddrinfo(list);
                        return true;
                    }
                    CloseHandle_(s);
                }
                freeaddrinfo(list);
                err = stop.load() ? "closed" : lastErr;
                return false;
            }

            bool Tls(const std::string& host, int timeoutMs, const std::atomic<bool>& stop, std::string& err) {
#ifdef PERLIN_MODSOCKET_TLS
                ctx = SSL_CTX_new(TLS_client_method());
                if (!ctx) {
                    err = "tls unavailable";
                    return false;
                }
                SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
                SSL_CTX_set_default_verify_paths(ctx);
                SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
                ssl = SSL_new(ctx);
                if (!ssl) {
                    err = "tls unavailable";
                    return false;
                }
                SSL_set_fd(ssl, static_cast<int>(fd));
                SSL_set_tlsext_host_name(ssl, host.c_str());
                SSL_set1_host(ssl, host.c_str());
                const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
                while (!stop.load() && Clock::now() < deadline) {
                    const int rc = SSL_connect(ssl);
                    if (rc == 1)
                        return true;
                    const int code = SSL_get_error(ssl, rc);
                    if (code == SSL_ERROR_WANT_READ)
                        PollOne(fd, false, 50);
                    else if (code == SSL_ERROR_WANT_WRITE)
                        PollOne(fd, true, 50);
                    else {
                        const long verify = SSL_get_verify_result(ssl);
                        err = verify != X509_V_OK ? std::string("tls certificate: ") + X509_verify_cert_error_string(verify)
                                                  : "tls handshake failed";
                        return false;
                    }
                }
                err = stop.load() ? "closed" : "tls handshake timed out";
                return false;
#else
                (void)host;
                (void)timeoutMs;
                (void)stop;
                err = "tls is not available in this build";
                return false;
#endif
            }

            long Read(uint8_t* buf, size_t n, std::string& err) {
#ifdef PERLIN_MODSOCKET_TLS
                if (ssl) {
                    if (SSL_pending(ssl) == 0 && PollOne(fd, false, 20) <= 0)
                        return 0;
                    const int rc = SSL_read(ssl, buf, static_cast<int>(n));
                    if (rc > 0)
                        return rc;
                    const int code = SSL_get_error(ssl, rc);
                    if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE)
                        return 0;
                    if (code != SSL_ERROR_ZERO_RETURN)
                        err = "connection lost";
                    return -1;
                }
#endif
                if (PollOne(fd, false, 20) <= 0)
                    return 0;
                const auto rc = ::recv(fd, reinterpret_cast<char*>(buf), static_cast<int>(n), 0);
                if (rc > 0)
                    return static_cast<long>(rc);
                if (rc < 0 && WouldBlock())
                    return 0;
                if (rc < 0)
                    err = LastError();
                return -1;
            }

            bool WriteAll(const uint8_t* p, size_t n, const std::atomic<bool>& stop, std::string& err) {
                const auto deadline = Clock::now() + std::chrono::seconds(30);
                size_t sent = 0;
                while (sent < n) {
                    if (stop.load() || Clock::now() > deadline) {
                        err = stop.load() ? "closed" : "send timed out";
                        return false;
                    }
#ifdef PERLIN_MODSOCKET_TLS
                    if (ssl) {
                        const int rc = SSL_write(ssl, p + sent, static_cast<int>(std::min<size_t>(n - sent, 1u << 20)));
                        if (rc > 0) {
                            sent += static_cast<size_t>(rc);
                            continue;
                        }
                        const int code = SSL_get_error(ssl, rc);
                        if (code == SSL_ERROR_WANT_WRITE) {
                            PollOne(fd, true, 50);
                            continue;
                        }
                        if (code == SSL_ERROR_WANT_READ) {
                            PollOne(fd, false, 50);
                            continue;
                        }
                        err = "connection lost";
                        return false;
                    }
#endif
                    int flags = 0;
#ifdef MSG_NOSIGNAL
                    flags = MSG_NOSIGNAL;
#endif
                    const auto rc = ::send(fd, reinterpret_cast<const char*>(p + sent), static_cast<int>(std::min<size_t>(n - sent, 1u << 20)), flags);
                    if (rc > 0) {
                        sent += static_cast<size_t>(rc);
                        continue;
                    }
                    if (rc < 0 && WouldBlock()) {
                        PollOne(fd, true, 50);
                        continue;
                    }
                    err = rc < 0 ? LastError() : "connection lost";
                    return false;
                }
                return true;
            }
        };

        bool ValidCloseCode(uint32_t c) { return c == 1000 || c == 1001 || c == 1008 || c == 1011 || (c >= 3000 && c <= 4999); }
    }

    struct SocketSet::Socket {
        struct Outgoing {
            bool binary = true;
            std::vector<uint8_t> data;
        };
        uint32_t id = 0;
        SocketOpenOptions options;
        std::thread thread;
        std::atomic<bool> stop{false};
        std::atomic<bool> done{false};
        std::atomic<bool> closeRequested{false};
        std::mutex mutex;
        std::deque<Outgoing> outgoing;
        size_t queuedBytes = 0;
        uint32_t closeCode = 1000;
        std::string closeReason;
    };

    SocketSet::SocketSet() = default;

    SocketSet::~SocketSet() { CloseAll(); }

    bool SocketSet::TlsAvailable() {
#ifdef PERLIN_MODSOCKET_TLS
        return true;
#else
        return false;
#endif
    }

    void SocketSet::Push(SocketEvent&& event) {
        std::lock_guard lock(eventMutex);
        events.push_back(std::move(event));
    }

    void SocketSet::Poll(std::vector<SocketEvent>& out) {
        {
            std::lock_guard lock(eventMutex);
            while (!events.empty()) {
                out.push_back(std::move(events.front()));
                events.pop_front();
            }
        }
        Reap();
    }

    void SocketSet::Reap() {
        std::vector<std::shared_ptr<Socket>> finished;
        {
            std::lock_guard lock(mutex);
            for (auto it = sockets.begin(); it != sockets.end();) {
                if ((*it)->done.load()) {
                    finished.push_back(*it);
                    it = sockets.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& s : finished)
            if (s->thread.joinable())
                s->thread.join();
    }

    SocketResult SocketSet::Open(SocketOpenOptions options) {
        Url url;
        if (!ParseUrl(options.url, url))
            return SocketResult::BadArguments;
        const bool ws = url.scheme == "ws" || url.scheme == "wss";
        const bool tcp = url.scheme == "tcp" || url.scheme == "tls";
        if (!ws && !tcp)
            return SocketResult::BadArguments;
        if (ws != options.webSocket)
            return SocketResult::BadArguments;
        const bool tls = url.scheme == "wss" || url.scheme == "tls";
        if (tls && !TlsAvailable())
            return SocketResult::Unavailable;
        if (options.headers.size() > 32 || options.protocols.size() > 256 || !Clean(options.protocols))
            return SocketResult::BadArguments;
        for (const auto& [k, v] : options.headers)
            if (k.empty() || k.size() > 128 || v.size() > 4096 || !Clean(k) || !Clean(v) || k.find(':') != std::string::npos)
                return SocketResult::BadArguments;
        options.timeoutMs = std::clamp(options.timeoutMs, 500, 60000);

        Reap();
        auto socket = std::make_shared<Socket>();
        socket->id = options.id;
        socket->options = std::move(options);
        {
            std::lock_guard lock(mutex);
            if (sockets.size() >= kMaxOpen)
                return SocketResult::Unavailable;
            for (const auto& s : sockets)
                if (s->id == socket->id)
                    return SocketResult::BadArguments;
            sockets.push_back(socket);
        }
        socket->thread = std::thread([this, socket] { Run(socket); });
        return SocketResult::Ok;
    }

    SocketResult SocketSet::Send(uint32_t id, bool binary, std::vector<uint8_t> data) {
        if (data.size() > kMaxMessage)
            return SocketResult::BadArguments;
        std::shared_ptr<Socket> target;
        {
            std::lock_guard lock(mutex);
            for (const auto& s : sockets)
                if (s->id == id && !s->done.load())
                    target = s;
        }
        if (!target || target->closeRequested.load())
            return SocketResult::NotFound;
        std::lock_guard lock(target->mutex);
        if (target->queuedBytes + data.size() > kMaxQueuedBytes)
            return SocketResult::Unavailable;
        target->queuedBytes += data.size();
        target->outgoing.push_back({binary, std::move(data)});
        return SocketResult::Ok;
    }

    SocketResult SocketSet::Close(uint32_t id, uint32_t code, std::string reason) {
        std::shared_ptr<Socket> target;
        {
            std::lock_guard lock(mutex);
            for (const auto& s : sockets)
                if (s->id == id && !s->done.load())
                    target = s;
        }
        if (!target)
            return SocketResult::NotFound;
        if (reason.size() > 120)
            reason.resize(120);
        {
            std::lock_guard lock(target->mutex);
            target->closeCode = ValidCloseCode(code) ? code : 1000;
            target->closeReason = std::move(reason);
        }
        target->closeRequested.store(true);
        return SocketResult::Ok;
    }

    void SocketSet::CloseAll() {
        std::vector<std::shared_ptr<Socket>> all;
        {
            std::lock_guard lock(mutex);
            all.swap(sockets);
        }
        for (auto& s : all)
            s->stop.store(true);
        for (auto& s : all)
            if (s->thread.joinable())
                s->thread.join();
    }

    void SocketSet::Run(std::shared_ptr<Socket> socket) {
        const SocketOpenOptions& o = socket->options;
        Url url;
        ParseUrl(o.url, url);
        const bool tls = url.scheme == "wss" || url.scheme == "tls";
        std::string error, subprotocol;
        uint32_t closeCode = 0;
        std::string closeReason;
        bool opened = false;

        const auto finish = [&] {
            SocketEvent e;
            e.kind = SocketEvent::Kind::Close;
            e.id = socket->id;
            e.code = closeCode;
            e.text = closeReason;
            e.error = error;
            Push(std::move(e));
            socket->done.store(true);
        };

        Conn conn;
        if (!conn.Connect(url.host, url.port, o.timeoutMs, socket->stop, error) ||
            (tls && !conn.Tls(url.host, o.timeoutMs, socket->stop, error))) {
            finish();
            return;
        }

        std::vector<uint8_t> inbuf;
        if (o.webSocket) {
            uint8_t raw[16];
            for (uint8_t& b : raw)
                b = static_cast<uint8_t>(RandomWord());
            const std::string key = Base64(raw, sizeof(raw));
            std::string req = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host;
            const bool defaultPort = (url.scheme == "ws" && url.port == 80) || (url.scheme == "wss" && url.port == 443);
            if (!defaultPort)
                req += ":" + std::to_string(url.port);
            req += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key + "\r\n";
            if (!o.protocols.empty())
                req += "Sec-WebSocket-Protocol: " + o.protocols + "\r\n";
            for (const auto& [k, v] : o.headers) {
                const std::string name = Lower(k);
                if (name == "host" || name == "upgrade" || name == "connection" || name.starts_with("sec-websocket-"))
                    continue;
                req += k + ": " + v + "\r\n";
            }
            req += "\r\n";
            if (!conn.WriteAll(reinterpret_cast<const uint8_t*>(req.data()), req.size(), socket->stop, error)) {
                finish();
                return;
            }
            const auto deadline = Clock::now() + std::chrono::milliseconds(o.timeoutMs);
            size_t headEnd = std::string::npos;
            std::string head;
            while (headEnd == std::string::npos) {
                if (socket->stop.load() || Clock::now() > deadline || head.size() > 16384) {
                    error = socket->stop.load() ? "closed" : "websocket handshake timed out";
                    finish();
                    return;
                }
                uint8_t buf[2048];
                const long n = conn.Read(buf, sizeof(buf), error);
                if (n < 0) {
                    if (error.empty())
                        error = "connection closed during the handshake";
                    finish();
                    return;
                }
                head.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
                headEnd = head.find("\r\n\r\n");
            }
            inbuf.assign(head.begin() + static_cast<long>(headEnd) + 4, head.end());
            head.resize(headEnd);
            const size_t lineEnd = head.find("\r\n");
            const std::string status = head.substr(0, lineEnd);
            if (status.size() < 12 || status.compare(9, 3, "101") != 0) {
                error = "websocket handshake rejected: " + status;
                finish();
                return;
            }
            const std::string expected = [&] {
                const auto sha = Sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
                return Base64(sha.data(), sha.size());
            }();
            bool accepted = false;
            size_t at = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
            while (at < head.size()) {
                size_t next = head.find("\r\n", at);
                if (next == std::string::npos)
                    next = head.size();
                const std::string line = head.substr(at, next - at);
                at = next + 2;
                const size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                const std::string name = Lower(Trim(line.substr(0, colon)));
                const std::string value = Trim(line.substr(colon + 1));
                if (name == "sec-websocket-accept")
                    accepted = value == expected;
                else if (name == "sec-websocket-protocol")
                    subprotocol = value;
            }
            if (!accepted) {
                error = "websocket handshake failed: bad accept key";
                finish();
                return;
            }
        }

        {
            SocketEvent e;
            e.kind = SocketEvent::Kind::Open;
            e.id = socket->id;
            e.text = subprotocol;
            Push(std::move(e));
            opened = true;
        }
        (void)opened;

        std::vector<uint8_t> message;
        bool messageBinary = false, inMessage = false;
        bool peerClosed = false;

        const auto sendClose = [&](uint32_t code, const std::string& reason) {
            if (!o.webSocket)
                return;
            std::vector<uint8_t> payload;
            payload.push_back(static_cast<uint8_t>(code >> 8));
            payload.push_back(static_cast<uint8_t>(code));
            payload.insert(payload.end(), reason.begin(), reason.end());
            const std::vector<uint8_t> f = MakeFrame(8, payload.data(), payload.size());
            std::string ignored;
            std::atomic<bool> never{false};
            conn.WriteAll(f.data(), f.size(), never, ignored);
        };

        const auto parseFrames = [&]() -> bool {
            for (;;) {
                if (inbuf.size() < 2)
                    return true;
                const bool fin = (inbuf[0] & 0x80) != 0;
                const uint8_t opcode = inbuf[0] & 0x0F;
                const bool masked = (inbuf[1] & 0x80) != 0;
                uint64_t len = inbuf[1] & 0x7F;
                size_t pos = 2;
                if (len == 126) {
                    if (inbuf.size() < 4)
                        return true;
                    len = static_cast<uint64_t>(inbuf[2]) << 8 | inbuf[3];
                    pos = 4;
                } else if (len == 127) {
                    if (inbuf.size() < 10)
                        return true;
                    len = 0;
                    for (int i = 0; i < 8; i++)
                        len = len << 8 | inbuf[static_cast<size_t>(2 + i)];
                    pos = 10;
                }
                if (len > kMaxMessage || (opcode >= 8 && len > 125)) {
                    closeCode = 1009;
                    closeReason = "message too large";
                    sendClose(1009, closeReason);
                    return false;
                }
                const size_t maskLen = masked ? 4 : 0;
                if (inbuf.size() < pos + maskLen + len)
                    return true;
                uint8_t mask[4] = {0, 0, 0, 0};
                if (masked)
                    std::memcpy(mask, inbuf.data() + pos, 4);
                pos += maskLen;
                std::vector<uint8_t> payload(inbuf.begin() + static_cast<long>(pos), inbuf.begin() + static_cast<long>(pos + len));
                if (masked)
                    for (size_t i = 0; i < payload.size(); i++)
                        payload[i] ^= mask[i & 3];
                inbuf.erase(inbuf.begin(), inbuf.begin() + static_cast<long>(pos + len));

                if (opcode == 8) {
                    closeCode = payload.size() >= 2 ? static_cast<uint32_t>(payload[0]) << 8 | payload[1] : 1005;
                    if (payload.size() > 2)
                        closeReason.assign(payload.begin() + 2, payload.end());
                    sendClose(closeCode == 1005 ? 1000 : closeCode, "");
                    peerClosed = true;
                    return false;
                }
                if (opcode == 9) {
                    const std::vector<uint8_t> f = MakeFrame(10, payload.data(), payload.size());
                    if (!conn.WriteAll(f.data(), f.size(), socket->stop, error))
                        return false;
                    continue;
                }
                if (opcode == 10)
                    continue;
                if (opcode == 1 || opcode == 2) {
                    if (inMessage) {
                        error = "websocket protocol error";
                        closeCode = 1002;
                        sendClose(1002, error);
                        return false;
                    }
                    inMessage = !fin;
                    messageBinary = opcode == 2;
                    message = std::move(payload);
                } else if (opcode == 0) {
                    if (!inMessage) {
                        error = "websocket protocol error";
                        closeCode = 1002;
                        sendClose(1002, error);
                        return false;
                    }
                    if (message.size() + payload.size() > kMaxMessage) {
                        closeCode = 1009;
                        closeReason = "message too large";
                        sendClose(1009, closeReason);
                        return false;
                    }
                    message.insert(message.end(), payload.begin(), payload.end());
                    inMessage = !fin;
                } else {
                    error = "websocket protocol error";
                    closeCode = 1002;
                    sendClose(1002, error);
                    return false;
                }
                if (!inMessage) {
                    SocketEvent e;
                    e.kind = SocketEvent::Kind::Data;
                    e.id = socket->id;
                    e.binary = messageBinary;
                    e.data = std::move(message);
                    message.clear();
                    Push(std::move(e));
                }
            }
        };

        for (;;) {
            if (socket->stop.load())
                break;
            std::deque<Socket::Outgoing> batch;
            {
                std::lock_guard lock(socket->mutex);
                batch.swap(socket->outgoing);
                socket->queuedBytes = 0;
            }
            bool failed = false;
            for (Socket::Outgoing& out : batch) {
                if (o.webSocket) {
                    const std::vector<uint8_t> f = MakeFrame(out.binary ? 2 : 1, out.data.data(), out.data.size());
                    failed = !conn.WriteAll(f.data(), f.size(), socket->stop, error);
                } else {
                    failed = !conn.WriteAll(out.data.data(), out.data.size(), socket->stop, error);
                }
                if (failed)
                    break;
            }
            if (failed)
                break;
            if (socket->closeRequested.load()) {
                uint32_t code;
                std::string reason;
                {
                    std::lock_guard lock(socket->mutex);
                    code = socket->closeCode;
                    reason = socket->closeReason;
                }
                sendClose(code, reason);
                closeCode = code;
                closeReason = reason;
                break;
            }
            uint8_t buf[65536];
            const long n = conn.Read(buf, sizeof(buf), error);
            if (n < 0)
                break;
            if (n == 0)
                continue;
            if (o.webSocket) {
                inbuf.insert(inbuf.end(), buf, buf + n);
                if (!parseFrames())
                    break;
            } else {
                SocketEvent e;
                e.kind = SocketEvent::Kind::Data;
                e.id = socket->id;
                e.data.assign(buf, buf + n);
                Push(std::move(e));
            }
        }
        (void)peerClosed;
        finish();
    }
}
