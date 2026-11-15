#include "util/TaskGroup.hpp"
#include "util/Paths.hpp"
#include "network/NetherNet.hpp"
#include "network/NetherNetSignal.hpp"

#include "network/FriendWorld.hpp"
#include "network/Http.hpp"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <expected>
#include <functional>
#include <cstring>
#include <iostream>
#include "util/Json.hpp"
#include <random>
#include <thread>
#include "util/Log.hpp"
#include "util/Strings.hpp"

namespace perlin {

    namespace {
        std::atomic<bool> g_secureSignaling{false};

        std::pair<int, std::string> HttpRequest(const std::string& host, uint16_t port,
                                                const std::string& verb, const std::string& path,
                                                const std::string& body, const std::string& contentType,
                                                int timeoutSecs = 15) {
            http::Response r = http::Request(host, port, g_secureSignaling.load(), verb, path, body,
                                             contentType, {}, timeoutSecs);
            return {r.status, std::move(r.body)};
        }
    }

    struct NetherNet::Signals {
        std::atomic<bool> gatheringDone{false};
        std::atomic<bool> reliableOpen{false};
        std::atomic<bool> failed{false};
    };

    struct NetherNet::Impl {
        std::shared_ptr<Signals> signals = std::make_shared<Signals>();
        std::string offerSdp;
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> reliable;
        std::shared_ptr<rtc::DataChannel> unreliable;
    };

    void NetherNet::SetSecureSignaling(bool secure) {
        g_secureSignaling = secure;
    }

    bool NetherNet::SecureSignaling() {
        return g_secureSignaling;
    }

    void NetherNet::SetIceServers(const std::string&) {}

