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
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "network/RakNet.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include "util/Log.hpp"

namespace perlin {
    namespace {
#ifdef _WIN32
        using Socket = SOCKET;
        constexpr Socket kInvalid = INVALID_SOCKET;
        void CloseSocket(Socket s) {
            closesocket(s);
        }
        struct WsaInit {
            WsaInit() {
                WSADATA data;
                WSAStartup(MAKEWORD(2, 2), &data);
            }
        };
        void EnsureSockets() {
            static WsaInit init;
        }
#else
        using Socket = int;
        constexpr Socket kInvalid = -1;
        void CloseSocket(Socket s) {
            close(s);
        }
        void EnsureSockets() {}
#endif

        constexpr uint8_t kMagic[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                                        0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};
        constexpr uint8_t kProtocolVersion = 11;
        constexpr int kMaxMtu = 1492;
        constexpr int kMinMtu = 576;
        constexpr size_t kInternalAddresses = 20;
        constexpr int kOrderingChannels = 32;
        constexpr int64_t kMinRto = 250;
        constexpr int64_t kMaxRto = 8000;

        enum : uint8_t {
            IdConnectedPing = 0x00,
            IdUnconnectedPing = 0x01,
            IdConnectedPong = 0x03,
            IdDetectLost = 0x04,
            IdOcr1 = 0x05,
            IdOcr1Reply = 0x06,
            IdOcr2 = 0x07,
            IdOcr2Reply = 0x08,
            IdConnectionRequest = 0x09,
            IdConnectionAccepted = 0x10,
            IdNewIncoming = 0x13,
            IdDisconnect = 0x15,
            IdIncompatible = 0x19,
            IdUnconnectedPong = 0x1c,
        };

        enum Reliability : uint8_t {
            Unreliable = 0,
            UnreliableSequenced = 1,
            Reliable = 2,
            ReliableOrdered = 3,
            ReliableSequenced = 4
        };
        bool IsReliable(uint8_t r) {
            return r == Reliable || r == ReliableOrdered || r == ReliableSequenced;
        }
        bool IsSequenced(uint8_t r) {
            return r == UnreliableSequenced || r == ReliableSequenced;
        }
        bool IsOrdered(uint8_t r) {
            return IsSequenced(r) || r == ReliableOrdered;
        }

        void PutBe16(std::vector<uint8_t>& o, uint16_t v) {
            o.push_back(uint8_t(v >> 8));
            o.push_back(uint8_t(v));
        }
        void PutBe32(std::vector<uint8_t>& o, uint32_t v) {
            for (int i = 3; i >= 0; i--)
                o.push_back(uint8_t(v >> (i * 8)));
        }
        void PutBe64(std::vector<uint8_t>& o, uint64_t v) {
            for (int i = 7; i >= 0; i--)
                o.push_back(uint8_t(v >> (i * 8)));
        }
        void PutLe24(std::vector<uint8_t>& o, uint32_t v) {
            o.push_back(uint8_t(v));
            o.push_back(uint8_t(v >> 8));
            o.push_back(uint8_t(v >> 16));
        }
        uint32_t GetLe24(const uint8_t* p) {
            return p[0] | (p[1] << 8) | (p[2] << 16);
        }
        uint16_t GetBe16(const uint8_t* p) {
            return uint16_t((p[0] << 8) | p[1]);
        }
        uint32_t GetBe32(const uint8_t* p) {
            return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        }
        uint64_t GetBe64(const uint8_t* p) {
            return (uint64_t(GetBe32(p)) << 32) | GetBe32(p + 4);
        }

        struct Address {
            sockaddr_storage storage{};
            socklen_t length = 0;

            int Family() const { return storage.ss_family; }
            const sockaddr* Raw() const { return reinterpret_cast<const sockaddr*>(&storage); }
            size_t HeaderBytes() const { return Family() == AF_INET6 ? 48 : 28; }
        };

