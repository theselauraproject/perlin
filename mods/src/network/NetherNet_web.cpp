#include "network/NetherNet.hpp"

#include "network/Http.hpp"
#include "util/Json.hpp"

#include <emscripten.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include "util/Log.hpp"

extern "C" {
void perlin_nn_js_begin(const char* iceServers);
int perlin_nn_js_state();
int perlin_nn_js_offer(char* out, int capacity);
int perlin_nn_js_error(char* out, int capacity);
void perlin_nn_js_set_answer(const char* sdp);
void perlin_nn_js_send(uint8_t* data, int size, int reliable);
void perlin_nn_js_close();
}

namespace perlin {
    namespace {
        std::atomic<bool> g_secureSignaling{false};
        std::atomic<NetherNet*> g_active{nullptr};
        std::mutex g_iceMutex;
        std::string g_iceServers = "stun:stun.l.google.com:19302";

        enum JsState { kIdle = 0, kGathering = 1, kOfferReady = 2, kOpen = 3, kFailed = 4, kAnswered = 5 };

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

        std::string JsError() {
            std::string text(512, '\0');
            const int n = perlin_nn_js_error(text.data(), static_cast<int>(text.size()));
            text.resize(static_cast<size_t>(std::max(0, n)));
            return text;
        }

        bool WaitForState(int minimum, int timeoutMs) {
            const auto start = std::chrono::steady_clock::now();
            for (;;) {
                const int state = perlin_nn_js_state();
                if (state == kFailed)
                    return false;
                if (state >= minimum)
                    return true;
                if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs))
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        }
    }

    struct NetherNet::Impl {};

    void NetherNet::SetSecureSignaling(bool secure) {
        g_secureSignaling = secure;
    }

    bool NetherNet::SecureSignaling() {
        return g_secureSignaling;
    }

    void NetherNet::SetIceServers(const std::string& servers) {
        std::lock_guard lock(g_iceMutex);
        g_iceServers = servers;
    }

    NetherNet::NetherNet() = default;

    NetherNet::~NetherNet() {
        Disconnect();
    }

    bool NetherNet::Probe(const std::string& host, uint16_t port, NetherNetServerInfo& info,
                          std::string& error, int timeoutSecs) {
        http::Response r =
            http::Request(host, port, g_secureSignaling.load(), "GET", "/v1/join", "", "", {}, timeoutSecs);
        if (r.status == 0) {
            error =
                "could not reach the signaling endpoint from the browser (server down, blocked by CORS, "
                "or http:// from an https:// page - the server must answer /v1/join with "
                "Access-Control-Allow-Origin and be served over https unless it is localhost; a server on "
                "this "
                "machine or LAN also works when Perlin is served by the bundled serve.py, which relays it)";
            return false;
        }
        if (r.status < 200 || r.status >= 300) {
            error = "GET /v1/join -> HTTP " + std::to_string(r.status) + " " + r.body;
            return false;
        }
        auto parsed = json::Read<NetherNetServerInfo>(r.body);
        if (!parsed) {
            error = "GET /v1/join returned non-JSON: " + r.body;
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

    void NetherNet::OnWebMessage(bool reliable, const uint8_t* data, size_t size) {
        if (reliable)
            OnReliableMessage(data, size);
        else
            OnUnreliableMessage(data, size);
    }

    void NetherNet::OnWebClosed() {
        connected = false;
    }

    bool NetherNet::ConnectFriend(friendworld::Lease&, const ClientIdentity&) {
        lastError = "joining a friend's world isn't available in the browser";
        return false;
    }

    bool NetherNet::Connect(const std::string& host, uint16_t port, const ClientIdentity& identity) {
        Disconnect();
        lastError.clear();

        if (!Probe(host, port, serverInfo, lastError))
            return false;
        Log("nethernet", "probe ok: '{}' protocol {} version {}", serverInfo.name, serverInfo.protocol,
            serverInfo.version);

        std::random_device rd;
        std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) | rd());
        uint64_t netId = rng();
        if (netId == 0)
            netId = 1;

        impl = std::make_unique<Impl>();
        g_active = this;

        std::string ice;
        {
            std::lock_guard lock(g_iceMutex);
            ice = g_iceServers;
        }
        perlin_nn_js_begin(ice.c_str());
        Log("nethernet", "offer requested, gathering ICE...");
        if (!WaitForState(kOfferReady, 20000)) {
            lastError = "WebRTC offer failed: " + JsError();
            Disconnect();
            return false;
        }

        std::string offer(65536, '\0');
        const int offerSize = perlin_nn_js_offer(offer.data(), static_cast<int>(offer.size()));
        offer.resize(static_cast<size_t>(std::max(0, offerSize)));
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

        const std::string netIdStr = std::to_string(netId);
        Log("nethernet", "offer ready ({} bytes), POSTing to /v1/join/{}...", offer.size(), netIdStr);
        http::Response answer = http::Request(host, port, g_secureSignaling.load(), "POST",
                                              "/v1/join/" + netIdStr, offer, "application/sdp", {}, 15);
        Log("nethernet", "POST status {}, answer {} bytes", answer.status, answer.body.size());
        if (answer.status == 0) {
            lastError = "POST offer: request failed (CORS or connection)";
            Disconnect();
            return false;
        }
        if (answer.status < 200 || answer.status >= 300) {
            lastError = "POST /v1/join/" + netIdStr + " -> HTTP " + std::to_string(answer.status) + " " +
                        answer.body.substr(0, 300);
            Disconnect();
            return false;
        }
        if (answer.body.size() < 16 || !answer.body.starts_with("v=")) {
            lastError = "signaling refused the offer (session error " + answer.body.substr(0, 32) + ")";
            Disconnect();
            return false;
        }

        perlin_nn_js_set_answer(StripIdentityLines(answer.body).c_str());
        Log("nethernet", "remote description set, waiting for datachannel...");
        if (!WaitForState(kOpen, 25000)) {
            const std::string detail = JsError();
            lastError = "ReliableDataChannel never opened (ICE/DTLS failed" +
                        (detail.empty() ? std::string() : ": " + detail) + "; check server UDP reachability)";
            Disconnect();
            return false;
        }

        connected = true;
        Log("nethernet", "ReliableDataChannel open");
        return true;
    }

    void NetherNet::Disconnect() {
        connected = false;
        if (impl) {
            perlin_nn_js_close();
            g_active = nullptr;
            impl.reset();
        }
        std::vector<uint8_t> tmp;
        while (inbound.try_dequeue(tmp)) {}
        std::lock_guard<std::mutex> lk(reassemblyMutex);
        reassembly.clear();
    }

    static bool SendFramed(bool reliable, const std::vector<uint8_t>& data) {
        if (data.empty())
            return true;
        const size_t total = data.size();
        const size_t fragments = (total - 1) / NetherNet::kFragmentSize + 1;
        if (fragments > 256)
            return false;
        for (size_t i = 0; i < fragments; i++) {
            const size_t off = i * NetherNet::kFragmentSize;
            const size_t len = std::min(NetherNet::kFragmentSize, total - off);
            uint8_t* message = static_cast<uint8_t*>(std::malloc(len + 1));
            if (!message)
                return false;
            message[0] = static_cast<uint8_t>(fragments - 1 - i);
            std::memcpy(message + 1, data.data() + off, len);
            perlin_nn_js_send(message, static_cast<int>(len + 1), reliable ? 1 : 0);
        }
        return true;
    }

    bool NetherNet::SendReliable(const std::vector<uint8_t>& data) {
        if (!connected || !impl)
            return false;
        return SendFramed(true, data);
    }

    bool NetherNet::SendUnreliable(const std::vector<uint8_t>& data) {
        if (!connected || !impl)
            return false;
        if (data.size() > kFragmentSize)
            return false;
        return SendFramed(false, data);
    }

    bool NetherNet::TryReceive(std::vector<uint8_t>& out) {
        return inbound.try_dequeue(out);
    }
}

extern "C" {
EMSCRIPTEN_KEEPALIVE void perlin_nn_on_message(const uint8_t* data, int size, int reliable) {
    if (perlin::NetherNet* nn = perlin::g_active.load())
        nn->OnWebMessage(reliable != 0, data, static_cast<size_t>(size));
}

EMSCRIPTEN_KEEPALIVE void perlin_nn_on_closed() {
    if (perlin::NetherNet* nn = perlin::g_active.load())
        nn->OnWebClosed();
}
}
