#include "util/WebSocket.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace perlin::util::ws {
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

    std::string TrimHeaderValue(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t'))
            a++;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
            b--;
        return s.substr(a, b - a);
    }

    std::vector<uint8_t> MakeFrame(uint8_t opcode, const uint8_t* p, size_t n, uint32_t maskKey) {
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
        const uint8_t mask[4] = {static_cast<uint8_t>(maskKey), static_cast<uint8_t>(maskKey >> 8),
                                 static_cast<uint8_t>(maskKey >> 16), static_cast<uint8_t>(maskKey >> 24)};
        f.insert(f.end(), mask, mask + 4);
        for (size_t i = 0; i < n; i++)
            f.push_back(p[i] ^ mask[i & 3]);
        return f;
    }

    std::string BuildHandshakeRequest(const Url& url, const std::string& key, const std::string& protocols,
                                      const std::vector<std::pair<std::string, std::string>>& headers) {
        std::string req = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host;
        const bool defaultPort = (url.scheme == "ws" && url.port == 80) || (url.scheme == "wss" && url.port == 443);
        if (!defaultPort)
            req += ":" + std::to_string(url.port);
        req += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key + "\r\n";
        if (!protocols.empty())
            req += "Sec-WebSocket-Protocol: " + protocols + "\r\n";
        for (const auto& [k, v] : headers) {
            const std::string name = str::Lower(k);
            if (name == "host" || name == "upgrade" || name == "connection" || name.starts_with("sec-websocket-"))
                continue;
            req += k + ": " + v + "\r\n";
        }
        req += "\r\n";
        return req;
    }

    HandshakeResult ParseHandshakeResponse(const std::string& head, const std::string& key) {
        HandshakeResult result;
        const size_t lineEnd = head.find("\r\n");
        result.statusLine = head.substr(0, lineEnd);
        if (result.statusLine.size() < 12 || result.statusLine.compare(9, 3, "101") != 0) {
            result.status = HandshakeStatus::Rejected;
            return result;
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
            const std::string name = str::Lower(TrimHeaderValue(line.substr(0, colon)));
            const std::string value = TrimHeaderValue(line.substr(colon + 1));
            if (name == "sec-websocket-accept")
                accepted = value == expected;
            else if (name == "sec-websocket-protocol")
                result.subprotocol = value;
        }
        result.status = accepted ? HandshakeStatus::Accepted : HandshakeStatus::BadAccept;
        return result;
    }

    void FrameDecoder::Feed(const uint8_t* bytes, size_t count) { buffer.insert(buffer.end(), bytes, bytes + count); }

    FrameDecoder::Event FrameDecoder::Next() {
        const auto make = [](Event::Kind kind) {
            Event e;
            e.kind = kind;
            return e;
        };
        const auto tooLarge = [&] {
            Event e = make(Event::Kind::TooLarge);
            e.code = 1009;
            e.reason = "message too large";
            return e;
        };
        const auto protocolError = [&] {
            Event e = make(Event::Kind::ProtocolError);
            e.code = 1002;
            e.reason = "websocket protocol error";
            return e;
        };
        for (;;) {
            if (buffer.size() < 2)
                return make(Event::Kind::NeedMore);
            const bool fin = (buffer[0] & 0x80) != 0;
            const uint8_t opcode = buffer[0] & 0x0F;
            const bool masked = (buffer[1] & 0x80) != 0;
            uint64_t len = buffer[1] & 0x7F;
            size_t pos = 2;
            if (len == 126) {
                if (buffer.size() < 4)
                    return make(Event::Kind::NeedMore);
                len = static_cast<uint64_t>(buffer[2]) << 8 | buffer[3];
                pos = 4;
            } else if (len == 127) {
                if (buffer.size() < 10)
                    return make(Event::Kind::NeedMore);
                len = 0;
                for (int i = 0; i < 8; i++)
                    len = len << 8 | buffer[static_cast<size_t>(2 + i)];
                pos = 10;
            }
            if (len > maxMessage || (opcode >= 8 && len > 125))
                return tooLarge();
            const size_t maskLen = masked ? 4 : 0;
            if (buffer.size() < pos + maskLen + len)
                return make(Event::Kind::NeedMore);
            uint8_t mask[4] = {0, 0, 0, 0};
            if (masked)
                std::memcpy(mask, buffer.data() + pos, 4);
            pos += maskLen;
            std::vector<uint8_t> payload(buffer.begin() + static_cast<long>(pos),
                                         buffer.begin() + static_cast<long>(pos + len));
            if (masked)
                for (size_t i = 0; i < payload.size(); i++)
                    payload[i] ^= mask[i & 3];
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<long>(pos + len));

            if (opcode == 8) {
                Event e = make(Event::Kind::PeerClose);
                e.code = payload.size() >= 2 ? static_cast<uint32_t>(payload[0]) << 8 | payload[1] : 1005;
                if (payload.size() > 2)
                    e.reason.assign(payload.begin() + 2, payload.end());
                return e;
            }
            if (opcode == 9) {
                Event e = make(Event::Kind::Ping);
                e.data = std::move(payload);
                return e;
            }
            if (opcode == 10)
                continue;
            if (opcode == 1 || opcode == 2) {
                if (inMessage)
                    return protocolError();
                inMessage = !fin;
                messageBinary = opcode == 2;
                message = std::move(payload);
            } else if (opcode == 0) {
                if (!inMessage)
                    return protocolError();
                if (message.size() + payload.size() > maxMessage)
                    return tooLarge();
                message.insert(message.end(), payload.begin(), payload.end());
                inMessage = !fin;
            } else {
                return protocolError();
            }
            if (!inMessage) {
                Event e = make(Event::Kind::Message);
                e.binary = messageBinary;
                e.data = std::move(message);
                message.clear();
                return e;
            }
        }
    }
}
