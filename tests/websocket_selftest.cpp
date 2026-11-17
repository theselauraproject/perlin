#include "util/ModSocket.hpp"
#include "util/Strings.hpp"
#include "util/WebSocket.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace perlin;
using namespace perlin::util;

static int failures = 0;
#define CHECK(cond, ...)                                          \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL %s:%d: %s ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                             \
            std::printf("\n");                                    \
            failures++;                                           \
        }                                                         \
    } while (0)

namespace reference {
    uint32_t gKey = 0;
    uint32_t RandomWord() { return gKey; }
    constexpr size_t kMaxMessage = 4096;

    namespace {
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
            if (authority.empty() || authority.contains('@'))
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
                if (portText.size() > 5 ||
                    !std::ranges::all_of(portText, [](char c) { return c >= '0' && c <= '9'; }))
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

        std::string Trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t'))
                a++;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
                b--;
            return s.substr(a, b - a);
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
    }

    std::string Hex(const std::vector<uint8_t>& v) {
        std::string s;
        char buf[4];
        for (uint8_t b : v) {
            std::snprintf(buf, sizeof(buf), "%02x", b);
            s += buf;
        }
        return s;
    }

    struct Run {
        std::string log;
        uint32_t closeCode = 0;
        std::string closeReason, error;
        bool peerClosed = false;
    };

    std::string BuildRequest(const Url& url, const std::string& key, const std::string& protocols,
                             const std::vector<std::pair<std::string, std::string>>& headers) {
        struct {
            std::string protocols;
            std::vector<std::pair<std::string, std::string>> headers;
        } o{protocols, headers};
            std::string req = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host;
            const bool defaultPort = (url.scheme == "ws" && url.port == 80) || (url.scheme == "wss" && url.port == 443);
            if (!defaultPort)
                req += ":" + std::to_string(url.port);
            req += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key + "\r\n";
            if (!o.protocols.empty())
                req += "Sec-WebSocket-Protocol: " + o.protocols + "\r\n";
            for (const auto& [k, v] : o.headers) {
                const std::string name = str::Lower(k);
                if (name == "host" || name == "upgrade" || name == "connection" || name.starts_with("sec-websocket-"))
                    continue;
                req += k + ": " + v + "\r\n";
            }
            req += "\r\n";
        return req;
    }

    struct Handshake {
        bool rejected = false, accepted = false;
        std::string status, subprotocol;
    };

    Handshake ParseResponse(std::string head, const std::string& key) {
        Handshake r;
        std::string subprotocol;
        const size_t lineEnd = head.find("\r\n");
        const std::string status = head.substr(0, lineEnd);
        r.status = status;
        if (status.size() < 12 || status.compare(9, 3, "101") != 0) {
            r.rejected = true;
            return r;
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
            const std::string name = str::Lower(Trim(line.substr(0, colon)));
            const std::string value = Trim(line.substr(colon + 1));
            if (name == "sec-websocket-accept")
                accepted = value == expected;
            else if (name == "sec-websocket-protocol")
                subprotocol = value;
        }
        r.accepted = accepted;
        r.subprotocol = subprotocol;
        return r;
    }

    Run Frames(const std::vector<std::vector<uint8_t>>& chunks) {
        Run run;
        std::vector<uint8_t> inbuf;
        std::vector<uint8_t> message;
        bool messageBinary = false, inMessage = false;
        bool peerClosed = false;
        uint32_t closeCode = 0;
        std::string closeReason, error;
        struct FakeSocket {
            std::atomic<bool> stop{false};
            uint32_t id = 7;
        } fake;
        FakeSocket* socket = &fake;
        struct FakeConn {
            std::string* log;
            bool WriteAll(const uint8_t* p, size_t n, std::atomic<bool>&, std::string&) {
                *log += "W" + Hex(std::vector<uint8_t>(p, p + n)) + ";";
                return true;
            }
        } conn{&run.log};
        const auto Push = [&](SocketEvent&& e) {
            run.log += "E" + std::to_string(static_cast<int>(e.kind)) + (e.binary ? "b" : "t") + Hex(e.data) + ";";
        };
        const auto sendClose = [&](uint32_t code, const std::string& reason) {
            run.log += "C" + std::to_string(code) + ":" + reason + ";";
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
        for (const auto& chunk : chunks) {
            inbuf.insert(inbuf.end(), chunk.begin(), chunk.end());
            if (!parseFrames())
                break;
        }
        run.closeCode = closeCode;
        run.closeReason = closeReason;
        run.error = error;
        run.peerClosed = peerClosed;
        return run;
    }
}


namespace {
    std::mt19937_64 rng{12345};
    uint32_t Rand(uint32_t n) { return static_cast<uint32_t>(rng() % n); }

