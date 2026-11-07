// the real transport: a webrtc client does what the Perlin client's direct join does (info probe, sdp offer over http,
// two data channels) against a live Server, then logs in
#include "test_util.hpp"
#include "test_auth.hpp"

#include "core/Json.hpp"
#include "core/Server.hpp"
#include "net/Batch.hpp"

#include "protocol/Packets.hpp"

#include <httplib.h>
#include <rtc/rtc.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>

using namespace perlin::server;
namespace proto = perlin::proto2225;
namespace net = perlin::net;

namespace {
    std::string B64Url(const std::string& s) {
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        std::string out;
        for (size_t i = 0; i < s.size(); i += 3) {
            uint32_t v = static_cast<unsigned char>(s[i]) << 16;
            if (i + 1 < s.size())
                v |= static_cast<unsigned char>(s[i + 1]) << 8;
            if (i + 2 < s.size())
                v |= static_cast<unsigned char>(s[i + 2]);
            out += t[(v >> 18) & 63];
            out += t[(v >> 12) & 63];
            if (i + 1 < s.size())
                out += t[(v >> 6) & 63];
            if (i + 2 < s.size())
                out += t[v & 63];
        }
        return out;
    }
    std::string Jwt(const std::string& payload) {
        return B64Url("{}") + "." + B64Url(payload) + ".sig";
    }

    template <class P> std::vector<uint8_t> Wire(BatchCodec& codec, const P& packet) {
        std::vector<uint8_t> body, content;
        net::EncodeChecked(packet, body);
        BatchCodec::AppendPacket(content, P::kId, body);
        return codec.Seal(content);
    }
}

namespace {
    struct Outcome {
        bool connected = false;
        bool loginSuccess = false;
        bool disconnected = false;
        int disconnectReason = -1;
        bool gotChunk = false;
    };

