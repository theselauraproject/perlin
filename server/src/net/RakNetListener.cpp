#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "core/Log.hpp"
#include "core/Server.hpp"
#include "net/Connection.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace perlin::server {
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
        constexpr int kMaxMtu = 1400;
        constexpr int kMinMtu = 400;
        constexpr int kOrderingChannels = 32;
        constexpr int64_t kMinRto = 200;
        constexpr int64_t kMaxRto = 4000;
        constexpr int64_t kTimeoutMs = 15000;
        constexpr int64_t kHandshakeTimeoutMs = 10000;
        constexpr size_t kWindow = 2048;
        constexpr size_t kMaxPeers = 512;

        enum : uint8_t {
            IdConnectedPing = 0x00,
            IdUnconnectedPing = 0x01,
            IdUnconnectedPingOpen = 0x02,
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
            IdGame = 0xfe,
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
            return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        }
        uint64_t GetBe64(const uint8_t* p) {
            return (uint64_t(GetBe32(p)) << 32) | GetBe32(p + 4);
        }

        int64_t NowMs() {
            static const auto start = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                .count();
        }

        struct Address {
            sockaddr_storage storage{};
            socklen_t length = 0;

            int Family() const { return storage.ss_family; }
            const sockaddr* Raw() const { return reinterpret_cast<const sockaddr*>(&storage); }
            size_t HeaderBytes() const { return Family() == AF_INET6 ? 48 : 28; }
            std::string Key() const { return std::string(reinterpret_cast<const char*>(&storage), length); }
            std::string Text() const {
                char host[INET6_ADDRSTRLEN] = {};
                uint16_t port = 0;
                if (Family() == AF_INET6) {
                    const auto& in6 = reinterpret_cast<const sockaddr_in6&>(storage);
                    inet_ntop(AF_INET6, &in6.sin6_addr, host, sizeof host);
                    port = ntohs(in6.sin6_port);
                    return std::string("[") + host + "]:" + std::to_string(port);
                }
                const auto& in4 = reinterpret_cast<const sockaddr_in&>(storage);
                inet_ntop(AF_INET, &in4.sin_addr, host, sizeof host);
                port = ntohs(in4.sin_port);
                return std::string(host) + ":" + std::to_string(port);
            }
            bool Loopback() const {
                if (Family() == AF_INET6) {
                    const auto& in6 = reinterpret_cast<const sockaddr_in6&>(storage);
                    return IN6_IS_ADDR_LOOPBACK(&in6.sin6_addr);
                }
                const auto& in4 = reinterpret_cast<const sockaddr_in&>(storage);
                return (ntohl(in4.sin_addr.s_addr) >> 24) == 127;
            }
        };

        void PutAddr(std::vector<uint8_t>& o, const Address& a) {
            if (a.Family() == AF_INET6) {
                const auto& in6 = reinterpret_cast<const sockaddr_in6&>(a.storage);
                o.push_back(6);
                o.push_back(uint8_t(23));
                o.push_back(0);
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

        struct Frame {
            std::vector<uint8_t> bytes;
            bool reliable = false;
        };

        class RakNetListener;

        class Peer final : public Connection, public std::enable_shared_from_this<Peer> {
        public:
            Peer(RakNetListener& owner, Socket sock, Address remote, int mtu, int64_t guid)
                : owner(owner), sock(sock), remote(remote), mtu(mtu), guid(guid) {
                lastRecv = NowMs();
                created = lastRecv;
            }

            bool Send(const uint8_t* data, size_t size, bool) override {
                if (closed || size == 0)
                    return false;
                std::vector<uint8_t> payload;
                payload.reserve(size + 1);
                payload.push_back(IdGame);
                payload.insert(payload.end(), data, data + size);
                std::lock_guard lock(mutex);
                QueuePayloadLocked(payload.data(), payload.size(), ReliableOrdered);
                FlushLocked(NowMs());
                return true;
            }

            void Close() override {
                if (closed.exchange(true))
                    return;
                closedAt = NowMs();
                std::lock_guard lock(mutex);
                const std::vector<uint8_t> bye{IdDisconnect};
                QueuePayloadLocked(bye.data(), bye.size(), ReliableOrdered);
                FlushLocked(NowMs());
            }

            bool Alive() const override { return !closed; }
            std::string Describe() const override { return "raknet " + remote.Text(); }
            bool Local() const override { return remote.Loopback(); }

            void OnDatagram(const uint8_t* b, int n, std::vector<std::vector<uint8_t>>& delivered);
            void Tick(int64_t now);
            bool Expired(int64_t now) const {
                if (closed && now - closedAt > 1000)
                    return true;
                if (!handshakeDone && now - created > kHandshakeTimeoutMs)
                    return true;
                return now - lastRecv > kTimeoutMs;
            }
            void MarkClosed() {
                if (!closed.exchange(true))
                    closedAt = NowMs();
                else if (closedAt == 0)
                    closedAt = NowMs();
            }

            bool handshakeDone = false;
            bool openSent = false;
            int64_t lastRecv = 0;
            int64_t created = 0;
            int64_t closedAt = 0;
            std::atomic<bool> closed{false};
            Address remote;
            int64_t guid = 0;

        private:
            struct Inflight {
                std::vector<Frame> frames;
                int64_t at = 0;
                bool resent = false;
            };
            struct Channel {
                uint32_t nextOrder = 0;
                uint32_t sequenceOrder = 0;
                uint32_t nextSequence = 0;
                std::map<uint32_t, std::vector<uint8_t>> pending;
            };
            struct Split {
                uint32_t count = 0;
                uint32_t have = 0;
                std::vector<std::vector<uint8_t>> parts;
            };

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

            size_t MaxFrameBody() const { return static_cast<size_t>(mtu) - remote.HeaderBytes() - 4 - 20; }

            void QueuePayloadLocked(const uint8_t* data, size_t n, uint8_t rel) {
                const size_t whole = MaxFrameBody();
                const size_t chunk = whole - 10;
                const uint32_t ord = IsOrdered(rel) ? (orderIndex++ & 0xFFFFFF) : 0;
                if (n <= whole) {
                    const uint32_t idx = IsReliable(rel) ? (messageIndex++ & 0xFFFFFF) : 0;
                    queue.push_back({BuildFrame(rel, idx, ord, false, 0, 0, 0, data, n), IsReliable(rel)});
                    return;
                }
                const uint32_t count = uint32_t((n + chunk - 1) / chunk);
                const uint16_t sid = splitId++;
                for (uint32_t i = 0; i < count; i++) {
                    const size_t off = size_t(i) * chunk;
                    const size_t len = std::min(chunk, n - off);
                    const uint32_t idx = IsReliable(rel) ? (messageIndex++ & 0xFFFFFF) : 0;
                    queue.push_back(
                        {BuildFrame(rel, idx, ord, true, count, sid, i, data + off, len), IsReliable(rel)});
                }
            }

            void EmitLocked(std::vector<Frame> frames, int64_t now, bool resent) {
                std::vector<uint8_t> d;
                size_t bytes = 4;
                for (const Frame& f : frames)
                    bytes += f.bytes.size();
                d.reserve(bytes);
                d.push_back(0x84);
                const uint32_t seq = datagramSeq++ & 0xFFFFFF;
                PutLe24(d, seq);
                bool reliable = false;
                for (const Frame& f : frames) {
                    d.insert(d.end(), f.bytes.begin(), f.bytes.end());
                    reliable |= f.reliable;
                }
                sendto(sock, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0, remote.Raw(),
                       remote.length);
                if (reliable) {
                    std::erase_if(frames, [](const Frame& f) { return !f.reliable; });
                    inflight[seq] = {std::move(frames), now, resent};
                }
            }

            void FlushLocked(int64_t now) {
                const size_t limit = static_cast<size_t>(mtu) - remote.HeaderBytes();
                while (!queue.empty() && inflight.size() < kWindow) {
                    std::vector<Frame> batch;
                    size_t size = 4;
                    while (!queue.empty() && (batch.empty() || size + queue.front().bytes.size() <= limit)) {
                        size += queue.front().bytes.size();
                        batch.push_back(std::move(queue.front()));
                        queue.pop_front();
                    }
                    EmitLocked(std::move(batch), now, false);
                }
            }

            void ResendLocked(const std::vector<uint32_t>& seqs, int64_t now) {
                for (uint32_t seq : seqs) {
                    const auto it = inflight.find(seq);
                    if (it == inflight.end())
                        continue;
                    std::vector<Frame> frames = std::move(it->second.frames);
                    inflight.erase(it);
                    EmitLocked(std::move(frames), now, true);
                }
            }

            void UpdateRtt(int64_t rtt) {
                averageRtt = averageRtt == 0 ? rtt : (averageRtt * 7 + rtt) / 8;
                rto = std::clamp(averageRtt * 2 + 50, kMinRto, kMaxRto);
            }

            void HandleAckOrNak(const uint8_t* b, int n, bool nak, int64_t now) {
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
                std::lock_guard lock(mutex);
                if (nak) {
                    ResendLocked(seqs, now);
                    return;
                }
                for (uint32_t s : seqs) {
                    const auto it = inflight.find(s);
                    if (it == inflight.end())
                        continue;
                    if (!it->second.resent)
                        UpdateRtt(now - it->second.at);
                    inflight.erase(it);
                }
                FlushLocked(now);
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

            static bool Before(uint32_t a, uint32_t b) { return ((a - b) & 0xFFFFFF) >= 0x800000; }

            void Ordered(Channel& ch, uint32_t idx, std::vector<uint8_t> content,
                         std::vector<std::vector<uint8_t>>& out) {
                if (Before(idx, ch.nextOrder))
                    return;
                ch.pending.emplace(idx, std::move(content));
                if (ch.pending.size() > 20000) {
                    MarkClosed();
                    return;
                }
                for (auto it = ch.pending.find(ch.nextOrder); it != ch.pending.end();
                     it = ch.pending.find(ch.nextOrder)) {
                    out.push_back(std::move(it->second));
                    ch.pending.erase(it);
                    ch.nextOrder = (ch.nextOrder + 1) & 0xFFFFFF;
                }
            }

            void Sequenced(Channel& ch, uint32_t ordIdx, uint32_t seqIdx, std::vector<uint8_t> content,
                           std::vector<std::vector<uint8_t>>& out) {
                if (Before(ordIdx, ch.sequenceOrder) || (ordIdx == ch.sequenceOrder && Before(seqIdx, ch.nextSequence)))
                    return;
                ch.sequenceOrder = ordIdx;
                ch.nextSequence = (seqIdx + 1) & 0xFFFFFF;
                out.push_back(std::move(content));
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
                    while (i < list.size() && d.size() < size_t(mtu) - remote.HeaderBytes() - 8 && records < 0xFFFF) {
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
                    sendto(sock, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0,
                           remote.Raw(), remote.length);
                }
                list.clear();
            }

            void HandleControl(std::vector<uint8_t> c, std::vector<std::vector<uint8_t>>& delivered);

            RakNetListener& owner;
            Socket sock;
            int mtu;
            std::mutex mutex;
            uint32_t datagramSeq = 0;
            uint32_t messageIndex = 0;
            uint32_t orderIndex = 0;
            uint16_t splitId = 0;
            std::deque<Frame> queue;
            std::map<uint32_t, Inflight> inflight;
            int64_t averageRtt = 0;
            int64_t rto = 800;
            int64_t lastPing = 0;
            std::vector<uint32_t> acks, naks;
            uint32_t expectedSeq = 0;
            uint32_t msgLow = 0;
            std::set<uint32_t> msgSeen;
            std::array<Channel, kOrderingChannels> channels;
            std::map<uint16_t, Split> splits;
        };

        class RakNetListener final : public Listener {
        public:
            RakNetListener(const ServerConfig& config, std::function<std::string()> motd)
                : config(config), motd(std::move(motd)) {
                std::random_device rd;
                guid = static_cast<int64_t>((static_cast<uint64_t>(rd()) << 32) ^ rd());
            }
            ~RakNetListener() override { Stop(); }

            const char* Name() const override { return "raknet"; }

            bool Start(EventQueue& q, std::string& error) override {
                EnsureSockets();
                queue = &q;
                if (!Bind(AF_INET, config.bindAddress, config.port, sock4, error))
                    return false;
                std::string ignored;
                if (config.portV6 != 0 && config.portV6 != config.port)
                    Bind(AF_INET6, "::", config.portV6, sock6, ignored);
                running = true;
                thread = std::thread([this] { Loop(); });
                return true;
            }

            void Stop() override {
                if (!running.exchange(false))
                    return;
                if (thread.joinable())
                    thread.join();
                {
                    std::lock_guard lock(peersMutex);
                    for (auto& [key, peer] : peers) {
                        peer->Close();
                        Push(NetEvent::Type::Close, peer, {});
                    }
                    peers.clear();
                }
                if (sock4 != kInvalid)
                    CloseSocket(sock4);
                if (sock6 != kInvalid)
                    CloseSocket(sock6);
                sock4 = sock6 = kInvalid;
            }

            void Push(NetEvent::Type type, const std::shared_ptr<Peer>& peer, std::vector<uint8_t> data) {
                if (!queue)
                    return;
                NetEvent e;
                e.type = type;
                e.connection = peer;
                e.data = std::move(data);
                queue->Push(std::move(e));
            }

            int64_t Guid() const { return guid; }

        private:
            bool Bind(int family, const std::string& address, uint16_t port, Socket& out, std::string& error) {
                Socket s = socket(family, SOCK_DGRAM, IPPROTO_UDP);
                if (s == kInvalid) {
                    error = "raknet: cannot open a udp socket";
                    return false;
                }
                const int big = 8 << 20;
                setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&big), sizeof big);
                setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&big), sizeof big);
#ifdef _WIN32
                DWORD tv = 5;
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof tv);
                DWORD off = 0, bytes = 0;
                WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &off, sizeof off, nullptr, 0, &bytes, nullptr, nullptr);