    reference::Run Replay(const std::vector<std::vector<uint8_t>>& chunks) {
        using Kind = ws::FrameDecoder::Event::Kind;
        reference::Run run;
        ws::FrameDecoder decoder(reference::kMaxMessage);
        for (const auto& chunk : chunks) {
            decoder.Feed(chunk.data(), chunk.size());
            for (;;) {
                ws::FrameDecoder::Event event = decoder.Next();
                if (event.kind == Kind::NeedMore)
                    break;
                switch (event.kind) {
                case Kind::Message:
                    run.log += std::string("E1") + (event.binary ? "b" : "t") + reference::Hex(event.data) + ";";
                    continue;
                case Kind::Ping:
                    run.log += "W" + reference::Hex(ws::MakeFrame(10, event.data.data(), event.data.size(), reference::gKey)) + ";";
                    continue;
                case Kind::PeerClose:
                    run.closeCode = event.code;
                    if (!event.reason.empty())
                        run.closeReason = event.reason;
                    run.log += "C" + std::to_string(run.closeCode == 1005 ? 1000 : run.closeCode) + ":;";
                    run.peerClosed = true;
                    return run;
                case Kind::TooLarge:
                    run.closeCode = event.code;
                    run.closeReason = event.reason;
                    run.log += "C" + std::to_string(run.closeCode) + ":" + run.closeReason + ";";
                    return run;
                case Kind::ProtocolError:
                    run.error = event.reason;
                    run.closeCode = event.code;
                    run.log += "C" + std::to_string(run.closeCode) + ":" + run.error + ";";
                    return run;
                case Kind::NeedMore:
                    break;
                }
            }
        }
        return run;
    }

    std::vector<uint8_t> RandomFrame() {
        static const uint8_t ops[] = {0, 1, 1, 2, 2, 0, 8, 9, 9, 10, 3, 5, 11, 15};
        const uint8_t op = ops[Rand(sizeof(ops))];
        static const size_t lens[] = {0, 1, 2, 5, 60, 125, 126, 127, 200, 1000, 3000, 4096, 4097, 9000};
        size_t len = lens[Rand(sizeof(lens) / sizeof(lens[0]))];
        const bool masked = Rand(3) != 0;
        std::vector<uint8_t> f;
        f.push_back(static_cast<uint8_t>((Rand(4) ? 0x80 : 0) | op));
        const bool wide = Rand(6) == 0;
        if (len < 126 && !wide) {
            f.push_back(static_cast<uint8_t>((masked ? 0x80 : 0) | len));
        } else if (len <= 0xFFFF && !wide) {
            f.push_back(static_cast<uint8_t>((masked ? 0x80 : 0) | 126));
            f.push_back(static_cast<uint8_t>(len >> 8));
            f.push_back(static_cast<uint8_t>(len));
        } else {
            f.push_back(static_cast<uint8_t>((masked ? 0x80 : 0) | 127));
            const uint64_t big = Rand(8) == 0 ? (1ull << 40) : len;
            for (int i = 7; i >= 0; i--)
                f.push_back(static_cast<uint8_t>(big >> (i * 8)));
            len = big > 100000 ? 0 : len;
        }
        if (masked)
            for (int i = 0; i < 4; i++)
                f.push_back(static_cast<uint8_t>(Rand(256)));
        for (size_t i = 0; i < len; i++)
            f.push_back(static_cast<uint8_t>(Rand(256)));
        return f;
    }