        void PutAddr(std::vector<uint8_t>& o, const Address& a) {
            if (a.Family() == AF_INET6) {
                const auto& in6 = reinterpret_cast<const sockaddr_in6&>(a.storage);
                o.push_back(6);
                o.push_back(uint8_t(AF_INET6));
                o.push_back(uint8_t(AF_INET6 >> 8));
                PutBe16(o, ntohs(in6.sin6_port));
                PutBe32(o, ntohl(in6.sin6_flowinfo));
                const uint8_t* ip = reinterpret_cast<const uint8_t*>(&in6.sin6_addr);
                o.insert(o.end(), ip, ip + 16);
                PutBe32(o, in6.sin6_scope_id);
                return;
            }
            const auto& in4 = reinterpret_cast<const sockaddr_in&>(a.storage);
            o.push_back(4);
            const uint8_t* ip = reinterpret_cast<const uint8_t*>(&in4.sin_addr);
            for (int i = 0; i < 4; i++)
                o.push_back(uint8_t(~ip[i]));
            PutBe16(o, ntohs(in4.sin_port));
        }
        void PutBlankAddr(std::vector<uint8_t>& o) {
            o.insert(o.end(), {4, 255, 255, 255, 255, 0, 0});
        }
        size_t AddrSize(const uint8_t* p, size_t left) {
            if (left == 0)
                return 7;
            return (p[0] == 4 || p[0] == 0) ? 7 : 29;
        }

        int64_t NowMs() {
            static const auto start = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                         start)
                .count();
        }

        bool Resolve(const std::string& host, uint16_t port, Address& out, std::string& error) {
            EnsureSockets();
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo* res = nullptr;
            if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
                error = "cannot resolve '" + host + "'";
                return false;
            }
            const addrinfo* pick = nullptr;
            for (const addrinfo* r = res; r; r = r->ai_next) {
                if (r->ai_family == AF_INET) {
                    pick = r;
                    break;
                }
                if (!pick && r->ai_family == AF_INET6)
                    pick = r;
            }
            if (!pick || pick->ai_addrlen > sizeof(out.storage)) {
                freeaddrinfo(res);
                error = "cannot resolve '" + host + "'";
                return false;
            }
            out = {};
            std::memcpy(&out.storage, pick->ai_addr, pick->ai_addrlen);
            out.length = static_cast<socklen_t>(pick->ai_addrlen);
            freeaddrinfo(res);
            return true;
        }

        Socket OpenSocket(const Address& to, int timeoutMs) {
            Socket s = socket(to.Family(), SOCK_DGRAM, IPPROTO_UDP);
            if (s == kInvalid)
                return kInvalid;
#ifdef _WIN32
            DWORD tv = static_cast<DWORD>(timeoutMs);
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
            DWORD off = 0;
            DWORD bytes = 0;
            WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);
#else
            timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
            const int rcvbuf = 4 << 20;
            setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
            if (connect(s, to.Raw(), to.length) != 0) {
                CloseSocket(s);
                return kInvalid;
            }
            return s;
        }

        void SetTimeout(Socket s, int timeoutMs) {
#ifdef _WIN32
            DWORD tv = static_cast<DWORD>(timeoutMs);
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
            timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        }

        int Send(Socket s, const std::vector<uint8_t>& d) {
            return static_cast<int>(
                send(s, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0));
        }
        int Recv(Socket s, uint8_t* buf, size_t cap) {
            return static_cast<int>(recv(s, reinterpret_cast<char*>(buf), static_cast<int>(cap), 0));
        }

        std::vector<Address> LocalAddresses(Socket s, int family) {
            std::vector<Address> out;
            Address bound;
            bound.length = sizeof(bound.storage);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&bound.storage), &bound.length) != 0)
                return out;
            const uint16_t port = bound.Family() == AF_INET6
                                      ? reinterpret_cast<const sockaddr_in6&>(bound.storage).sin6_port
                                      : reinterpret_cast<const sockaddr_in&>(bound.storage).sin_port;
            const auto add = [&](const sockaddr* a) {
                if (!a || a->sa_family != family || out.size() >= kInternalAddresses)
                    return;
                Address addr;
                if (family == AF_INET6) {
                    auto& in6 = reinterpret_cast<sockaddr_in6&>(addr.storage);
                    in6 = *reinterpret_cast<const sockaddr_in6*>(a);
                    if (IN6_IS_ADDR_UNSPECIFIED(&in6.sin6_addr) || IN6_IS_ADDR_LOOPBACK(&in6.sin6_addr))
                        return;
                    in6.sin6_port = port;
                    addr.length = sizeof(sockaddr_in6);
                } else {
                    auto& in4 = reinterpret_cast<sockaddr_in&>(addr.storage);
                    in4 = *reinterpret_cast<const sockaddr_in*>(a);
                    const uint32_t ip = ntohl(in4.sin_addr.s_addr);
                    if (ip == INADDR_ANY || (ip >> 24) == 127)
                        return;
                    in4.sin_port = port;
                    addr.length = sizeof(sockaddr_in);
                }
                for (const Address& e : out)
                    if (e.length == addr.length && std::memcmp(&e.storage, &addr.storage, addr.length) == 0)
                        return;
                out.push_back(addr);
            };