    NetherNet::NetherNet() = default;
    NetherNet::~NetherNet() {
        Disconnect();
        std::weak_ptr<int> alive = life;
        life.reset();
        for (int i = 0; i < 2000 && !alive.expired(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool NetherNet::Probe(const std::string& host, uint16_t port, NetherNetServerInfo& info,
                          std::string& error, int timeoutSecs) {
        auto [status, body] = HttpRequest(host, port, "GET", "/v1/join", "", "", timeoutSecs);
        if (status == 0) {
            error = "HTTP connect failed (is the server running with transport=nethernet?)";
            return false;
        }
        if (status < 200 || status >= 300) {
            error = "GET /v1/join -> HTTP " + std::to_string(status) + " " + body;
            return false;
        }
        auto parsed = json::Read<NetherNetServerInfo>(body);
        if (!parsed) {
            error = "GET /v1/join returned non-JSON: " + body;
            return false;
        }
        info = std::move(*parsed);
        return true;
    }

    void NetherNet::OnReliableMessage(const uint8_t* data, size_t size) {
        if (size == 0)
            return;
        std::lock_guard<std::mutex> lk(reassemblyMutex);
        reassembly.insert(reassembly.end(), data + 1, data + size);
        if (data[0] == 0) {
            inbound.enqueue(std::move(reassembly));
            reassembly.clear();
        }
    }

    void NetherNet::OnUnreliableMessage(const uint8_t* data, size_t size) {
        if (size == 0)
            return;
        inbound.enqueue(std::vector<uint8_t>(data + 1, data + size));
    }

    bool NetherNet::Connect(const std::string& host, uint16_t port, const ClientIdentity& identity) {
        Disconnect();
        lastError.clear();

        if (!Probe(host, port, serverInfo, lastError))
            return false;
        Log("nethernet", "probe ok: '{}' protocol {} version {}", serverInfo.name, serverInfo.protocol,
            serverInfo.version);
        serverInfo.advertisedProtocol = serverInfo.protocol;
        serverInfo.advertisedVersion = serverInfo.version;
        const bool older = serverInfo.protocol >= kOldestBedrockProtocol && serverInfo.protocol < kBedrockProtocol;
        const std::string joinVersion = older && !serverInfo.version.empty() ? serverInfo.version : kBedrockVersion;
        const int joinProtocol = older ? serverInfo.protocol : kBedrockProtocol;

        std::random_device rd;
        std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) | rd());
        uint64_t netId = rng();
        if (netId == 0)
            netId = 1;

        impl = std::make_unique<Impl>();
        try {

            rtc::Configuration config;
            config.iceServers.emplace_back("stun.l.google.com", uint16_t(19302));
            config.iceServers.emplace_back("stun.cloudflare.com", uint16_t(3478));
            impl->pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            lastError = std::string("PeerConnection create failed: ") + e.what();
            Disconnect();
            return false;
        }

        impl->pc->onGatheringStateChange(
            [sig = impl->signals, w = std::weak_ptr<int>(life)](rtc::PeerConnection::GatheringState state) {
                const auto keep = w.lock();
                if (keep && state == rtc::PeerConnection::GatheringState::Complete)
                    sig->gatheringDone = true;
            });
        impl->pc->onStateChange([sig = impl->signals,
                                 w = std::weak_ptr<int>(life)](rtc::PeerConnection::State state) {
            const auto keep = w.lock();
            if (!keep)
                return;
            if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)
                sig->failed = true;
        });

        try {
            impl->reliable = impl->pc->createDataChannel("ReliableDataChannel");

            rtc::DataChannelInit unrel;
            unrel.reliability.unordered = true;
            unrel.reliability.maxRetransmits = 0u;
            impl->unreliable = impl->pc->createDataChannel("UnreliableDataChannel", unrel);
        } catch (const std::exception& e) {
            lastError = std::string("DataChannel create failed: ") + e.what();
            Disconnect();
            return false;
        }

        impl->reliable->onOpen([sig = impl->signals, w = std::weak_ptr<int>(life)]() {
            if (const auto keep = w.lock())
                sig->reliableOpen = true;
        });
        impl->reliable->onClosed([sig = impl->signals, w = std::weak_ptr<int>(life)]() {
            if (const auto keep = w.lock())
                sig->failed = true;
        });
        impl->reliable->onMessage([this, w = std::weak_ptr<int>(life)](rtc::message_variant msg) {
            const auto keep = w.lock();
            if (!keep)
                return;
            if (auto* b = std::get_if<rtc::binary>(&msg))
                OnReliableMessage(reinterpret_cast<const uint8_t*>(b->data()), b->size());
        });
        impl->unreliable->onMessage([this, w = std::weak_ptr<int>(life)](rtc::message_variant msg) {
            const auto keep = w.lock();
            if (!keep)
                return;
            if (auto* b = std::get_if<rtc::binary>(&msg))
                OnUnreliableMessage(reinterpret_cast<const uint8_t*>(b->data()), b->size());
        });

        impl->pc->setLocalDescription();
        Log("nethernet", "offer requested, gathering ICE...");

        bool gathered = false;
        {
            auto start = std::chrono::steady_clock::now();
            while (!impl->signals->gatheringDone.load()) {
                if (impl->signals->failed.load())
                    break;
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(15))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            gathered = impl->signals->gatheringDone.load();
        }
        if (!gathered) {
            lastError = "ICE gathering did not complete (no usable host candidates?)";
            Disconnect();
            return false;
        }

        std::string offer;
        try {
            auto desc = impl->pc->localDescription();
            if (!desc) {
                lastError = "no local description after gathering";
                Disconnect();
                return false;
            }
            offer = std::string(*desc);
        } catch (const std::exception& e) {
            lastError = std::string("localDescription failed: ") + e.what();
            Disconnect();
            return false;
        }

        std::string offerClean;
        offerClean.reserve(offer.size());
        for (size_t i = 0; i < offer.size(); i++) {
            if (offer[i] == '\r' && i + 1 < offer.size() && offer[i + 1] == '\r')
                continue;
            offerClean.push_back(offer[i]);
        }
        offer.swap(offerClean);

        if (!identity.Valid() || identity.MultiplayerToken().empty()) {
            lastError = "no client identity for offer";
            Disconnect();
            return false;
        }
        std::string identified = identity.AssertOffer(offer);
        if (identified.empty()) {
            lastError = "failed to build offer identity assertion";
            Disconnect();
            return false;
        }
        offer.swap(identified);
        Log("nethernet", "offer identity added ({} bytes)", offer.size());

        std::string netIdStr = std::to_string(netId);
        HttpRequest(host, port, "GET",
                    "/v1/join?version=" + joinVersion + "&protocol=" + std::to_string(joinProtocol) +
                        "&platform=1&id=" + netIdStr,
                    "", "", 5);
        std::string wpath = "/v1/join/" + netIdStr;
        Log("nethernet", "offer ready ({} bytes), POSTing to /v1/join/{}...", offer.size(), netIdStr);
        auto [status, answer] = HttpRequest(host, port, "POST", wpath, offer, "application/sdp");
        Log("nethernet", "POST status {}, answer {} bytes", status, answer.size());
        if (status == 0) {
            lastError = "POST offer: HTTP connect failed";
            Disconnect();
            return false;
        }
        if (status < 200 || status >= 300) {
            lastError = "POST /v1/join/" + netIdStr + " -> HTTP " + std::to_string(status) + " " +
                        answer.substr(0, 300);
            Disconnect();
            return false;
        }
        if (!answer.empty()) {
            std::cout << "[nethernet] answer head: ";
            for (size_t i = 0; i < answer.size() && i < 120; i++) {
                unsigned char c = answer[i];
                if (c >= 32 && c < 127)
                    std::cout << static_cast<char>(c);
                else
                    std::cout << "\\x" << std::hex << static_cast<int>(c) << std::dec;
            }
            std::cout << std::endl;
        }
        if (answer.size() < 16 || !answer.starts_with("v=")) {
            lastError = "signaling refused the offer (session error " + answer.substr(0, 32) + ")";
            Disconnect();
            return false;
        }

        try {
            impl->pc->setRemoteDescription(
                rtc::Description(nethernet::StripIdentityLines(answer), rtc::Description::Type::Answer));
        } catch (const std::exception& e) {
            lastError = std::string("setRemoteDescription failed: ") + e.what();
            Disconnect();
            return false;
        }

        Log("nethernet", "remote description set, waiting for datachannel...");
        bool openFlag = false;
        {
            auto start = std::chrono::steady_clock::now();
            while (!impl->signals->reliableOpen.load()) {
                if (impl->signals->failed.load())
                    break;

                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(8))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            openFlag = impl->signals->reliableOpen.load();
        }
        if (!openFlag) {
            lastError = "ReliableDataChannel never opened (ICE/DTLS failed; "
                        "check server UDP reachability / firewall)";
            Disconnect();
            return false;
        }

        connected = true;
        Log("nethernet", "ReliableDataChannel open");
        return true;
    }