    void DifferentialFrames() {
        int terminal = 0, messages = 0, pings = 0, errors = 0;
        for (int iter = 0; iter < 30000; iter++) {
            reference::gKey = static_cast<uint32_t>(rng());
            std::vector<uint8_t> stream;
            const int count = 1 + static_cast<int>(Rand(6));
            for (int i = 0; i < count; i++) {
                const auto f = RandomFrame();
                stream.insert(stream.end(), f.begin(), f.end());
            }
            if (Rand(5) == 0 && !stream.empty())
                stream.resize(Rand(static_cast<uint32_t>(stream.size())) + 1);
            std::vector<std::vector<uint8_t>> chunks;
            for (size_t at = 0; at < stream.size();) {
                const size_t n = std::min<size_t>(stream.size() - at, 1 + Rand(Rand(2) ? 3 : 300));
                chunks.emplace_back(stream.begin() + static_cast<long>(at), stream.begin() + static_cast<long>(at + n));
                at += n;
            }
            const reference::Run a = reference::Frames(chunks);
            const reference::Run b = Replay(chunks);
            const bool same = a.log == b.log && a.closeCode == b.closeCode && a.closeReason == b.closeReason &&
                              a.error == b.error && a.peerClosed == b.peerClosed;
            CHECK(same, "iteration %d\n ref: %s | %u '%s' '%s'\n new: %s | %u '%s' '%s'", iter, a.log.c_str(), a.closeCode,
                  a.closeReason.c_str(), a.error.c_str(), b.log.c_str(), b.closeCode, b.closeReason.c_str(), b.error.c_str());
            if (!same)
                return;
            terminal += a.closeCode != 0;
            messages += static_cast<int>(std::count(a.log.begin(), a.log.end(), 'E'));
            pings += static_cast<int>(std::count(a.log.begin(), a.log.end(), 'W'));
            errors += !a.error.empty();
        }
        std::printf("frames: terminal=%d messages=%d pongs=%d protocol-errors=%d\n", terminal, messages, pings, errors);
        CHECK(terminal > 1000 && messages > 1000 && pings > 500 && errors > 100, "generator coverage too thin");
    }

