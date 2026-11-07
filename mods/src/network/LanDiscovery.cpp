#include "network/LanDiscovery.hpp"

#ifdef __EMSCRIPTEN__

namespace perlin::lan {
    struct Session::Impl {};
    Session::Session() = default;
    Session::~Session() = default;
    std::shared_ptr<Session> Session::Open(std::string& error) {
        error = "LAN discovery isn't available in the browser";
        return nullptr;
    }
    void Session::Scan() {}
    std::vector<Host> Session::Hosts() const {
        return {};
    }
    bool Session::WaitFor(uint64_t, int) {
        return false;
    }
    bool Session::Send(const std::string&, const std::string&, bool, std::string& error) {
        error = "not available";
        return false;
    }
    void Session::SetHandler(std::function<void(const std::string&, const std::string&)>) {}
    bool Session::Credentials(std::vector<friendworld::IceServerInfo>&, std::string&) {
        return true;
    }
    bool Session::Alive() const {
        return false;
    }
    void Session::Close() {}
}

#else

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <thread>

namespace perlin::lan {

    namespace {
#ifdef _WIN32
        using Socket = SOCKET;
        constexpr Socket kInvalid = INVALID_SOCKET;
        void CloseSocket(Socket s) {
            closesocket(s);
        }
        void EnsureSockets() {
            static const bool once = [] {
                WSADATA data;
                WSAStartup(MAKEWORD(2, 2), &data);
                return true;
            }();
            (void)once;
        }
#else
        using Socket = int;
        constexpr Socket kInvalid = -1;
        void CloseSocket(Socket s) {
            close(s);
        }
        void EnsureSockets() {}
#endif

        constexpr uint16_t kPort = 7551;
        enum : uint16_t { kRequest = 0, kResponse = 1, kMessage = 2 };

        const uint8_t* Key() {
            static uint8_t key[32];
            static const bool once = [] {
                const uint8_t id[8] = {0xef, 0xbe, 0xad, 0xde, 0, 0, 0, 0};
                SHA256(id, sizeof id, key);
                return true;
            }();
            (void)once;
            return key;
        }

        bool Ecb(bool encrypt, const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
            EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
            if (!ctx)
                return false;
            out.assign(in.size() + 16, 0);
            int n = 0, m = 0;
            const bool ok =
                EVP_CipherInit_ex(ctx, EVP_aes_256_ecb(), nullptr, Key(), nullptr, encrypt ? 1 : 0) == 1 &&
                EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
                EVP_CipherUpdate(ctx, out.data(), &n, in.data(), static_cast<int>(in.size())) == 1 &&
                EVP_CipherFinal_ex(ctx, out.data() + n, &m) == 1;
            EVP_CIPHER_CTX_free(ctx);
            out.resize(ok ? static_cast<size_t>(n + m) : 0);
            return ok;
        }

        std::vector<uint8_t> Hmac(const std::vector<uint8_t>& data) {
            std::vector<uint8_t> mac(32);
            unsigned len = 0;
            HMAC(EVP_sha256(), Key(), 32, data.data(), data.size(), mac.data(), &len);
            return mac;
        }