#ifdef _WIN32
            char hostname[256] = {};
            if (gethostname(hostname, sizeof(hostname)) == 0) {
                addrinfo hints{};
                hints.ai_family = family;
                hints.ai_socktype = SOCK_DGRAM;
                addrinfo* res = nullptr;
                if (getaddrinfo(hostname, nullptr, &hints, &res) == 0) {
                    for (const addrinfo* r = res; r; r = r->ai_next)
                        add(r->ai_addr);
                    freeaddrinfo(res);
                }
            }
#else
            ifaddrs* interfaces = nullptr;
            if (getifaddrs(&interfaces) == 0) {
                for (const ifaddrs* i = interfaces; i; i = i->ifa_next)
                    if (i->ifa_flags & IFF_UP)
                        add(i->ifa_addr);
                freeifaddrs(interfaces);
            }
#endif
            return out;
        }

        int64_t ClientGuid(uint64_t entropy) {
            return static_cast<int64_t>((entropy & 0x3fffffffffffffffull) | 0x8000000000000000ull);
        }

        std::vector<uint8_t> UnconnectedPing(int64_t guid) {
            std::vector<uint8_t> p{IdUnconnectedPing};
            PutBe64(p, static_cast<uint64_t>(NowMs()));
            p.insert(p.end(), kMagic, kMagic + 16);
            PutBe64(p, static_cast<uint64_t>(guid));
            return p;
        }

        bool ParsePong(const uint8_t* b, int n, std::string& ad) {
            if (n < 35 || b[0] != IdUnconnectedPong)
                return false;
            const int len = GetBe16(b + 33);
            if (n < 35 + len)
                return false;
            ad.assign(reinterpret_cast<const char*>(b + 35), static_cast<size_t>(len));
            return true;
        }
    }

    struct RakNet::Impl {
        Socket sock = kInvalid;
        Address server{};
        int64_t guid = 0;
        int mtu = 1400;
        std::thread thread;
        std::atomic<bool> running{false};
        std::atomic<bool> accepted{false};
        std::atomic<int64_t> lastRecv{0};
        RakNet* owner = nullptr;

        std::mutex sendMutex;
        uint32_t datagramSeq = 0;
        uint32_t messageIndex = 0;
        uint32_t orderIndex = 0;
        uint16_t splitId = 0;
        struct Sent {
            std::vector<uint8_t> frame;
            int64_t at = 0;
            bool resent = false;
        };
        std::map<uint32_t, Sent> inflight;
        int64_t averageRtt = 0;
        int64_t rto = 1000;

        std::vector<uint32_t> acks;
        std::vector<uint32_t> naks;
        uint32_t expectedSeq = 0;
        uint32_t msgLow = 0;
        std::set<uint32_t> msgSeen;
        struct Channel {
            uint32_t nextOrder = 0;
            uint32_t sequenceOrder = 0;
            uint32_t nextSequence = 0;
            std::map<uint32_t, std::vector<uint8_t>> pending;
        };
        std::array<Channel, kOrderingChannels> channels;
        struct Split {
            uint32_t count = 0;
            uint32_t have = 0;
            std::vector<std::vector<uint8_t>> parts;
        };
        std::map<uint16_t, Split> splits;
        int64_t lastPing = 0, lastResendCheck = 0;

        moodycamel::ConcurrentQueue<std::vector<uint8_t>> inbound;

        void Fail(const std::string& why) {
            if (owner->lastError.empty())
                owner->lastError = why;
            owner->connected = false;
            running = false;
        }

        void EmitDatagramLocked(const std::vector<uint8_t>& frame, bool reliable, bool resent = false) {
            std::vector<uint8_t> d;
            d.reserve(frame.size() + 4);
            d.push_back(0x84);
            const uint32_t seq = datagramSeq++ & 0xFFFFFF;
            PutLe24(d, seq);
            d.insert(d.end(), frame.begin(), frame.end());
            Send(sock, d);
            if (reliable)
                inflight[seq] = {frame, NowMs(), resent};
        }

        static std::vector<uint8_t> BuildFrame(uint8_t rel, uint32_t msgIdx, uint32_t ordIdx, bool split,
                                               uint32_t splitCount, uint16_t sId, uint32_t splitIdx,
                                               const uint8_t* data, size_t n) {
            std::vector<uint8_t> f;
            f.reserve(n + 24);
            f.push_back(uint8_t((rel << 5) | (split ? 0x10 : 0)));
            PutBe16(f, uint16_t(n << 3));
            if (IsReliable(rel))
                PutLe24(f, msgIdx);
            if (IsSequenced(rel))
                PutLe24(f, 0);
            if (IsOrdered(rel)) {
                PutLe24(f, ordIdx);
                f.push_back(0);
            }
            if (split) {
                PutBe32(f, splitCount);
                PutBe16(f, sId);
                PutBe32(f, splitIdx);
            }
            f.insert(f.end(), data, data + n);
            return f;
        }

        bool SendPayload(const uint8_t* data, size_t n, uint8_t rel) {
            if (sock == kInvalid || n == 0)
                return false;
            std::lock_guard<std::mutex> lock(sendMutex);
            const size_t whole = size_t(mtu) - server.HeaderBytes() - 14;
            const size_t chunk = whole - 10;
            const uint32_t ord = IsOrdered(rel) ? (orderIndex++ & 0xFFFFFF) : 0;
            if (n <= whole) {
                const uint32_t idx = IsReliable(rel) ? (messageIndex++ & 0xFFFFFF) : 0;
                EmitDatagramLocked(BuildFrame(rel, idx, ord, false, 0, 0, 0, data, n), IsReliable(rel));
                return true;
            }
            const uint32_t count = uint32_t((n + chunk - 1) / chunk);
            const uint16_t sid = splitId++;
            for (uint32_t i = 0; i < count; i++) {
                const size_t off = size_t(i) * chunk;
                const size_t len = std::min(chunk, n - off);
                const uint32_t idx = IsReliable(rel) ? (messageIndex++ & 0xFFFFFF) : 0;
                EmitDatagramLocked(BuildFrame(rel, idx, ord, true, count, sid, i, data + off, len),
                                   IsReliable(rel));
            }
            return true;
        }

        bool SendControl(const std::vector<uint8_t>& payload, uint8_t rel) {
            return SendPayload(payload.data(), payload.size(), rel);
        }

        void FlushAcks() {
            FlushRanges(acks, 0xC0);
            FlushRanges(naks, 0xA0);
        }

        void FlushRanges(std::vector<uint32_t>& list, uint8_t header) {
            if (list.empty())
                return;
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
            size_t i = 0;
            while (i < list.size()) {
                std::vector<uint8_t> d{header, 0, 0};
                uint16_t records = 0;
                while (i < list.size() && d.size() < size_t(mtu) - server.HeaderBytes() - 8 &&
                       records < 0xFFFF) {
                    size_t j = i;
                    while (j + 1 < list.size() && list[j + 1] == list[j] + 1)
                        j++;
                    if (i == j) {
                        d.push_back(1);
                        PutLe24(d, list[i]);
                    } else {
                        d.push_back(0);
                        PutLe24(d, list[i]);
                        PutLe24(d, list[j]);
                    }
                    records++;
                    i = j + 1;
                }
                d[1] = uint8_t(records >> 8);
                d[2] = uint8_t(records);
                Send(sock, d);
            }
            list.clear();
        }

        void Resend(const std::vector<uint32_t>& seqs) {
            std::lock_guard<std::mutex> lock(sendMutex);
            for (uint32_t seq : seqs) {
                const auto it = inflight.find(seq);
                if (it == inflight.end())
                    continue;
                std::vector<uint8_t> frame = std::move(it->second.frame);
                inflight.erase(it);
                EmitDatagramLocked(frame, true, true);
            }
        }

        void CheckResend(int64_t now) {
            std::vector<uint32_t> due;
            {
                std::lock_guard<std::mutex> lock(sendMutex);
                for (auto& [seq, sent] : inflight)
                    if (now - sent.at > rto)
                        due.push_back(seq);
            }
            Resend(due);
        }

        void HandleAckOrNak(const uint8_t* b, int n, bool nak) {
            if (n < 3)
                return;
            const int records = GetBe16(b + 1);
            int off = 3;
            std::vector<uint32_t> seqs;
            for (int r = 0; r < records && off < n; r++) {
                if (b[off] == 1 && off + 4 <= n) {
                    seqs.push_back(GetLe24(b + off + 1));
                    off += 4;
                } else if (b[off] == 0 && off + 7 <= n) {
                    const uint32_t a = GetLe24(b + off + 1), z = GetLe24(b + off + 4);
                    for (uint32_t s = a; s <= z && seqs.size() < 8192; s++)
                        seqs.push_back(s);
                    off += 7;
                } else {
                    break;
                }
            }
            if (nak) {
                Resend(seqs);
            } else {
                const int64_t now = NowMs();
                std::lock_guard<std::mutex> lock(sendMutex);
                for (uint32_t s : seqs) {
                    const auto it = inflight.find(s);
                    if (it == inflight.end())
                        continue;
                    if (!it->second.resent)
                        UpdateRtt(now - it->second.at);
                    inflight.erase(it);
                }
            }
        }

        void UpdateRtt(int64_t rtt) {
            averageRtt = averageRtt == 0 ? rtt : (averageRtt * 7 + rtt) / 8;
            rto = std::clamp(averageRtt * 2 + 100, kMinRto, kMaxRto);
        }

        void TrackDatagram(uint32_t seq) {
            acks.push_back(seq);
            const uint32_t ahead = (seq - expectedSeq) & 0xFFFFFF;
            if (ahead >= 0x800000)
                return;
            for (uint32_t missing = expectedSeq; missing != seq && naks.size() < 4096;
                 missing = (missing + 1) & 0xFFFFFF)
                naks.push_back(missing);
            expectedSeq = (seq + 1) & 0xFFFFFF;
        }

        bool MarkMessageSeen(uint32_t idx) {
            if (((idx - msgLow) & 0xFFFFFF) >= 0x800000)
                return false;
            if (!msgSeen.insert(idx).second)
                return false;
            while (msgSeen.erase(msgLow))
                msgLow = (msgLow + 1) & 0xFFFFFF;
            return true;
        }

        void HandleDatagram(const uint8_t* b, int n) {
            if (n < 4)
                return;
            TrackDatagram(GetLe24(b + 1));
            int off = 4;
            while (off < n) {
                if (n - off < 3)
                    return;
                const uint8_t head = b[off];
                const uint8_t rel = head >> 5;
                const bool split = head & 0x10;
                const size_t len = GetBe16(b + off + 1) >> 3;
                off += 3;
                uint32_t msgIdx = 0, seqIdx = 0, ordIdx = 0;
                uint8_t channel = 0;
                if (IsReliable(rel)) {
                    if (n - off < 3)
                        return;
                    msgIdx = GetLe24(b + off);
                    off += 3;
                }
                if (IsSequenced(rel)) {
                    if (n - off < 3)
                        return;
                    seqIdx = GetLe24(b + off);
                    off += 3;
                }
                if (IsOrdered(rel)) {
                    if (n - off < 4)
                        return;
                    ordIdx = GetLe24(b + off);
                    channel = b[off + 3];
                    off += 4;
                    if (channel >= kOrderingChannels)
                        return;
                }
                uint32_t sCount = 0, sIdx = 0;
                uint16_t sId = 0;
                if (split) {
                    if (n - off < 10)
                        return;
                    sCount = GetBe32(b + off);
                    sId = GetBe16(b + off + 4);
                    sIdx = GetBe32(b + off + 6);
                    off += 10;
                }
                if (len == 0 || size_t(n - off) < len)
                    return;
                std::vector<uint8_t> content(b + off, b + off + len);
                off += int(len);

                if (IsReliable(rel) && !MarkMessageSeen(msgIdx))
                    continue;
                if (split) {
                    if (sCount == 0 || sCount > 512 || sIdx >= sCount || splits.size() > 64)
                        continue;
                    Split& sp = splits[sId];
                    if (sp.parts.empty()) {
                        sp.count = sCount;
                        sp.parts.resize(sCount);
                    }
                    if (sp.parts.size() != sCount || !sp.parts[sIdx].empty())
                        continue;
                    sp.parts[sIdx] = std::move(content);
                    if (++sp.have < sp.count)
                        continue;
                    content.clear();
                    for (auto& part : sp.parts)
                        content.insert(content.end(), part.begin(), part.end());
                    splits.erase(sId);
                }
                if (rel == ReliableOrdered)
                    Ordered(channels[channel], ordIdx, std::move(content));
                else if (IsSequenced(rel))
                    Sequenced(channels[channel], ordIdx, seqIdx, std::move(content));
                else
                    Deliver(std::move(content));
            }
        }

        static bool Before(uint32_t a, uint32_t b) { return ((a - b) & 0xFFFFFF) >= 0x800000; }

        void Ordered(Channel& ch, uint32_t idx, std::vector<uint8_t> content) {
            if (Before(idx, ch.nextOrder))
                return;
            ch.pending.emplace(idx, std::move(content));
            if (ch.pending.size() > 20000) {
                Fail("RakNet: too many out-of-order packets");
                return;
            }
            for (auto it = ch.pending.find(ch.nextOrder); it != ch.pending.end();
                 it = ch.pending.find(ch.nextOrder)) {
                std::vector<uint8_t> c = std::move(it->second);
                ch.pending.erase(it);
                ch.nextOrder = (ch.nextOrder + 1) & 0xFFFFFF;
                Deliver(std::move(c));
            }
        }

        void Sequenced(Channel& ch, uint32_t ordIdx, uint32_t seqIdx, std::vector<uint8_t> content) {
            if (Before(ordIdx, ch.sequenceOrder) ||
                (ordIdx == ch.sequenceOrder && Before(seqIdx, ch.nextSequence)))
                return;
            ch.sequenceOrder = ordIdx;
            ch.nextSequence = (seqIdx + 1) & 0xFFFFFF;
            Deliver(std::move(content));
        }

        void Deliver(std::vector<uint8_t> c) {
            if (c.empty())
                return;
            switch (c[0]) {
            case 0xFE:
                c.erase(c.begin());
                inbound.enqueue(std::move(c));
                return;
            case IdConnectedPing: {
                if (c.size() < 9)
                    return;
                std::vector<uint8_t> pong{IdConnectedPong};
                pong.insert(pong.end(), c.begin() + 1, c.begin() + 9);
                PutBe64(pong, static_cast<uint64_t>(NowMs()));
                SendControl(pong, Unreliable);
                return;
            }
            case IdConnectionAccepted: {
                if (accepted)
                    return;
                size_t off = 1;
                off += AddrSize(c.data() + off, c.size() - off) + 2;
                int count = 0;
                while (count < 20 && c.size() > off + 16) {
                    off += AddrSize(c.data() + off, c.size() - off);
                    count++;
                }
                uint64_t pongTime = 0;
                if (c.size() >= off + 16)
                    pongTime = GetBe64(c.data() + off + 8);
                std::vector<uint8_t> nic{IdNewIncoming};
                PutAddr(nic, server);
                const std::vector<Address> local = LocalAddresses(sock, server.Family());
                for (size_t i = 0; i < kInternalAddresses; i++) {
                    if (i < local.size())
                        PutAddr(nic, local[i]);
                    else
                        PutBlankAddr(nic);
                }
                PutBe64(nic, pongTime);
                PutBe64(nic, static_cast<uint64_t>(NowMs()));
                SendControl(nic, ReliableOrdered);
                accepted = true;
                return;
            }
            case IdDisconnect:
                Fail("the server closed the connection");
                return;
            case IdDetectLost: {
                std::vector<uint8_t> ping{IdConnectedPing};
                PutBe64(ping, static_cast<uint64_t>(NowMs()));
                SendControl(ping, ReliableOrdered);
                return;
            }
            default:
                return;
            }
        }

        void Loop() {
            uint8_t buf[2048];
            while (running) {
                const int n = Recv(sock, buf, sizeof(buf));
                const int64_t now = NowMs();
                if (n > 0) {
                    lastRecv = now;
                    if (buf[0] & 0x80) {
                        if (buf[0] & 0x40)
                            HandleAckOrNak(buf, n, false);
                        else if (buf[0] & 0x20)
                            HandleAckOrNak(buf, n, true);
                        else
                            HandleDatagram(buf, n);
                    }
                }
                FlushAcks();
                if (now - lastResendCheck >= 100) {
                    lastResendCheck = now;
                    CheckResend(now);
                }
                if (accepted && now - lastPing >= 2500) {
                    lastPing = now;
                    std::vector<uint8_t> ping{IdConnectedPing};
                    PutBe64(ping, static_cast<uint64_t>(now));
                    SendControl(ping, ReliableOrdered);
                }
                if (now - lastRecv > 15000) {
                    Fail("the server stopped answering (RakNet timeout)");
                }
            }
        }
    };

    RakNet::RakNet() = default;
    RakNet::~RakNet() {
        Disconnect();
    }

    std::string RakNet::LastError() const {
        return lastError;
    }

    bool RakNet::Ping(const std::string& host, uint16_t port, std::string& ad, std::string& error,
                      int timeoutMs) {
        Address to{};
        if (!Resolve(host, port, to, error))
            return false;
        Socket s = OpenSocket(to, 300);
        if (s == kInvalid) {
            error = "cannot open a UDP socket";
            return false;
        }
        std::mt19937_64 rng(std::random_device{}());
        const auto ping = UnconnectedPing(ClientGuid(rng()));
        uint8_t buf[2048];
        const int64_t end = NowMs() + timeoutMs;
        bool ok = false;
        while (!ok && NowMs() < end) {
            Send(s, ping);
            const int64_t until = std::min(end, NowMs() + 500);
            while (NowMs() < until) {
                const int n = Recv(s, buf, sizeof(buf));
                if (n > 0 && ParsePong(buf, n, ad)) {
                    ok = true;
                    break;
                }
            }
        }
        CloseSocket(s);
        if (!ok)
            error = "no RakNet answer from " + host + ":" + std::to_string(port) + " (UDP)";
        return ok;
    }

    bool RakNet::Discover(std::vector<LanServer>& found, std::string& error, int timeoutMs) {
        EnsureSockets();
        Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == kInvalid) {
            error = "cannot open a UDP socket";
            return false;
        }
        int on = 1;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
        SetTimeout(s, 100);
        std::mt19937_64 rng(std::random_device{}());
        const auto ping = UnconnectedPing(ClientGuid(rng()));
        sockaddr_in targets[2]{};
        for (sockaddr_in& t : targets) {
            t.sin_family = AF_INET;
            t.sin_port = htons(19132);
        }
        targets[0].sin_addr.s_addr = htonl(INADDR_BROADCAST);
        targets[1].sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        uint8_t buf[2048];
        const int64_t end = NowMs() + timeoutMs;
        int64_t nextPing = 0;
        while (NowMs() < end) {
            if (NowMs() >= nextPing) {
                for (const sockaddr_in& t : targets)
                    sendto(s, reinterpret_cast<const char*>(ping.data()), static_cast<int>(ping.size()), 0,
                           reinterpret_cast<const sockaddr*>(&t), sizeof(t));
                nextPing = NowMs() + 500;
            }
            sockaddr_in from{};
#ifdef _WIN32
            int fromLen = sizeof(from);
#else
            socklen_t fromLen = sizeof(from);
#endif
            const int n = static_cast<int>(recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                                                    reinterpret_cast<sockaddr*>(&from), &fromLen));
            std::string ad;
            if (n <= 0 || !ParsePong(buf, n, ad))
                continue;
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            const uint16_t port = ntohs(from.sin_port);
            bool known = false;
            for (const LanServer& f : found)
                known = known || (f.host == ip && f.port == port);
            if (!known)
                found.push_back({ip, port, ad});
        }
        CloseSocket(s);
        return true;
    }

    bool RakNet::Connect(const std::string& host, uint16_t port) {
        Disconnect();
        lastError.clear();
        impl = std::make_unique<Impl>();
        impl->owner = this;
        Impl& s = *impl;
        std::mt19937_64 rng(std::random_device{}());
        s.guid = ClientGuid(rng());

        if (!Resolve(host, port, s.server, lastError))
            return false;
        s.sock = OpenSocket(s.server, 250);
        if (s.sock == kInvalid) {
            lastError = "cannot open a UDP socket";
            return false;
        }

        std::string ad;
        std::string pingError;
        if (Ping(host, port, ad, pingError, 1500)) {
            Log("raknet", "server: {}", ad);
            info.name = ad;
            size_t a = 0;
            std::vector<std::string> parts;
            for (size_t i = 0; i <= ad.size(); i++)
                if (i == ad.size() || ad[i] == ';') {
                    parts.push_back(ad.substr(a, i - a));
                    a = i + 1;
                }
            if (parts.size() > 3) {
                info.name = parts[1];
                const int theirs = std::atoi(parts[2].c_str());
                info.advertisedProtocol = theirs;
                info.advertisedVersion = parts[3];
                if (theirs != 0 && theirs != kBedrockProtocol)
                    Log("raknet", "note: the server advertises protocol {} ({}), Perlin speaks {} ({})",
                        theirs, parts[3], kBedrockProtocol, kBedrockVersion);
            }
        }
        info.protocol = kBedrockProtocol;
        info.version = kBedrockVersion;

        uint8_t buf[2048];
        bool gotReply1 = false;
        bool security = false;
        uint32_t cookie = 0;
        int mtu = 0;
        for (int size : {kMaxMtu, 1200, kMinMtu}) {
            std::vector<uint8_t> r1(size_t(size) - s.server.HeaderBytes(), 0);
            r1[0] = IdOcr1;
            std::memcpy(&r1[1], kMagic, 16);
            r1[17] = kProtocolVersion;
            for (int attempt = 0; attempt < 3 && !gotReply1; attempt++) {
                Send(s.sock, r1);
                const int64_t until = NowMs() + 600;
                while (NowMs() < until && !gotReply1) {
                    const int n = Recv(s.sock, buf, sizeof(buf));
                    if (n <= 0)
                        continue;
                    if (buf[0] == IdIncompatible) {
                        lastError =
                            "the server does not speak RakNet protocol " + std::to_string(kProtocolVersion);
                        CloseSocket(s.sock);
                        s.sock = kInvalid;
                        return false;
                    }
                    if (buf[0] == IdOcr1Reply && n >= 28) {
                        security = buf[25] != 0;
                        if (security) {
                            cookie = GetBe32(buf + 26);
                            mtu = GetBe16(buf + 30);
                        } else {
                            mtu = GetBe16(buf + 26);
                        }
                        gotReply1 = true;
                    }
                }
            }
            if (gotReply1)
                break;
        }
        if (!gotReply1) {
            lastError = "no answer to the RakNet handshake from " + host + ":" + std::to_string(port);
            CloseSocket(s.sock);
            s.sock = kInvalid;
            return false;
        }
        mtu = std::clamp(mtu, 400, kMaxMtu);

        std::vector<uint8_t> r2{IdOcr2};
        r2.insert(r2.end(), kMagic, kMagic + 16);
        if (security) {
            PutBe32(r2, cookie);
            r2.push_back(0);
        }
        PutAddr(r2, s.server);
        PutBe16(r2, uint16_t(mtu));
        PutBe64(r2, static_cast<uint64_t>(s.guid));
        bool gotReply2 = false;
        for (int attempt = 0; attempt < 6 && !gotReply2; attempt++) {
            Send(s.sock, r2);
            const int64_t until = NowMs() + 600;
            while (NowMs() < until && !gotReply2) {
                const int n = Recv(s.sock, buf, sizeof(buf));
                if (n > 0 && buf[0] == IdOcr2Reply && n >= 28) {
                    const size_t off = 25 + AddrSize(buf + 25, size_t(n) - 25);
                    if (size_t(n) >= off + 2)
                        mtu = std::clamp(int(GetBe16(buf + off)), 400, kMaxMtu);
                    gotReply2 = true;
                }
            }
        }
        if (!gotReply2) {
            lastError = "the RakNet handshake with " + host + " did not finish (second request unanswered)";
            CloseSocket(s.sock);
            s.sock = kInvalid;
            return false;
        }
        s.mtu = mtu;
        Log("raknet", "connected at MTU {}", mtu);

        SetTimeout(s.sock, 5);
        s.lastRecv = NowMs();
        s.running = true;
        connected = true;
        s.thread = std::thread([this] { impl->Loop(); });
        std::vector<uint8_t> req{IdConnectionRequest};
        PutBe64(req, static_cast<uint64_t>(s.guid));
        PutBe64(req, static_cast<uint64_t>(NowMs()));
        req.push_back(0);
        s.SendControl(req, Reliable);
        const int64_t until = NowMs() + 8000;
        while (!s.accepted && s.running && NowMs() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!s.accepted) {
            if (lastError.empty())
                lastError = "the server did not accept the RakNet connection";
            Disconnect();
            return false;
        }
        return true;
    }

    void RakNet::Disconnect() {
        if (!impl)
            return;
        Impl& s = *impl;
        if (s.sock != kInvalid && connected) {
            std::vector<uint8_t> bye{IdDisconnect};
            s.SendControl(bye, ReliableOrdered);
            const int64_t until = NowMs() + 300;
            while (s.running && NowMs() < until) {
                {
                    std::lock_guard<std::mutex> lock(s.sendMutex);
                    if (s.inflight.empty())
                        break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        connected = false;
        s.running = false;
        if (s.thread.joinable())
            s.thread.join();
        if (s.sock != kInvalid) {
            CloseSocket(s.sock);
            s.sock = kInvalid;
        }
    }

    bool RakNet::SendReliable(const std::vector<uint8_t>& data) {
        if (!impl || !connected)
            return false;
        std::vector<uint8_t> payload;
        payload.reserve(data.size() + 1);
        payload.push_back(0xFE);
        payload.insert(payload.end(), data.begin(), data.end());
        return impl->SendPayload(payload.data(), payload.size(), ReliableOrdered);
    }

    bool RakNet::TryReceive(std::vector<uint8_t>& out) {
        return impl && impl->inbound.try_dequeue(out);
    }
}