    namespace {
        bool MakeIceServer(const friendworld::IceServerInfo& info, const std::string& url,
                           rtc::IceServer& out) {
            const size_t colon = url.find(':');
            if (colon == std::string::npos)
                return false;
            const std::string scheme = str::Lower(url.substr(0, colon));
            std::string rest = url.substr(colon + 1);
            std::string transport = "udp";
            if (const size_t q = rest.find('?'); q != std::string::npos) {
                const std::string query = str::Lower(rest.substr(q + 1));
                if (query.contains("transport=tcp"))
                    transport = "tcp";
                rest.resize(q);
            }
            std::string host = rest;
            uint16_t port = scheme == "turns" || scheme == "stuns" ? 5349 : 3478;
            if (const size_t p = rest.rfind(':'); p != std::string::npos && !rest.contains(']')) {
                host = rest.substr(0, p);
                port = static_cast<uint16_t>(std::atoi(rest.c_str() + p + 1));
            }
            if (host.empty())
                return false;
            if (scheme == "stun" || scheme == "stuns") {
                out = rtc::IceServer(host, port);
            } else if (scheme == "turn" || scheme == "turns") {
                const auto relay = scheme == "turns"    ? rtc::IceServer::RelayType::TurnTls
                                   : transport == "tcp" ? rtc::IceServer::RelayType::TurnTcp
                                                        : rtc::IceServer::RelayType::TurnUdp;
                out = rtc::IceServer(host, port, info.username, info.password, relay);
            } else {
                return false;
            }
            return true;
        }
    }

    namespace {
        class HandlerScope {
        public:
            HandlerScope(friendworld::Signaling& s,
                         std::function<void(const std::string&, const std::string&)> fn)
                : signaling(&s) {
                signaling->SetHandler(std::move(fn));
            }
            ~HandlerScope() { Detach(); }
            HandlerScope(const HandlerScope&) = delete;
            HandlerScope& operator=(const HandlerScope&) = delete;

            void Detach() {
                if (signaling)
                    signaling->SetHandler(nullptr);
                signaling = nullptr;
            }

        private:
            friendworld::Signaling* signaling;
        };