        void PutU16(std::vector<uint8_t>& b, uint16_t v) {
            for (int i = 0; i < 2; i++)
                b.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        void PutU32(std::vector<uint8_t>& b, uint32_t v) {
            for (int i = 0; i < 4; i++)
                b.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        void PutU64(std::vector<uint8_t>& b, uint64_t v) {
            for (int i = 0; i < 8; i++)
                b.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        uint64_t GetLe(const uint8_t* p, int n) {
            uint64_t v = 0;
            for (int i = 0; i < n; i++)
                v |= static_cast<uint64_t>(p[i]) << (8 * i);
            return v;
        }

        std::vector<uint8_t> Marshal(uint16_t id, uint64_t sender, const std::vector<uint8_t>& body) {
            std::vector<uint8_t> inner;
            PutU16(inner, id);
            PutU64(inner, sender);
            inner.insert(inner.end(), 8, 0);
            inner.insert(inner.end(), body.begin(), body.end());
            std::vector<uint8_t> payload;
            PutU16(payload, static_cast<uint16_t>(inner.size() + 2));
            payload.insert(payload.end(), inner.begin(), inner.end());
            std::vector<uint8_t> padded = payload;
            const uint8_t pad = static_cast<uint8_t>(16 - padded.size() % 16);
            padded.insert(padded.end(), pad, pad);
            std::vector<uint8_t> cipher;
            if (!Ecb(true, padded, cipher))
                return {};
            std::vector<uint8_t> out = Hmac(payload);
            out.insert(out.end(), cipher.begin(), cipher.end());
            return out;
        }

        struct Packet {
            uint16_t id = 0;
            uint64_t sender = 0;
            std::vector<uint8_t> body;
        };

        bool Unmarshal(const uint8_t* data, size_t size, Packet& out) {
            if (size < 32 + 16 || (size - 32) % 16 != 0)
                return false;
            std::vector<uint8_t> cipher(data + 32, data + size), plain;
            if (!Ecb(false, cipher, plain) || plain.empty())
                return false;
            const uint8_t pad = plain.back();
            if (pad == 0 || pad > 16 || pad > plain.size())
                return false;
            plain.resize(plain.size() - pad);
            const std::vector<uint8_t> mac = Hmac(plain);
            if (std::memcmp(mac.data(), data, 32) != 0)
                return false;
            if (plain.size() < 2 + 2 + 8 + 8)
                return false;
            out.id = static_cast<uint16_t>(GetLe(&plain[2], 2));
            out.sender = GetLe(&plain[4], 8);
            out.body.assign(plain.begin() + 20, plain.end());
            return true;
        }

        bool ReadVaruint(const std::vector<uint8_t>& b, size_t& at, uint32_t& v) {
            v = 0;
            for (unsigned shift = 0; shift < 35; shift += 7) {
                if (at >= b.size())
                    return false;
                const uint8_t c = b[at++];
                v |= static_cast<uint32_t>(c & 0x7f) << shift;
                if (!(c & 0x80))
                    return true;
            }
            return false;
        }
        bool ReadVarint(const std::vector<uint8_t>& b, size_t& at, int32_t& v) {
            uint32_t u = 0;
            if (!ReadVaruint(b, at, u))
                return false;
            v = static_cast<int32_t>(u >> 1);
            if (u & 1)
                v = ~v;
            return true;
        }
        bool ReadString(const std::vector<uint8_t>& b, size_t& at, std::string& s) {
            uint32_t n = 0;
            if (!ReadVaruint(b, at, n) || n > b.size() - at)
                return false;
            s.assign(b.begin() + at, b.begin() + at + n);
            at += n;
            return true;
        }

        int HexValue(char c) {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        bool ParseServerData(const std::vector<uint8_t>& body, Host& host) {
            if (body.size() < 4)
                return false;
            const uint32_t n = static_cast<uint32_t>(GetLe(body.data(), 4));
            if (n > body.size() - 4 || n % 2 != 0)
                return false;
            std::vector<uint8_t> data;
            for (uint32_t i = 0; i < n; i += 2) {
                const int hi = HexValue(static_cast<char>(body[4 + i])),
                          lo = HexValue(static_cast<char>(body[5 + i]));
                if (hi < 0 || lo < 0)
                    return false;
                data.push_back(static_cast<uint8_t>(hi * 16 + lo));
            }
            size_t at = 0;
            if (data.empty() || data[at++] != 7)
                return false;
            int32_t protocol = 0, players = 0, maxPlayers = 0, gameType = 0;
            std::string name, version, level;
            if (!ReadString(data, at, name) || !ReadVarint(data, at, protocol) ||
                !ReadString(data, at, version) || !ReadString(data, at, level) ||
                !ReadVarint(data, at, players) || !ReadVarint(data, at, maxPlayers) ||
                !ReadVarint(data, at, gameType) || at + 4 > data.size())
                return false;
            host.name = name;
            host.protocol = protocol;
            host.version = version;
            host.level = level;
            host.players = players;
            host.maxPlayers = maxPlayers;
            host.gameType = gameType;
            host.onlineAuth = data[at] != 0;
            host.selfSignedAuth = data[at + 1] != 0 ? true : false;
            return true;
        }
    }

    struct Session::Impl {
        Socket sock = kInvalid;
        uint64_t networkId = 0;
        std::atomic<bool> stop{false};
        std::thread reader;
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::map<uint64_t, Host> hosts;
        std::map<uint64_t, sockaddr_in> addresses;
        std::function<void(const std::string&, const std::string&)> handler;
        std::vector<sockaddr_in> targets;
        std::mutex sendMutex;

        void SendTo(const std::vector<uint8_t>& packet, const sockaddr_in& to) {
            std::lock_guard lock(sendMutex);
            sendto(sock, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                   reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        }

        void Handle(const uint8_t* data, size_t size, const sockaddr_in& from) {
            Packet pk;
            if (!Unmarshal(data, size, pk) || pk.sender == networkId)
                return;
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
            if (pk.id == kRequest) {
                return;
            }
            if (pk.id == kResponse) {
                Host host;
                if (!ParseServerData(pk.body, host))
                    return;
                host.networkId = pk.sender;
                host.ip = ip;
                host.port = ntohs(from.sin_port);
                std::lock_guard lock(mutex);
                hosts[pk.sender] = host;
                addresses[pk.sender] = from;
                cv.notify_all();
                return;
            }
            if (pk.id == kMessage && pk.body.size() >= 12) {
                const uint64_t recipient = GetLe(pk.body.data(), 8);
                if (recipient != networkId)
                    return;
                const uint32_t n = static_cast<uint32_t>(GetLe(&pk.body[8], 4));
                std::string text;
                if (n <= pk.body.size() - 12)
                    text.assign(pk.body.begin() + 12, pk.body.end());
                if (text.empty() || text == "Ping")
                    return;
                {
                    std::lock_guard lock(mutex);
                    addresses[pk.sender] = from;
                }
                std::function<void(const std::string&, const std::string&)> h;
                {
                    std::lock_guard lock(mutex);
                    h = handler;
                }
                if (h)
                    h(std::to_string(pk.sender), text);
            }
        }
    };

    Session::Session() : impl(std::make_unique<Impl>()) {}
    Session::~Session() {
        Close();
    }

    std::shared_ptr<Session> Session::Open(std::string& error) {
        EnsureSockets();
        std::shared_ptr<Session> s(new Session());
        Impl& im = *s->impl;
        im.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (im.sock == kInvalid) {
            error = "cannot open a UDP socket";
            return nullptr;
        }
        int on = 1;
        setsockopt(im.sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
#ifdef _WIN32
        DWORD timeout = 200;
        setsockopt(im.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
#else
        timeval tv{0, 200000};
        setsockopt(im.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
        std::mt19937_64 rng(std::random_device{}());
        im.networkId = rng() | 1;

        sockaddr_in t{};
        t.sin_family = AF_INET;
        t.sin_port = htons(kPort);
        t.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        im.targets.push_back(t);
        t.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        im.targets.push_back(t);
#ifndef _WIN32
        if (ifaddrs* list = nullptr; getifaddrs(&list) == 0) {
            for (ifaddrs* a = list; a; a = a->ifa_next) {
                if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_BROADCAST) ||
                    !(a->ifa_flags & IFF_UP) || !a->ifa_broadaddr)
                    continue;
                sockaddr_in b = *reinterpret_cast<sockaddr_in*>(a->ifa_broadaddr);
                b.sin_port = htons(kPort);
                im.targets.push_back(b);
            }
            freeifaddrs(list);
        }
#endif
        Impl* raw = &im;
        im.reader = std::thread([raw] {
            uint8_t buf[4096];
            while (!raw->stop) {
                sockaddr_in from{};
#ifdef _WIN32
                int fromLen = sizeof from;
#else
                socklen_t fromLen = sizeof from;
#endif
                const int n = static_cast<int>(recvfrom(raw->sock, reinterpret_cast<char*>(buf), sizeof buf,
                                                        0, reinterpret_cast<sockaddr*>(&from), &fromLen));
                if (n > 0)
                    raw->Handle(buf, static_cast<size_t>(n), from);
            }
        });
        return s;
    }

    void Session::Scan() {
        const std::vector<uint8_t> packet = Marshal(kRequest, impl->networkId, {});
        if (packet.empty())
            return;
        for (const sockaddr_in& t : impl->targets)
            impl->SendTo(packet, t);
    }

    std::vector<Host> Session::Hosts() const {
        std::lock_guard lock(impl->mutex);
        std::vector<Host> out;
        for (const auto& [id, host] : impl->hosts)
            out.push_back(host);
        return out;
    }

    bool Session::WaitFor(uint64_t networkId, int timeoutMs) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        std::unique_lock lock(impl->mutex);
        while (!impl->hosts.contains(networkId)) {
            lock.unlock();
            Scan();
            lock.lock();
            if (impl->cv.wait_for(lock, std::chrono::milliseconds(500),
                                  [&] { return impl->hosts.contains(networkId); }))
                break;
            if (std::chrono::steady_clock::now() > end)
                return false;
        }
        return true;
    }

    bool Session::Send(const std::string& to, const std::string& text, bool, std::string& error) {
        const uint64_t id = std::strtoull(to.c_str(), nullptr, 10);
        sockaddr_in addr{};
        {
            std::lock_guard lock(impl->mutex);
            const auto it = impl->addresses.find(id);
            if (it == impl->addresses.end()) {
                error = "that game is no longer visible on the network";
                return false;
            }
            addr = it->second;
        }
        std::vector<uint8_t> body;
        PutU64(body, id);
        PutU32(body, static_cast<uint32_t>(text.size()));
        body.insert(body.end(), text.begin(), text.end());
        const std::vector<uint8_t> packet = Marshal(kMessage, impl->networkId, body);
        if (packet.empty()) {
            error = "could not encrypt the signal";
            return false;
        }
        impl->SendTo(packet, addr);
        return true;
    }

    void Session::SetHandler(std::function<void(const std::string&, const std::string&)> handler) {
        std::lock_guard lock(impl->mutex);
        impl->handler = std::move(handler);
    }

    bool Session::Credentials(std::vector<friendworld::IceServerInfo>&, std::string&) {
        return true;
    }

    bool Session::Alive() const {
        return impl && impl->sock != kInvalid && !impl->stop;
    }

    void Session::Close() {
        if (!impl || impl->stop.exchange(true))
            return;
        if (impl->reader.joinable())
            impl->reader.join();
        if (impl->sock != kInvalid)
            CloseSocket(impl->sock);
        impl->sock = kInvalid;
    }

}

#endif