    // one webrtc client against a running server: probe, offer (optionally with an identity assertion), login, and if let in, a chunk
    Outcome JoinOver(uint16_t port, const std::string& identity, const std::string& clientToken,
                     const testauth::Key* assertWith, bool fullHandshake) {
        Outcome out;
        httplib::Client http("127.0.0.1", port);
        http.set_connection_timeout(3);
        http.set_read_timeout(15);
        auto info = http.Get("/v1/join");
        CHECK(info && info->status == 200);
        if (info) {
            json::Value v;
            CHECK(json::Parse(info->body, v));
            CHECK(v["protocol"].Int() == 2225 && v["version"].String() == "1.26.60");
        }

        rtc::Configuration rc;
        rc.bindAddress = "127.0.0.1";
        auto pc = std::make_shared<rtc::PeerConnection>(rc);
        std::atomic<bool> gathered{false}, open{false};
        std::mutex mutex;
        std::vector<std::vector<uint8_t>> messages;
        BatchCodec codec;
        pc->onGatheringStateChange([&](rtc::PeerConnection::GatheringState st) {
            if (st == rtc::PeerConnection::GatheringState::Complete)
                gathered = true;
        });
        auto reliable = pc->createDataChannel("ReliableDataChannel");
        rtc::DataChannelInit unrel;
        unrel.reliability.unordered = true;
        unrel.reliability.maxRetransmits = 0u;
        auto unreliable = pc->createDataChannel("UnreliableDataChannel", unrel);
        reliable->onOpen([&] { open = true; });
        reliable->onMessage([&](rtc::message_variant m) {
            if (auto* b = std::get_if<rtc::binary>(&m); b && b->size() > 1) {
                std::lock_guard lock(mutex);
                messages.emplace_back(reinterpret_cast<const uint8_t*>(b->data()) + 1,
                                      reinterpret_cast<const uint8_t*>(b->data()) + b->size());
            }
        });
        pc->setLocalDescription();
        for (int i = 0; i < 100 && !gathered; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(gathered);
        std::string offer = std::string(*pc->localDescription());
        if (assertWith) {
            offer = testauth::AssertOffer(offer, *assertWith);
        } else {
            // a bogus assertion line: the server must tolerate and ignore it when it is not asked to check
            const size_t m = offer.find("m=");
            offer.insert(offer.rfind('\n', m) + 1, "a=identity:e30=\r\n");
        }

        auto answer = http.Post("/v1/join/12345", offer, "application/sdp");
        CHECK(answer && answer->status == 200 && answer->body.starts_with("v="));
        if (!answer || answer->status != 200) {
            if (answer)
                std::fprintf(stderr, "answer status %d: %s\n", answer->status, answer->body.c_str());
            return out;
        }
        pc->setRemoteDescription(rtc::Description(answer->body, rtc::Description::Type::Answer));
        for (int i = 0; i < 200 && !open; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(open);
        out.connected = open;
        if (!open)
            return out;

        const auto send = [&](const std::vector<uint8_t>& wire) {
            for (const auto& p : SplitFragments(wire.data(), wire.size()))
                reliable->send(rtc::binary(reinterpret_cast<const std::byte*>(p.data()),
                                           reinterpret_cast<const std::byte*>(p.data()) + p.size()));
        };
        // packets already unpacked from batches, so two packets in one batch are both found
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> packets;
        const auto waitFor = [&](uint32_t id, std::vector<uint8_t>* bodyOut = nullptr, int tries = 100) {
            for (int i = 0; i < tries; i++) {
                {
                    std::lock_guard lock(mutex);
                    for (const auto& wire : messages) {
                        std::vector<uint8_t> content;
                        if (!codec.Open(wire, content))
                            continue;
                        BatchCodec::Split(content, [&](uint32_t pid, std::span<const uint8_t> body) {
                            packets.emplace_back(pid, std::vector<uint8_t>(body.begin(), body.end()));
                            if (pid == proto::NetworkSettingsPacket::kId)
                                codec.EnableCompression(256);
                        });
                    }
                    messages.clear();
                }
                for (auto it = packets.begin(); it != packets.end(); ++it) {
                    if (it->first == id) {
                        if (bodyOut)
                            *bodyOut = it->second;
                        packets.erase(it);
                        return true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return false;
        };

        proto::RequestNetworkSettingsPacket req;
        req.clientProtocol = 2225;
        send(Wire(codec, req));
        CHECK(waitFor(proto::NetworkSettingsPacket::kId));

        proto::LoginPacket login;
        login.protocolVersion = 2225;
        login.tokens.identity = identity;
        login.tokens.client = clientToken;
        send(Wire(codec, login));
        std::vector<uint8_t> body;
        // either the login goes through or the server says why not
        for (int i = 0; i < 100 && !out.loginSuccess && !out.disconnected; i++) {
            if (waitFor(proto::PlayStatusPacket::kId, &body, 1)) {
                proto::PlayStatusPacket st;
                net::ReadStream rs(body.data(), body.size());
                if (Read(rs, st) && st.status == proto::PlayStatusPacketStatus::LoginSuccess)
                    out.loginSuccess = true;
            }
            if (waitFor(proto::DisconnectPacket::kId, &body, 1)) {
                proto::DisconnectPacket d;
                net::ReadStream rs(body.data(), body.size());
                if (Read(rs, d)) {
                    out.disconnected = true;
                    out.disconnectReason = static_cast<int>(d.reason);
                }
            }
        }
        if (out.loginSuccess && fullHandshake) {
            CHECK(waitFor(proto::ResourcePacksInfoPacket::kId));
            proto::ResourcePackClientResponsePacket have;
            have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
            send(Wire(codec, have));
            CHECK(waitFor(proto::ResourcePackStackPacket::kId));
            proto::ResourcePackClientResponsePacket done;
            done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
            send(Wire(codec, done));
            CHECK(waitFor(proto::StartGamePacket::kId));
            proto::RequestChunkRadiusPacket radius;
            radius.chunkRadius = 2;
            radius.maxRadius = 2;
            send(Wire(codec, radius));
            out.gotChunk = waitFor(proto::LevelChunkPacket::kId);
            CHECK(out.gotChunk);
        }
        pc->close();
        return out;
    }
}

int main() {
    constexpr uint16_t kPort = 19333;
    ServerConfig config;
    config.port = kPort;
    config.bindAddress = "127.0.0.1";
    config.generator = "flat";
    config.terrainBackend = "cpu";
    config.iceServers = {"none"};
    config.iceBindAddress = "127.0.0.1";
    config.iceUdpMux = std::getenv("PS_TEST_MUX") != nullptr;
    config.viewDistance = 4;
    Server open(config);
    std::string error;
    if (!open.Start(error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }
    std::thread ticker([&] { open.Run(); });

    // an open server: an offline login gets all the way to a chunk
    {
        testauth::Account guest("Webrtc", "");
        const Outcome o =
            JoinOver(kPort, guest.Identity(guest.Chain(false), 2), guest.ClientToken(), nullptr, true);
        CHECK(o.connected && o.loginSuccess && o.gotChunk);
        // a forged login is turned away even on an open server
        const Outcome forged =
            JoinOver(kPort, guest.Identity({"a.b.c"}, 2), guest.ClientToken(), nullptr, false);
        CHECK(forged.connected && !forged.loginSuccess);
    }
    open.RequestStop();
    ticker.join();
    open.Stop();

    // xbox_auth: only a chain rooted in a trusted key whose owner also signed this connection's offer gets in
    testauth::Account xbox("XboxPlayer", "2535412345678");
    ServerConfig strict = config;
    strict.port = kPort + 1;
    strict.xboxAuth = true;
    strict.authRootKeys = {xbox.root.spki};
    Server locked(strict);
    CHECK(locked.Start(error));
    std::thread ticker2([&] { locked.Run(); });
    {
        const Outcome good =
            JoinOver(strict.port, xbox.Identity(xbox.Chain()), xbox.ClientToken(), &xbox.player, true);
        CHECK(good.loginSuccess && good.gotChunk);
        const Outcome offline = JoinOver(strict.port, xbox.Identity(xbox.Chain(false), 2), xbox.ClientToken(),
                                         &xbox.player, false);
        CHECK(!offline.loginSuccess && offline.disconnected && offline.disconnectReason == 46);
        // a replayed login: right chain, but the offer is not signed by the account's key
        testauth::Key replayer;
        const Outcome replay =
            JoinOver(strict.port, xbox.Identity(xbox.Chain()), xbox.ClientToken(), &replayer, false);
        CHECK(!replay.loginSuccess && replay.disconnected && replay.disconnectReason == 46);
        const Outcome noAssertion =
            JoinOver(strict.port, xbox.Identity(xbox.Chain()), xbox.ClientToken(), nullptr, false);
        CHECK(!noAssertion.loginSuccess && noAssertion.disconnected && noAssertion.disconnectReason == 46);
    }
    locked.RequestStop();
    ticker2.join();
    locked.Stop();
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_nethernet_test ok");
    return 0;
}
