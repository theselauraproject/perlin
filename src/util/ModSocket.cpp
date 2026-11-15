#include "util/ModSocket.hpp"
#include "util/Strings.hpp"
#include "util/WebSocket.hpp"

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

        bool Clean(const std::string& s) { return s.find_first_of("\r\n") == std::string::npos; }

        uint32_t RandomWord() {
            thread_local std::mt19937 rng{std::random_device{}()};
            return rng();
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
        ws::Url url;
        if (!ws::ParseUrl(options.url, url))
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
            if (k.empty() || k.size() > 128 || v.size() > 4096 || !Clean(k) || !Clean(v) || k.contains(':'))
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

    class SocketSet::Session {
    public:
        Session(SocketSet& owner, std::shared_ptr<Socket> socket)
            : owner(owner), socket(std::move(socket)), options(this->socket->options), decoder(kMaxMessage) {}

        void Run() {
            if (!Connect() || (options.webSocket && !Handshake())) {
                Finish();
                return;
            }
            SocketEvent opened;
            opened.kind = SocketEvent::Kind::Open;
            opened.id = socket->id;
            opened.text = subprotocol;
            owner.Push(std::move(opened));
            if (!options.webSocket || DrainFrames())
                Pump();
            Finish();
        }

    private:
        bool Connect() {
            ws::ParseUrl(options.url, url);
            const bool tls = url.scheme == "wss" || url.scheme == "tls";
            return conn.Connect(url.host, url.port, options.timeoutMs, socket->stop, error) &&
                   (!tls || conn.Tls(url.host, options.timeoutMs, socket->stop, error));
        }

        bool Handshake() {
            uint8_t raw[16];
            for (uint8_t& b : raw)
                b = static_cast<uint8_t>(RandomWord());
            const std::string key = ws::Base64(raw, sizeof(raw));
            const std::string request = ws::BuildHandshakeRequest(url, key, options.protocols, options.headers);
            if (!conn.WriteAll(reinterpret_cast<const uint8_t*>(request.data()), request.size(), socket->stop, error))
                return false;
            std::string head;
            if (!ReadHandshakeHead(head))
                return false;
            const ws::HandshakeResult result = ws::ParseHandshakeResponse(head, key);
            switch (result.status) {
            case ws::HandshakeStatus::Rejected:
                error = "websocket handshake rejected: " + result.statusLine;
                return false;
            case ws::HandshakeStatus::BadAccept:
                error = "websocket handshake failed: bad accept key";
                return false;
            case ws::HandshakeStatus::Accepted:
                break;
            }
            subprotocol = result.subprotocol;
            return true;
        }

        bool ReadHandshakeHead(std::string& head) {
            const auto deadline = Clock::now() + std::chrono::milliseconds(options.timeoutMs);
            size_t headEnd = std::string::npos;
            while (headEnd == std::string::npos) {
                if (socket->stop.load() || Clock::now() > deadline || head.size() > 16384) {
                    error = socket->stop.load() ? "closed" : "websocket handshake timed out";
                    return false;
                }
                uint8_t buf[2048];
                const long n = conn.Read(buf, sizeof(buf), error);
                if (n < 0) {
                    if (error.empty())
                        error = "connection closed during the handshake";
                    return false;
                }
                head.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
                headEnd = head.find("\r\n\r\n");
            }
            decoder.Feed(reinterpret_cast<const uint8_t*>(head.data()) + headEnd + 4, head.size() - headEnd - 4);
            head.resize(headEnd);
            return true;
        }

        void SendClose(uint32_t code, const std::string& reason) {
            if (!options.webSocket)
                return;
            std::vector<uint8_t> payload;
            payload.push_back(static_cast<uint8_t>(code >> 8));
            payload.push_back(static_cast<uint8_t>(code));
            payload.insert(payload.end(), reason.begin(), reason.end());
            const std::vector<uint8_t> frame = ws::MakeFrame(8, payload.data(), payload.size(), RandomWord());
            std::string ignored;
            std::atomic<bool> never{false};
            conn.WriteAll(frame.data(), frame.size(), never, ignored);
        }

        void Pump() {
            while (!socket->stop.load()) {
                if (!FlushOutgoing())
                    return;
                if (socket->closeRequested.load()) {
                    CloseFromLocalRequest();
                    return;
                }
                uint8_t buf[65536];
                const long n = conn.Read(buf, sizeof(buf), error);
                if (n < 0)
                    return;
                if (n == 0)
                    continue;
                if (!options.webSocket) {
                    SocketEvent e;
                    e.kind = SocketEvent::Kind::Data;
                    e.id = socket->id;
                    e.data.assign(buf, buf + n);
                    owner.Push(std::move(e));
                    continue;
                }
                decoder.Feed(buf, static_cast<size_t>(n));
                if (!DrainFrames())
                    return;
            }
        }

        bool FlushOutgoing() {
            std::deque<Socket::Outgoing> batch;
            {
                std::lock_guard lock(socket->mutex);
                batch.swap(socket->outgoing);
                socket->queuedBytes = 0;
            }
            for (Socket::Outgoing& out : batch) {
                bool written;
                if (options.webSocket) {
                    const std::vector<uint8_t> f =
                        ws::MakeFrame(out.binary ? 2 : 1, out.data.data(), out.data.size(), RandomWord());
                    written = conn.WriteAll(f.data(), f.size(), socket->stop, error);
                } else {
                    written = conn.WriteAll(out.data.data(), out.data.size(), socket->stop, error);
                }
                if (!written)
                    return false;
            }
            return true;
        }

        void CloseFromLocalRequest() {
            uint32_t code;
            std::string reason;
            {
                std::lock_guard lock(socket->mutex);
                code = socket->closeCode;
                reason = socket->closeReason;
            }
            SendClose(code, reason);
            closeCode = code;
            closeReason = reason;
        }

        bool DrainFrames() {
            using Kind = ws::FrameDecoder::Event::Kind;
            for (;;) {
                ws::FrameDecoder::Event event = decoder.Next();
                switch (event.kind) {
                case Kind::NeedMore:
                    return true;
                case Kind::Message: {
                    SocketEvent e;
                    e.kind = SocketEvent::Kind::Data;
                    e.id = socket->id;
                    e.binary = event.binary;
                    e.data = std::move(event.data);
                    owner.Push(std::move(e));
                    break;
                }
                case Kind::Ping: {
                    const std::vector<uint8_t> pong =
                        ws::MakeFrame(10, event.data.data(), event.data.size(), RandomWord());
                    if (!conn.WriteAll(pong.data(), pong.size(), socket->stop, error))
                        return false;
                    break;
                }
                case Kind::PeerClose:
                    closeCode = event.code;
                    if (!event.reason.empty())
                        closeReason = std::move(event.reason);
                    SendClose(closeCode == 1005 ? 1000 : closeCode, "");
                    return false;
                case Kind::TooLarge:
                    closeCode = event.code;
                    closeReason = std::move(event.reason);
                    SendClose(closeCode, closeReason);
                    return false;
                case Kind::ProtocolError:
                    error = std::move(event.reason);
                    closeCode = event.code;
                    SendClose(closeCode, error);
                    return false;
                }
            }
        }

        void Finish() {
            SocketEvent e;
            e.kind = SocketEvent::Kind::Close;
            e.id = socket->id;
            e.code = closeCode;
            e.text = closeReason;
            e.error = error;
            owner.Push(std::move(e));
            socket->done.store(true);
        }

        SocketSet& owner;
        std::shared_ptr<Socket> socket;
        const SocketOpenOptions& options;
        ws::Url url;
        Conn conn;
        ws::FrameDecoder decoder;
        std::string error, subprotocol, closeReason;
        uint32_t closeCode = 0;
    };

    void SocketSet::Run(std::shared_ptr<Socket> socket) { Session(*this, std::move(socket)).Run(); }
}