        uint64_t NewConnectionId() {
            std::random_device rd;
            std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) | rd());
            const uint64_t id = rng();
            return id == 0 ? 1 : id;
        }

        template <class Pred> bool WaitUntil(std::chrono::steady_clock::duration limit, Pred done) {
            const auto start = std::chrono::steady_clock::now();
            while (!done() && std::chrono::steady_clock::now() - start < limit)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            return done();
        }
    }

    bool NetherNet::Fail(std::string why) {
        lastError = std::move(why);
        Disconnect();
        return false;
    }

    std::expected<void, std::string>
    NetherNet::BuildPeer(const std::vector<friendworld::IceServerInfo>& relays) {
        impl = std::make_unique<Impl>();
        try {
            rtc::Configuration config;
            for (const auto& relay : relays)
                for (const std::string& url : relay.urls) {
                    rtc::IceServer server("localhost", uint16_t(0));
                    if (MakeIceServer(relay, url, server))
                        config.iceServers.push_back(server);
                }
            Log("nethernet", "{} ICE servers", config.iceServers.size());
            impl->pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            return std::unexpected(std::string("PeerConnection create failed: ") + e.what());
        }
        return {};
    }

    std::shared_ptr<nethernet::CandidateLog> NetherNet::WirePeerCallbacks() {
        impl->pc->onGatheringStateChange(
            [sig = impl->signals, w = std::weak_ptr<int>(life)](rtc::PeerConnection::GatheringState state) {
                const auto keep = w.lock();
                if (keep && state == rtc::PeerConnection::GatheringState::Complete)
                    sig->gatheringDone = true;
            });
        auto candidates = std::make_shared<nethernet::CandidateLog>();
        impl->pc->onLocalCandidate([candidates](rtc::Candidate c) {
            std::string text = std::string(c);
            if (text.starts_with("a="))
                text.erase(0, 2);
            candidates->Add(std::move(text));
        });
        impl->pc->onIceStateChange([](rtc::PeerConnection::IceState state) {
            Log("nethernet",
                "ICE state {} (0 new, 1 checking, 2 connected, 3 completed, 4 failed, 5 disconnected, 6 "
                "closed)",
                static_cast<int>(state));
        });
        impl->pc->onStateChange([sig = impl->signals,
                                 w = std::weak_ptr<int>(life)](rtc::PeerConnection::State state) {
            const auto keep = w.lock();
            if (!keep)
                return;
            Log("nethernet",
                "peer state {} (0 new, 1 connecting, 2 connected, 3 disconnected, 4 failed, 5 closed)",
                static_cast<int>(state));
            if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)
                sig->failed = true;
        });
        return candidates;
    }

    std::expected<void, std::string> NetherNet::OpenChannels() {
        try {
            impl->reliable = impl->pc->createDataChannel("ReliableDataChannel");
            rtc::DataChannelInit unrel;
            unrel.reliability.unordered = true;
            unrel.reliability.maxRetransmits = 0u;
            impl->unreliable = impl->pc->createDataChannel("UnreliableDataChannel", unrel);
        } catch (const std::exception& e) {
            return std::unexpected(std::string("DataChannel create failed: ") + e.what());
        }
        impl->reliable->onOpen([sig = impl->signals, w = std::weak_ptr<int>(life)]() {
            if (const auto keep = w.lock())
                sig->reliableOpen = true;
        });
        impl->reliable->onClosed([sig = impl->signals, w = std::weak_ptr<int>(life)]() {
            if (const auto keep = w.lock())
                sig->failed = true;
        });
        impl->reliable->onMessage([this, w = std::weak_ptr<int>(life)](rtc::message_variant msg) {
            const auto keep = w.lock();
            if (!keep)
                return;
            if (auto* b = std::get_if<rtc::binary>(&msg))
                OnReliableMessage(reinterpret_cast<const uint8_t*>(b->data()), b->size());
        });
        impl->unreliable->onMessage([this, w = std::weak_ptr<int>(life)](rtc::message_variant msg) {
            const auto keep = w.lock();
            if (!keep)
                return;
            if (auto* b = std::get_if<rtc::binary>(&msg))
                OnUnreliableMessage(reinterpret_cast<const uint8_t*>(b->data()), b->size());
        });
        return {};
    }

    std::expected<std::string, std::string> NetherNet::BuildSignedOffer(const friendworld::Lease& lease,
                                                                        const ClientIdentity& identity) {
        impl->pc->setLocalDescription();
        if (!WaitUntil(std::chrono::seconds(15),
                       [&] { return impl->signals->gatheringDone.load() || impl->signals->failed.load(); }) ||
            !impl->signals->gatheringDone.load())
            return std::unexpected("ICE gathering did not complete (no usable candidates?)");
        std::string offer;
        try {
            auto desc = impl->pc->localDescription();
            if (!desc)
                return std::unexpected("no local description after gathering");
            offer = std::string(*desc);
        } catch (const std::exception& e) {
            return std::unexpected(std::string("localDescription failed: ") + e.what());
        }
        std::string identified =
            identity.AssertOffer(nethernet::CollapseDoubledCarriageReturns(std::move(offer)), lease.issuer);
        if (identified.empty())
            return std::unexpected("failed to build offer identity assertion");
        return identified;
    }

    std::expected<void, std::string> NetherNet::SendOffer(friendworld::Lease& lease,
                                                          const std::string& connectionId,
                                                          const std::string& signedOffer,
                                                          const nethernet::CandidateLog& candidates) {
        Log("nethernet", "sending the offer to {} (connection {})", lease.hostNetworkId, connectionId);
        std::string sendError;
        if (!lease.signaling->Send(lease.hostNetworkId, "CONNECTREQUEST " + connectionId + " " + signedOffer,
                                   true, sendError))
            return std::unexpected("could not reach the host: " + sendError);
        const std::vector<std::string> mine = candidates.Snapshot();
        Log("nethernet", "sending {} local candidates", mine.size());
        for (const std::string& c : mine) {
            std::string ignored;
            lease.signaling->Send(lease.hostNetworkId, "CANDIDATEADD " + connectionId + " " + c, false,
                                  ignored);
        }
        return {};
    }

    void NetherNet::AddRemoteCandidate(const std::string& text) {
        try {
            std::string mid = "0";
            if (auto remote = impl->pc->remoteDescription(); remote && remote->mediaCount() > 0)
                mid = std::visit([](auto* m) { return m->mid(); }, remote->media(0));
            Log("nethernet", "remote candidate: {}", text);
            impl->pc->addRemoteCandidate(rtc::Candidate(text, mid));
        } catch (const std::exception& e) {
            Log("nethernet", "bad remote candidate: {}", e.what());
        }
    }

    std::expected<void, std::string> NetherNet::AwaitChannelOpen(const friendworld::Lease& lease,
                                                                 nethernet::SignalInbox& inbox) {
        bool haveAnswer = false;
        std::vector<std::string> pendingCandidates;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!impl->signals->reliableOpen.load()) {
            if (impl->signals->failed.load() || std::chrono::steady_clock::now() > deadline)
                break;
            if (!lease.signaling->Alive() && !haveAnswer)
                break;
            const auto signal = inbox.Next(std::chrono::milliseconds(50));
            if (!signal)
                continue;
            if (signal->command == "CONNECTRESPONSE" && !haveAnswer) {
                try {
                    impl->pc->setRemoteDescription(rtc::Description(
                        nethernet::StripIdentityLines(signal->payload), rtc::Description::Type::Answer));
                    haveAnswer = true;
                    Log("nethernet", "answer received:\n{}", nethernet::StripIdentityLines(signal->payload));
                    for (const std::string& c : pendingCandidates)
                        AddRemoteCandidate(c);
                    pendingCandidates.clear();
                } catch (const std::exception& e) {
                    return std::unexpected(std::string("setRemoteDescription failed: ") + e.what());
                }
            } else if (signal->command == "CANDIDATEADD") {
                if (haveAnswer)
                    AddRemoteCandidate(signal->payload);
                else
                    pendingCandidates.push_back(signal->payload);
            } else if (signal->command == "CONNECTERROR") {
                return std::unexpected("the host refused the connection (error " + signal->payload + ")");
            }
        }
        if (!impl || !impl->signals->reliableOpen.load())
            return std::unexpected(haveAnswer ? "the connection to the host never opened (ICE/DTLS failed; "
                                                "the host's network may block peer-to-peer)"
                                              : "the host did not answer");
        return {};
    }

    std::expected<void, std::string> NetherNet::NegotiateWithHost(friendworld::Lease& lease,
                                                                  const ClientIdentity& identity,
                                                                  const nethernet::CandidateLog& candidates) {
        const std::string connectionId = std::to_string(NewConnectionId());
        auto inbox = std::make_shared<nethernet::SignalInbox>(lease.hostNetworkId, connectionId);
        HandlerScope handler(*lease.signaling, [inbox](const std::string& from, const std::string& text) {
            inbox->Receive(from, text);
        });
        const auto outcome = BuildSignedOffer(lease, identity)
                                 .and_then([&](const std::string& signedOffer) {
                                     return SendOffer(lease, connectionId, signedOffer, candidates);
                                 })
                                 .and_then([&] { return AwaitChannelOpen(lease, *inbox); });
        handler.Detach();
        return outcome;
    }

    bool NetherNet::ConnectFriend(friendworld::Lease& lease, const ClientIdentity& identity) {
        Disconnect();
        lastError.clear();
        if (!lease.signaling || !lease.signaling->Alive())
            return Fail("the signalling connection to Minecraft is closed");
        if (!identity.Valid() || identity.MultiplayerToken().empty())
            return Fail("no client identity for offer");
        serverInfo = {};
        serverInfo.name = lease.worldName.empty() ? lease.hostName : lease.worldName;
        serverInfo.protocol = lease.protocol > 0 ? lease.protocol : kBedrockProtocol;
        serverInfo.version = lease.version.empty() ? kBedrockVersion : lease.version;

        std::vector<friendworld::IceServerInfo> relays;
        std::string relayError;
        if (!lease.signaling->Credentials(relays, relayError))
            Log("nethernet", "no relay servers: {}", relayError);

        if (auto built = BuildPeer(relays); !built)
            return Fail(std::move(built.error()));
        const std::shared_ptr<nethernet::CandidateLog> candidates = WirePeerCallbacks();
        if (auto opened = OpenChannels(); !opened)
            return Fail(std::move(opened.error()));
        if (auto negotiated = NegotiateWithHost(lease, identity, *candidates); !negotiated)
            return Fail(std::move(negotiated.error()));
        connected = true;
        Log("nethernet", "ReliableDataChannel open");
        return true;
    }

    void NetherNet::Disconnect() {
        connected = false;
        if (impl) {
            try {
                if (impl->reliable) {
                    impl->reliable->onMessage(nullptr);
                    impl->reliable->onOpen(nullptr);
                    impl->reliable->onClosed(nullptr);
                }
                if (impl->unreliable)
                    impl->unreliable->onMessage(nullptr);
                if (impl->pc) {
                    impl->pc->onStateChange(nullptr);
                    impl->pc->onGatheringStateChange(nullptr);
                }
            } catch (const std::exception&) {}
            ProcessTasks().Run([owned = std::move(impl)]() mutable { owned.reset(); });
        }
        std::vector<uint8_t> tmp;
        while (inbound.try_dequeue(tmp)) {}
        std::lock_guard<std::mutex> lk(reassemblyMutex);
        reassembly.clear();
    }

    static bool SendFramed(const std::shared_ptr<rtc::DataChannel>& dc, const std::vector<uint8_t>& data) {
        if (!dc || !dc->isOpen())
            return false;
        if (data.empty())
            return true;
        size_t total = data.size();
        size_t fragments = (total - 1) / NetherNet::kFragmentSize + 1;
        if (fragments > 256)
            return false;
        try {
            for (size_t i = 0; i < fragments; i++) {
                size_t off = i * NetherNet::kFragmentSize;
                size_t len = std::min(NetherNet::kFragmentSize, total - off);
                rtc::binary msg;
                msg.reserve(len + 1);
                msg.push_back(static_cast<std::byte>(fragments - 1 - i));
                msg.insert(msg.end(), reinterpret_cast<const std::byte*>(data.data() + off),
                           reinterpret_cast<const std::byte*>(data.data() + off + len));
                if (!dc->send(std::move(msg)))
                    return false;
            }
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    bool NetherNet::SendReliable(const std::vector<uint8_t>& data) {
        if (!connected || !impl)
            return false;
        return SendFramed(impl->reliable, data);
    }

    bool NetherNet::SendUnreliable(const std::vector<uint8_t>& data) {
        if (!connected || !impl)
            return false;
        if (data.size() > kFragmentSize)
            return false;
        return SendFramed(impl->unreliable, data);
    }

    bool NetherNet::TryReceive(std::vector<uint8_t>& out) {
        return inbound.try_dequeue(out);
    }

}
