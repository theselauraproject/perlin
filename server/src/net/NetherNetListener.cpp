#include "core/Json.hpp"
#include "core/Log.hpp"
#include "core/Server.hpp"
#include "net/Connection.hpp"

#include <httplib.h>
#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace perlin::server {
    namespace {
        constexpr auto kAnswerTimeout = std::chrono::seconds(4);

        std::string StripIdentityLines(const std::string& sdp) {
            std::istringstream in(sdp);
            std::string line, out;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.starts_with("a=identity"))
                    continue;
                out += line + "\r\n";
            }
            return out;
        }

        class Peer final : public Connection, public std::enable_shared_from_this<Peer> {
        public:
            Peer(std::string remote, EventQueue& queue) : remote(std::move(remote)), queue(queue) {}

            bool Send(const uint8_t* data, size_t size, bool reliable) override {
                std::shared_ptr<rtc::DataChannel> dc =
                    reliable ? this->reliable : (unreliable ? unreliable : this->reliable);
                if (!dc || !dc->isOpen())
                    return false;
                if (!reliable && size > kFragmentSize)
                    dc = this->reliable;
                const auto parts = SplitFragments(data, size);
                if (parts.empty())
                    return size == 0;
                try {
                    for (const auto& part : parts) {
                        rtc::binary msg(reinterpret_cast<const std::byte*>(part.data()),
                                        reinterpret_cast<const std::byte*>(part.data()) + part.size());
                        if (!dc->send(std::move(msg)))
                            return false;
                    }
                } catch (...) {
                    return false;
                }
                return true;
            }

            void Close() override {
                if (closed.exchange(true))
                    return;
                try {
                    if (reliable)
                        reliable->close();
                    if (pc)
                        pc->close();
                } catch (...) {}
            }

            bool Alive() const override { return !closed && reliable && reliable->isOpen(); }
            std::string Describe() const override { return "nethernet " + remote; }

            void AttachChannel(std::shared_ptr<rtc::DataChannel> dc) {
                const std::string label = dc->label();
                std::weak_ptr<Peer> weak = shared_from_this();
                if (label == "ReliableDataChannel") {
                    reliable = dc;
                    dc->onOpen([weak] {
                        if (auto self = weak.lock(); self && !self->opened.exchange(true)) {
                            NetEvent e;
                            e.type = NetEvent::Type::Open;
                            e.connection = self;
                            self->queue.Push(std::move(e));
                        }
                    });
                    dc->onClosed([weak] {
                        if (auto self = weak.lock())
                            self->Disconnected();
                    });
                    dc->onMessage([weak](rtc::message_variant msg) {
                        auto self = weak.lock();
                        if (!self)
                            return;
                        if (auto* b = std::get_if<rtc::binary>(&msg)) {
                            std::vector<uint8_t> whole;
                            if (self->assembler.Push(reinterpret_cast<const uint8_t*>(b->data()), b->size(),
                                                     whole)) {
                                NetEvent e;
                                e.type = NetEvent::Type::Message;
                                e.connection = self;
                                e.data = std::move(whole);
                                self->queue.Push(std::move(e));
                            }
                        }
                    });
                    if (dc->isOpen() && !opened.exchange(true)) {
                        NetEvent e;
                        e.type = NetEvent::Type::Open;
                        e.connection = shared_from_this();
                        queue.Push(std::move(e));
                    }
                } else if (label == "UnreliableDataChannel") {
                    unreliable = dc;
                    dc->onMessage([weak](rtc::message_variant msg) {
                        auto self = weak.lock();
                        if (!self)
                            return;
                        if (auto* b = std::get_if<rtc::binary>(&msg); b && b->size() > 1) {
                            NetEvent e;
                            e.type = NetEvent::Type::Message;
                            e.connection = self;
                            e.data.assign(reinterpret_cast<const uint8_t*>(b->data()) + 1,
                                          reinterpret_cast<const uint8_t*>(b->data()) + b->size());
                            self->queue.Push(std::move(e));
                        }
                    });
                }
            }

            void Disconnected() {
                if (!opened) {
                    closed = true;
                    return;
                }
                if (!closed.exchange(true)) {
                    NetEvent e;
                    e.type = NetEvent::Type::Close;
                    e.connection = shared_from_this();
                    queue.Push(std::move(e));
                }
            }

            std::string remote;
            EventQueue& queue;
            std::shared_ptr<rtc::PeerConnection> pc;
            std::shared_ptr<rtc::DataChannel> reliable, unreliable;
            FragmentAssembler assembler;
            std::atomic<bool> closed{false};
            std::atomic<bool> opened{false};
            std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
            std::mutex answerMutex;
            std::condition_variable answerReady;
            bool gathered = false;
        };

        class NetherNetListener final : public Listener {
        public:
            NetherNetListener(ServerConfig config, std::function<std::string()> infoJson)
                : config(std::move(config)), infoJson(std::move(infoJson)) {}
            ~NetherNetListener() override { Stop(); }

            const char* Name() const override { return "nethernet"; }

            bool Start(EventQueue& q, std::string& error) override {
                queue = &q;
                rtc::InitLogger(std::getenv("PS_RTC_LOG") ? rtc::LogLevel::Debug : rtc::LogLevel::Error);

                http.new_task_queue = [] { return new httplib::ThreadPool(16, 64); };

                http.Get("/v1/join", [this](const httplib::Request&, httplib::Response& res) {
                    res.set_content(infoJson(), "application/json");
                });
                http.Post(R"(/v1/join/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
                    HandleOffer(req, res);
                });

                if (!http.bind_to_port(config.bindAddress, config.port)) {
                    error = "cannot listen on " + config.bindAddress + ":" + std::to_string(config.port) +
                            " (port in use?)";
                    return false;
                }
                running = true;
                httpThread = std::thread([this] { http.listen_after_bind(); });
                janitor = std::thread([this] {
                    while (running) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        Sweep();
                    }
                });
                return true;
            }

            void Stop() override {
                if (!running.exchange(false))
                    return;
                http.stop();
                if (httpThread.joinable())
                    httpThread.join();
                if (janitor.joinable())
                    janitor.join();
                std::vector<std::shared_ptr<Peer>> all;
                {
                    std::lock_guard lock(mutex);
                    for (auto& [id, p] : peers)
                        all.push_back(p);
                    peers.clear();
                }
                for (auto& p : all)
                    p->Close();
                all.clear();
            }

        private:
            void HandleOffer(const httplib::Request& req, httplib::Response& res) {
                if (req.body.size() < 16 || !req.body.starts_with("v=")) {
                    res.status = 400;
                    res.set_content("expected an sdp offer", "text/plain");
                    return;
                }
                {
                    std::lock_guard lock(mutex);
                    if (peers.size() >= static_cast<size_t>(config.maxPlayers) * 4 + 16) {
                        res.status = 503;
                        res.set_content("busy", "text/plain");
                        return;
                    }
                }
                const uint64_t netId = std::strtoull(req.matches[1].str().c_str(), nullptr, 10);
                auto peer =
                    std::make_shared<Peer>(req.remote_addr + ":" + std::to_string(req.remote_port), *queue);
                peer->offerSdp = req.body;
                try {
                    rtc::Configuration rc;
                    if (config.iceServers.empty()) {
                        rc.iceServers.emplace_back("stun.l.google.com", uint16_t(19302));
                    } else {
                        for (const std::string& s : config.iceServers)
                            if (s != "none")
                                rc.iceServers.emplace_back(s);
                    }
                    if (!config.iceBindAddress.empty())
                        rc.bindAddress = config.iceBindAddress;
                    if (config.udpPortsSet) {
                        if (config.udpPortBegin != 0) {
                            rc.portRangeBegin = config.udpPortBegin;
                            rc.portRangeEnd = config.udpPortEnd;
                            rc.enableIceUdpMux = config.udpPortBegin == config.udpPortEnd;
                            if (config.iceUdpPort && config.udpPortBegin == config.udpPortEnd) {
                                rc.portRangeBegin = config.iceUdpPort;
                                rc.portRangeEnd = config.iceUdpPort;
                            }
                        }
                    } else if (config.iceUdpMux) {
                        rc.enableIceUdpMux = true;
                        rc.portRangeBegin = config.iceUdpPort ? config.iceUdpPort : config.port;
                        rc.portRangeEnd = rc.portRangeBegin;
                    } else {
                        rc.portRangeBegin = config.port;
                        rc.portRangeEnd = static_cast<uint16_t>(std::min<int>(65535, config.port + 255));
                    }
                    peer->pc = std::make_shared<rtc::PeerConnection>(rc);

                    std::weak_ptr<Peer> weak = peer;
                    peer->pc->onGatheringStateChange([weak](rtc::PeerConnection::GatheringState state) {
                        if (state != rtc::PeerConnection::GatheringState::Complete)
                            return;
                        if (auto p = weak.lock()) {
                            std::lock_guard lock(p->answerMutex);
                            p->gathered = true;
                            p->answerReady.notify_all();
                        }
                    });
                    peer->pc->onStateChange([weak](rtc::PeerConnection::State state) {
                        if (state == rtc::PeerConnection::State::Failed ||
                            state == rtc::PeerConnection::State::Closed ||
                            state == rtc::PeerConnection::State::Disconnected)
                            if (auto p = weak.lock())
                                p->Disconnected();
                    });
                    peer->pc->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> dc) {
                        if (auto p = weak.lock())
                            p->AttachChannel(std::move(dc));
                    });
                    peer->pc->setRemoteDescription(
                        rtc::Description(StripIdentityLines(req.body), rtc::Description::Type::Offer));
                } catch (const std::exception& e) {
                    LogWarn(std::string("nethernet: bad offer from ") + req.remote_addr + ": " + e.what());
                    res.status = 400;
                    res.set_content("bad offer", "text/plain");
                    return;
                }

                {
                    std::unique_lock lock(peer->answerMutex);
                    peer->answerReady.wait_for(lock, kAnswerTimeout, [&] { return peer->gathered; });
                }
                std::string answer;
                try {
                    const auto desc = peer->pc->localDescription();
                    if (desc)
                        answer = std::string(*desc);
                } catch (...) {}
                if (answer.empty()) {
                    peer->Close();
                    res.status = 500;
                    res.set_content("no answer", "text/plain");
                    return;
                }
                {
                    std::lock_guard lock(mutex);
                    peers[netId] = peer;
                }
                res.set_content(answer, "application/sdp");
            }

            void Sweep() {
                std::vector<std::shared_ptr<Peer>> doomed;
                {
                    std::lock_guard lock(mutex);
                    const auto now = std::chrono::steady_clock::now();
                    for (auto it = peers.begin(); it != peers.end();) {
                        const bool stale =
                            !it->second->opened && now - it->second->created > std::chrono::seconds(30);
                        if (it->second->closed || stale) {
                            doomed.push_back(it->second);
                            it = peers.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
                for (auto& p : doomed)
                    p->Close();
            }

            ServerConfig config;
            std::function<std::string()> infoJson;
            EventQueue* queue = nullptr;
            httplib::Server http;
            std::thread httpThread, janitor;
            std::atomic<bool> running{false};
            std::mutex mutex;
            std::unordered_map<uint64_t, std::shared_ptr<Peer>> peers;
        };
    }

    std::unique_ptr<Listener> CreateNetherNetListener(const ServerConfig& config,
                                                      std::function<std::string()> infoJson) {
        return std::make_unique<NetherNetListener>(config, std::move(infoJson));
    }
}