    void DifferentialHelpers() {
        std::vector<std::string> urls = {"ws://host/path", "wss://host:444/a/b?x=1#frag", "ws://[::1]:9000", "ws://[::1", "ws://", "http://a",
                                         "ws://user@host/", "ws://host:99999/", "ws://host:abc/", "WSS://Host", "ws://h?q", "ws://h#f", "nope",
                                         "ws://host:0", "tls://example.com:8443", "tcp://127.0.0.1:1", "ws://[::1]x", "ws://:80"};
        for (int i = 0; i < 400; i++) {
            std::string u = Rand(2) ? "ws://" : "wss://";
            for (uint32_t n = Rand(30); n > 0; n--)
                u.push_back("ab:/[]?#@.0123456789"[Rand(20)]);
            urls.push_back(u);
        }
        for (const auto& u : urls) {
            reference::Url a;
            ws::Url b;
            const bool ra = reference::ParseUrl(u, a), rb = ws::ParseUrl(u, b);
            CHECK(ra == rb && a.scheme == b.scheme && a.host == b.host && a.path == b.path && a.port == b.port, "url %s", u.c_str());
        }
        for (int i = 0; i < 300; i++) {
            std::string text;
            for (uint32_t n = Rand(200); n > 0; n--)
                text.push_back(static_cast<char>(Rand(256)));
            CHECK(reference::Sha1(text) == ws::Sha1(text), "sha1 len %zu", text.size());
            std::vector<uint8_t> bytes(text.begin(), text.end());
            CHECK(reference::Base64(bytes.data(), bytes.size()) == ws::Base64(bytes.data(), bytes.size()), "base64 len %zu", bytes.size());
            const uint32_t key = static_cast<uint32_t>(rng());
            reference::gKey = key;
            const uint8_t op = static_cast<uint8_t>(Rand(16));
            CHECK(reference::MakeFrame(op, bytes.data(), bytes.size()) == ws::MakeFrame(op, bytes.data(), bytes.size(), key),
                  "frame len %zu", bytes.size());
        }
        for (int i = 0; i < 400; i++) {
            reference::Url a;
            ws::Url b;
            const std::string u = i % 3 == 0 ? "ws://host/p" : i % 3 == 1 ? "wss://h:5/x" : "ws://h:81";
            reference::ParseUrl(u, a);
            ws::ParseUrl(u, b);
            std::vector<std::pair<std::string, std::string>> headers;
            static const char* const names[] = {"Host", "UPGRADE", "connection", "Sec-WebSocket-Foo", "X-A", "Authorization", "sec-websocket-protocol"};
            for (uint32_t n = Rand(5); n > 0; n--)
                headers.emplace_back(names[Rand(7)], "v" + std::to_string(Rand(100)));
            const std::string protocols = Rand(2) ? "" : "chat, v2";
            const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
            CHECK(reference::BuildRequest(a, key, protocols, headers) == ws::BuildHandshakeRequest(b, key, protocols, headers), "request %d", i);
        }
        const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
        const auto sha = ws::Sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        CHECK(ws::Base64(sha.data(), sha.size()) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "RFC 6455 accept key vector");
        const auto abc = ws::Sha1("abc");
        CHECK(reference::Hex(std::vector<uint8_t>(abc.begin(), abc.end())) == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 abc");
        const std::string good = ws::Base64(sha.data(), sha.size());
        const std::vector<std::string> heads = {
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: " + good + "\r\nSec-WebSocket-Protocol: chat",
            "HTTP/1.1 101 Switching Protocols\r\nsec-websocket-accept:   " + good + "  \r\nSEC-WEBSOCKET-PROTOCOL: a",
            "HTTP/1.1 101 x\r\nSec-WebSocket-Accept: wrong",
            "HTTP/1.1 101 x",
            "HTTP/1.1 200 OK\r\nSec-WebSocket-Accept: " + good,
            "short",
            "",
            "HTTP/1.1 101 Switching Protocols\r\nnocolon\r\nSec-WebSocket-Accept: " + good + "\r\nSec-WebSocket-Accept: bad",
            "HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept: bad\r\nSec-WebSocket-Accept: " + good,
        };
        for (const auto& h : heads) {
            const auto a = reference::ParseResponse(h, key);
            const auto b = ws::ParseHandshakeResponse(h, key);
            const bool statusMatch = (a.rejected && b.status == ws::HandshakeStatus::Rejected) ||
                                     (!a.rejected && a.accepted && b.status == ws::HandshakeStatus::Accepted) ||
                                     (!a.rejected && !a.accepted && b.status == ws::HandshakeStatus::BadAccept);
            CHECK(statusMatch && a.status == b.statusLine && (a.rejected || a.subprotocol == b.subprotocol), "head '%s'", h.c_str());
        }
    }

#ifndef _WIN32
    struct Listener {
        int fd = -1;
        int port = 0;
        Listener() {
            fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = 0;
            ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            ::listen(fd, 1);
            socklen_t len = sizeof(addr);
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
            port = ntohs(addr.sin_port);
        }
        ~Listener() { ::close(fd); }
    };

    bool ReadExact(int fd, uint8_t* p, size_t n) {
        while (n) {
            const ssize_t r = ::read(fd, p, n);
            if (r <= 0)
                return false;
            p += r;
            n -= static_cast<size_t>(r);
        }
        return true;
    }

    void WriteAll(int fd, const std::vector<uint8_t>& v) { (void)!::write(fd, v.data(), v.size()); }

    std::vector<uint8_t> ServerFrame(uint8_t opcode, bool fin, const std::string& payload) {
        std::vector<uint8_t> f{static_cast<uint8_t>((fin ? 0x80 : 0) | opcode), static_cast<uint8_t>(payload.size())};
        f.insert(f.end(), payload.begin(), payload.end());
        return f;
    }

    struct ClientFrame {
        uint8_t opcode = 0;
        std::string payload;
        bool masked = false;
    };

    bool ReadClientFrame(int fd, ClientFrame& out) {
        uint8_t h[2];
        if (!ReadExact(fd, h, 2))
            return false;
        out.opcode = h[0] & 0x0F;
        out.masked = (h[1] & 0x80) != 0;
        const size_t len = h[1] & 0x7F;
        uint8_t mask[4] = {};
        if (out.masked && !ReadExact(fd, mask, 4))
            return false;
        std::string p(len, '\0');
        if (len && !ReadExact(fd, reinterpret_cast<uint8_t*>(p.data()), len))
            return false;
        for (size_t i = 0; i < len; i++)
            p[i] = static_cast<char>(p[i] ^ mask[i & 3]);
        out.payload = p;
        return true;
    }

    std::vector<SocketEvent> Collect(SocketSet& set, size_t want, int ms = 5000) {
        std::vector<SocketEvent> all;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (all.size() < want && std::chrono::steady_clock::now() < end) {
            set.Poll(all);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return all;
    }

    void LoopbackWebSocket() {
        Listener listener;
        std::string serverFailure;
        bool sawPong = false, sawData = false, sawCloseReply = false, sawMaskedAll = true;
        std::thread server([&] {
            const int fd = ::accept(listener.fd, nullptr, nullptr);
            std::string req;
            uint8_t c;
            while (req.find("\r\n\r\n") == std::string::npos && ReadExact(fd, &c, 1))
                req.push_back(static_cast<char>(c));
            const size_t k = req.find("Sec-WebSocket-Key: ");
            const std::string key = req.substr(k + 19, req.find("\r\n", k) - k - 19);
            const auto sha = ws::Sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
            const std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                                     ws::Base64(sha.data(), sha.size()) + "\r\nSec-WebSocket-Protocol: chat\r\n\r\n";
            std::vector<uint8_t> first(resp.begin(), resp.end());
            const auto hello = ServerFrame(1, true, "hello");
            first.insert(first.end(), hello.begin(), hello.end());
            WriteAll(fd, first);
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            WriteAll(fd, ServerFrame(2, false, "ab"));
            WriteAll(fd, ServerFrame(9, true, "pp"));
            WriteAll(fd, ServerFrame(0, true, "cd"));
            ClientFrame f;
            if (ReadClientFrame(fd, f)) {
                sawPong = f.opcode == 10 && f.payload == "pp";
                sawMaskedAll = sawMaskedAll && f.masked;
            }
            if (ReadClientFrame(fd, f)) {
                sawData = f.opcode == 2 && f.payload == std::string("\x01\x02\x03", 3);
                sawMaskedAll = sawMaskedAll && f.masked;
            }
            std::string closePayload("\x03\xe9", 2);
            closePayload += "bye";
            WriteAll(fd, ServerFrame(8, true, closePayload));
            if (ReadClientFrame(fd, f))
                sawCloseReply = f.opcode == 8 && f.payload.size() >= 2 && static_cast<uint8_t>(f.payload[0]) == 0x03 &&
                                static_cast<uint8_t>(f.payload[1]) == 0xe9 && f.masked;
            ::close(fd);
        });
        SocketSet set;
        SocketOpenOptions opt;
        opt.id = 11;
        opt.webSocket = true;
        opt.url = "ws://127.0.0.1:" + std::to_string(listener.port) + "/x";
        opt.protocols = "chat";
        CHECK(set.Open(opt) == SocketResult::Ok, "open");
        std::vector<SocketEvent> events = Collect(set, 2, 300);
        CHECK(events.size() == 2, "a frame that arrives with the handshake must be delivered without waiting for more data (%zu)", events.size());
        for (SocketEvent& e : Collect(set, 1))
            events.push_back(std::move(e));
        CHECK(events.size() == 3, "got %zu events before the send", events.size());
        if (events.size() >= 3) {
            CHECK(events[0].kind == SocketEvent::Kind::Open && events[0].text == "chat", "open event");
            CHECK(events[1].kind == SocketEvent::Kind::Data && !events[1].binary &&
                      std::string(events[1].data.begin(), events[1].data.end()) == "hello", "text message");
            CHECK(events[2].kind == SocketEvent::Kind::Data && events[2].binary &&
                      std::string(events[2].data.begin(), events[2].data.end()) == "abcd", "fragmented message across a ping");
        }
        CHECK(set.Send(11, true, {1, 2, 3}) == SocketResult::Ok, "send");
        std::vector<SocketEvent> rest = Collect(set, 1);
        server.join();
        CHECK(!rest.empty() && rest.back().kind == SocketEvent::Kind::Close && rest.back().code == 1001 && rest.back().text == "bye" &&
                  rest.back().error.empty(),
              "close event (%zu events)", rest.size());
        CHECK(sawPong, "server saw masked pong carrying the ping payload");
        CHECK(sawData, "server saw the sent binary message");
        CHECK(sawCloseReply, "server saw the close reply echoing the code");
        CHECK(sawMaskedAll, "every client frame masked");
    }

    void LoopbackRawAndErrors() {
        {
            Listener listener;
            std::thread server([&] {
                const int fd = ::accept(listener.fd, nullptr, nullptr);
                uint8_t buf[5];
                if (ReadExact(fd, buf, 5))
                    WriteAll(fd, {'w', 'o', 'r', 'l', 'd'});
                ::close(fd);
            });
            SocketSet set;
            SocketOpenOptions opt;
            opt.id = 3;
            opt.url = "tcp://127.0.0.1:" + std::to_string(listener.port);
            CHECK(set.Open(opt) == SocketResult::Ok, "raw open");
            CHECK(set.Send(3, true, {'h', 'e', 'l', 'l', 'o'}) == SocketResult::Ok, "raw send");
            const auto events = Collect(set, 3);
            server.join();
            CHECK(events.size() == 3 && events[0].kind == SocketEvent::Kind::Open && events[1].kind == SocketEvent::Kind::Data &&
                      std::string(events[1].data.begin(), events[1].data.end()) == "world" && events[2].kind == SocketEvent::Kind::Close,
                  "raw tcp events (%zu)", events.size());
        }
        {
            Listener listener;
            std::thread server([&] {
                const int fd = ::accept(listener.fd, nullptr, nullptr);
                std::string req;
                uint8_t c;
                while (req.find("\r\n\r\n") == std::string::npos && ReadExact(fd, &c, 1))
                    req.push_back(static_cast<char>(c));
                const std::string resp = "HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept: wrong\r\n\r\n";
                WriteAll(fd, std::vector<uint8_t>(resp.begin(), resp.end()));
                ::close(fd);
            });
            SocketSet set;
            SocketOpenOptions opt;
            opt.id = 4;
            opt.webSocket = true;
            opt.url = "ws://127.0.0.1:" + std::to_string(listener.port);
            set.Open(opt);
            const auto events = Collect(set, 1);
            server.join();
            CHECK(events.size() == 1 && events[0].kind == SocketEvent::Kind::Close &&
                      events[0].error == "websocket handshake failed: bad accept key",
                  "bad accept key must end with a close event carrying the reason");
        }
        {
            Listener listener;
            std::thread server([&] {
                const int fd = ::accept(listener.fd, nullptr, nullptr);
                std::string req;
                uint8_t c;
                while (req.find("\r\n\r\n") == std::string::npos && ReadExact(fd, &c, 1))
                    req.push_back(static_cast<char>(c));
                const size_t k = req.find("Sec-WebSocket-Key: ");
                const std::string key = req.substr(k + 19, req.find("\r\n", k) - k - 19);
                const auto sha = ws::Sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
                const std::string resp = "HTTP/1.1 101 x\r\nSec-WebSocket-Accept: " + ws::Base64(sha.data(), sha.size()) + "\r\n\r\n";
                WriteAll(fd, std::vector<uint8_t>(resp.begin(), resp.end()));
                WriteAll(fd, {0x82, 0x7F, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF});
                ClientFrame f;
                ReadClientFrame(fd, f);
                ::close(fd);
            });
            SocketSet set;
            SocketOpenOptions opt;
            opt.id = 5;
            opt.webSocket = true;
            opt.url = "ws://127.0.0.1:" + std::to_string(listener.port);
            set.Open(opt);
            const auto events = Collect(set, 2);
            server.join();
            CHECK(events.size() == 2 && events[1].kind == SocketEvent::Kind::Close && events[1].code == 1009 &&
                      events[1].text == "message too large",
                  "oversized frame closes with 1009");
        }
    }
#endif
}

int main() {
    DifferentialHelpers();
    DifferentialFrames();
#ifndef _WIN32
    LoopbackWebSocket();
    LoopbackRawAndErrors();
#endif
    if (failures)
        std::printf("%d failures\n", failures);
    else
        std::printf("websocket selftest passed\n");
    return failures ? 1 : 0;
}