#else
                timeval tv{0, 5000};
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
                Address a;
                if (family == AF_INET6) {
                    const int only = 1;
                    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&only), sizeof only);
                    auto& in6 = reinterpret_cast<sockaddr_in6&>(a.storage);
                    in6.sin6_family = AF_INET6;
                    in6.sin6_port = htons(port);
                    inet_pton(AF_INET6, address.c_str(), &in6.sin6_addr);
                    a.length = sizeof(sockaddr_in6);
                } else {
                    auto& in4 = reinterpret_cast<sockaddr_in&>(a.storage);
                    in4.sin_family = AF_INET;
                    in4.sin_port = htons(port);
                    if (address.empty() || inet_pton(AF_INET, address.c_str(), &in4.sin_addr) != 1)
                        in4.sin_addr.s_addr = htonl(INADDR_ANY);
                    a.length = sizeof(sockaddr_in);
                }
                if (bind(s, a.Raw(), a.length) != 0) {
                    CloseSocket(s);
                    error = "raknet: cannot listen on udp " + address + ":" + std::to_string(port);
                    return false;
                }
                out = s;
                return true;
            }

            void SendRaw(Socket s, const Address& to, const std::vector<uint8_t>& d) {
                sendto(s, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0, to.Raw(), to.length);
            }

            void Unconnected(Socket s, const Address& from, const uint8_t* b, int n) {
                switch (b[0]) {
                case IdUnconnectedPing:
                case IdUnconnectedPingOpen: {
                    if (n < 33)
                        return;
                    const std::string info = PongText();
                    std::vector<uint8_t> p{IdUnconnectedPong};
                    p.insert(p.end(), b + 1, b + 9);
                    PutBe64(p, static_cast<uint64_t>(guid));
                    p.insert(p.end(), kMagic, kMagic + 16);
                    PutBe16(p, static_cast<uint16_t>(info.size()));
                    p.insert(p.end(), info.begin(), info.end());
                    SendRaw(s, from, p);
                    return;
                }
                case IdOcr1: {
                    if (n < 18 || std::memcmp(b + 1, kMagic, 16) != 0)
                        return;
                    if (b[17] != kProtocolVersion) {
                        std::vector<uint8_t> p{IdIncompatible, kProtocolVersion};
                        p.insert(p.end(), kMagic, kMagic + 16);
                        PutBe64(p, static_cast<uint64_t>(guid));
                        SendRaw(s, from, p);
                        return;
                    }
                    const int mtu = std::clamp(n + static_cast<int>(from.HeaderBytes()), kMinMtu, kMaxMtu);
                    std::vector<uint8_t> p{IdOcr1Reply};
                    p.insert(p.end(), kMagic, kMagic + 16);
                    PutBe64(p, static_cast<uint64_t>(guid));
                    p.push_back(0);
                    PutBe16(p, static_cast<uint16_t>(mtu));
                    SendRaw(s, from, p);
                    return;
                }
                case IdOcr2: {
                    if (n < 17 + 7 + 10 || std::memcmp(b + 1, kMagic, 16) != 0)
                        return;
                    size_t off = 17;
                    off += AddrSize(b + off, static_cast<size_t>(n) - off);
                    if (static_cast<size_t>(n) < off + 10)
                        return;
                    const int mtu = std::clamp<int>(GetBe16(b + off), kMinMtu, kMaxMtu);
                    const int64_t clientGuid = static_cast<int64_t>(GetBe64(b + off + 2));
                    std::shared_ptr<Peer> peer;
                    {
                        std::lock_guard lock(peersMutex);
                        auto it = peers.find(from.Key());
                        if (it != peers.end() && it->second->handshakeDone) {
                            it->second->MarkClosed();
                            Push(NetEvent::Type::Close, it->second, {});
                            peers.erase(it);
                            it = peers.end();
                        }
                        if (it == peers.end()) {
                            if (peers.size() >= kMaxPeers)
                                return;
                            peer = std::make_shared<Peer>(*this, s, from, mtu, clientGuid);
                            peers[from.Key()] = peer;
                        }
                    }
                    std::vector<uint8_t> p{IdOcr2Reply};
                    p.insert(p.end(), kMagic, kMagic + 16);
                    PutBe64(p, static_cast<uint64_t>(guid));
                    PutAddr(p, from);
                    PutBe16(p, static_cast<uint16_t>(mtu));
                    p.push_back(0);
                    SendRaw(s, from, p);
                    return;
                }
                default:
                    return;
                }
            }

            std::string PongText() {
                const std::string base = motd ? motd() : std::string();
                return base + std::to_string(guid) + ";" + config.levelName + ";" +
                       (config.gameMode == 1 ? "Creative" : config.gameMode == 2 ? "Adventure" : "Survival") + ";" +
                       std::to_string(config.gameMode == 1 ? 1 : config.gameMode == 2 ? 2 : 0) + ";" +
                       std::to_string(config.port) + ";" + std::to_string(config.portV6) + ";0;";
            }

            void Receive(Socket s) {
                uint8_t buf[2048];
                for (int burst = 0; burst < 512; burst++) {
                    Address from;
                    from.length = sizeof(from.storage);
                    const int n = static_cast<int>(recvfrom(s, reinterpret_cast<char*>(buf), sizeof buf, 0,
                                                            reinterpret_cast<sockaddr*>(&from.storage), &from.length));
                    if (n <= 0)
                        return;
                    std::shared_ptr<Peer> peer;
                    {
                        std::lock_guard lock(peersMutex);
                        const auto it = peers.find(from.Key());
                        if (it != peers.end())
                            peer = it->second;
                    }
                    if (peer && (buf[0] & 0x80)) {
                        std::vector<std::vector<uint8_t>> delivered;
                        peer->OnDatagram(buf, n, delivered);
                        for (auto& message : delivered)
                            Push(NetEvent::Type::Message, peer, std::move(message));
                        continue;
                    }
                    Unconnected(s, from, buf, n);
#ifdef _WIN32
                    u_long pending = 0;
                    ioctlsocket(s, FIONREAD, &pending);
                    if (pending == 0)
                        return;
#endif
                }
            }

            void Loop() {
                while (running) {
                    if (sock4 != kInvalid)
                        Receive(sock4);
                    if (sock6 != kInvalid)
                        Receive(sock6);
                    const int64_t now = NowMs();
                    std::vector<std::shared_ptr<Peer>> list;
                    {
                        std::lock_guard lock(peersMutex);
                        list.reserve(peers.size());
                        for (auto& [key, peer] : peers)
                            list.push_back(peer);
                    }
                    for (const std::shared_ptr<Peer>& peer : list) {
                        peer->Tick(now);
                        if (peer->Expired(now)) {
                            const bool announced = peer->openSent;
                            peer->MarkClosed();
                            {
                                std::lock_guard lock(peersMutex);
                                const auto it = peers.find(peer->remote.Key());
                                if (it != peers.end() && it->second == peer)
                                    peers.erase(it);
                            }
                            if (announced)
                                Push(NetEvent::Type::Close, peer, {});
                        }
                    }
                }
            }

            const ServerConfig& config;
            std::function<std::string()> motd;
            EventQueue* queue = nullptr;
            Socket sock4 = kInvalid, sock6 = kInvalid;
            std::atomic<bool> running{false};
            std::thread thread;
            int64_t guid = 0;
            std::mutex peersMutex;
            std::unordered_map<std::string, std::shared_ptr<Peer>> peers;

            friend class Peer;
        };

        void Peer::OnDatagram(const uint8_t* b, int n, std::vector<std::vector<uint8_t>>& delivered) {
            const int64_t now = NowMs();
            lastRecv = now;
            if (b[0] & 0x40) {
                HandleAckOrNak(b, n, false, now);
                return;
            }
            if (b[0] & 0x20) {
                HandleAckOrNak(b, n, true, now);
                return;
            }
            if (n < 4)
                return;
            std::vector<std::vector<uint8_t>> ready;
            {
                std::lock_guard lock(mutex);
                TrackDatagram(GetLe24(b + 1));
                int off = 4;
                while (off < n) {
                    if (n - off < 3)
                        break;
                    const uint8_t head = b[off];
                    const uint8_t rel = head >> 5;
                    const bool split = head & 0x10;
                    const size_t len = GetBe16(b + off + 1) >> 3;
                    off += 3;
                    uint32_t msgIdx = 0, seqIdx = 0, ordIdx = 0;
                    uint8_t channel = 0;
                    if (IsReliable(rel)) {
                        if (n - off < 3)
                            break;
                        msgIdx = GetLe24(b + off);
                        off += 3;
                    }
                    if (IsSequenced(rel)) {
                        if (n - off < 3)
                            break;
                        seqIdx = GetLe24(b + off);
                        off += 3;
                    }
                    if (IsOrdered(rel)) {
                        if (n - off < 4)
                            break;
                        ordIdx = GetLe24(b + off);
                        channel = b[off + 3];
                        off += 4;
                        if (channel >= kOrderingChannels)
                            break;
                    }
                    uint32_t sCount = 0, sIdx = 0;
                    uint16_t sId = 0;
                    if (split) {
                        if (n - off < 10)
                            break;
                        sCount = GetBe32(b + off);
                        sId = GetBe16(b + off + 4);
                        sIdx = GetBe32(b + off + 6);
                        off += 10;
                    }
                    if (len == 0 || size_t(n - off) < len)
                        break;
                    std::vector<uint8_t> content(b + off, b + off + len);
                    off += int(len);
                    if (IsReliable(rel) && !MarkMessageSeen(msgIdx))
                        continue;
                    if (split) {
                        if (sCount == 0 || sCount > 8192 || sIdx >= sCount || splits.size() > 64)
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
                        Ordered(channels[channel], ordIdx, std::move(content), ready);
                    else if (IsSequenced(rel))
                        Sequenced(channels[channel], ordIdx, seqIdx, std::move(content), ready);
                    else
                        ready.push_back(std::move(content));
                }
            }
            for (auto& c : ready)
                HandleControl(std::move(c), delivered);
        }

        void Peer::HandleControl(std::vector<uint8_t> c, std::vector<std::vector<uint8_t>>& delivered) {
            if (c.empty())
                return;
            switch (c[0]) {
            case IdGame:
                if (!handshakeDone || closed)
                    return;
                c.erase(c.begin());
                delivered.push_back(std::move(c));
                return;
            case IdConnectionRequest: {
                if (c.size() < 17)
                    return;
                const uint64_t requestTime = GetBe64(c.data() + 9);
                std::vector<uint8_t> p{IdConnectionAccepted};
                PutAddr(p, remote);
                PutBe16(p, 0);
                for (int i = 0; i < 20; i++)
                    PutBlankAddr(p);
                PutBe64(p, requestTime);
                PutBe64(p, static_cast<uint64_t>(NowMs()));
                std::lock_guard lock(mutex);
                QueuePayloadLocked(p.data(), p.size(), ReliableOrdered);
                FlushLocked(NowMs());
                return;
            }
            case IdNewIncoming:
                if (handshakeDone)
                    return;
                handshakeDone = true;
                if (!openSent) {
                    openSent = true;
                    owner.Push(NetEvent::Type::Open, shared_from_this(), {});
                }
                return;
            case IdConnectedPing: {
                if (c.size() < 9)
                    return;
                std::vector<uint8_t> pong{IdConnectedPong};
                pong.insert(pong.end(), c.begin() + 1, c.begin() + 9);
                PutBe64(pong, static_cast<uint64_t>(NowMs()));
                std::lock_guard lock(mutex);
                QueuePayloadLocked(pong.data(), pong.size(), Unreliable);
                FlushLocked(NowMs());
                return;
            }
            case IdConnectedPong:
                return;
            case IdDetectLost: {
                std::vector<uint8_t> ping{IdConnectedPing};
                PutBe64(ping, static_cast<uint64_t>(NowMs()));
                std::lock_guard lock(mutex);
                QueuePayloadLocked(ping.data(), ping.size(), Unreliable);
                FlushLocked(NowMs());
                return;
            }
            case IdDisconnect:
                MarkClosed();
                lastRecv = 0;
                return;
            default:
                return;
            }
        }

        void Peer::Tick(int64_t now) {
            std::lock_guard lock(mutex);
            FlushRanges(acks, 0xC0);
            FlushRanges(naks, 0xA0);
            std::vector<uint32_t> due;
            for (auto& [seq, sent] : inflight)
                if (now - sent.at > rto)
                    due.push_back(seq);
            if (!due.empty()) {
                ResendLocked(due, now);
                rto = std::min(kMaxRto, rto + rto / 4);
            }
            if (handshakeDone && !closed && now - lastPing >= 5000) {
                lastPing = now;
                std::vector<uint8_t> ping{IdConnectedPing};
                PutBe64(ping, static_cast<uint64_t>(now));
                QueuePayloadLocked(ping.data(), ping.size(), Unreliable);
            }
            FlushLocked(now);
        }
    }

    std::unique_ptr<Listener> CreateRakNetListener(const ServerConfig& config, std::function<std::string()> motd) {
        return std::make_unique<RakNetListener>(config, std::move(motd));
    }
}
