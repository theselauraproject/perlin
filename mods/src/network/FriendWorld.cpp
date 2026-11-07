#include "network/FriendWorld.hpp"

#include "network/XboxAuth.hpp"
#include "util/Log.hpp"

#ifdef __EMSCRIPTEN__

namespace perlin::friendworld {
    bool Join(xbox::Account&, const std::string&, const std::string&, std::shared_ptr<Lease>&,
              const ProgressFn&, std::string& error) {
        error = "joining a friend's world isn't available in the browser";
        return false;
    }
    bool JoinRealm(xbox::Account&, const std::string&, const std::string&, const std::string&,
                   const std::string&, std::shared_ptr<Lease>&, std::string& error) {
        error = "this Realm needs NetherNet signalling, which isn't available in the browser";
        return false;
    }
}

#else

#include "network/Http.hpp"
#include "util/Json.hpp"
#include "util/Strings.hpp"

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <thread>

namespace perlin::friendworld {

    namespace {
        constexpr const char* kScid = "4fc10100-5f7a-4470-899b-280835760c07";
        constexpr int kTransportNetherNet = 2;
        constexpr int kConnectionWebSocket = 3;
        constexpr int kConnectionJsonRpc = 7;

        std::mt19937_64& Rng() {
            static std::random_device device;
            static std::mt19937_64 rng((static_cast<uint64_t>(device()) << 32) | device());
            return rng;
        }

        std::string NewUuid() {
            uint8_t raw[16];
            for (int i = 0; i < 16; i += 8) {
                const uint64_t v = Rng()();
                for (int j = 0; j < 8; j++)
                    raw[i + j] = static_cast<uint8_t>(v >> (8 * j));
            }
            raw[6] = static_cast<uint8_t>((raw[6] & 0x0F) | 0x40);
            raw[8] = static_cast<uint8_t>((raw[8] & 0x3F) | 0x80);
            static const char* kHex = "0123456789abcdef";
            std::string out;
            for (int i = 0; i < 16; i++) {
                if (i == 4 || i == 6 || i == 8 || i == 10)
                    out += '-';
                out += kHex[raw[i] >> 4];
                out += kHex[raw[i] & 15];
            }
            return out;
        }

        std::string Upper(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return s;
        }

        std::string Q(const std::string& s) {
            std::string out = "\"";
            for (const char c : s) {
                switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    } else {
                        out += c;
                    }
                }
            }
            return out + "\"";
        }

        std::string QuoteBigIds(const std::string& body) {
            static const char* const kKeys[] = {"\"NetherNetId\"", "\"RakNetGUID\""};
            std::string out = body;
            for (const char* key : kKeys) {
                size_t at = 0;
                while ((at = out.find(key, at)) != std::string::npos) {
                    size_t i = at + std::strlen(key);
                    while (i < out.size() && (out[i] == ' ' || out[i] == ':'))
                        i++;
                    size_t j = i;
                    while (j < out.size() && std::isdigit(static_cast<unsigned char>(out[j])))
                        j++;
                    if (j > i) {
                        out.insert(j, "\"");
                        out.insert(i, "\"");
                        j += 2;
                    }
                    at = j;
                }
            }
            return out;
        }

        class Link {
        public:
            ~Link() { Close(); }

            bool Open(const std::string& url, const httplib::Headers& headers, std::string& error) {
                ws = std::make_unique<httplib::ws::WebSocketClient>(url, headers);
                if (!ws->is_valid()) {
                    error = "bad WebSocket address " + url;
                    return false;
                }
                http::UseSystemCa(*ws);
                ws->set_connection_timeout(15);
                const httplib::ws::Result result = ws->connect();
                if (!result) {
                    error = "could not open a WebSocket to " + url.substr(0, url.find('/', 8)) +
                            (result.status() > 0 ? " (HTTP " + std::to_string(result.status()) + ")"
                                                 : std::string());
                    ws.reset();
                    return false;
                }
                ws->set_read_timeout(1);
                open = true;
                reader = std::thread([this] {
                    std::string message;
                    while (!stop) {
                        const httplib::ws::ReadResult r = ws->read(message);
                        if (r == httplib::ws::Timeout)
                            continue;
                        if (r == httplib::ws::Text || r == httplib::ws::Binary) {
                            if (onText)
                                onText(message);
                            continue;
                        }
                        break;
                    }
                    open = false;
                    if (!stop && onClosed)
                        onClosed();
                });
                return true;
            }

            bool Send(const std::string& text) {
                std::lock_guard lock(sendMutex);
                return ws && open && ws->send(text);
            }

            void Close() {
                stop = true;
                if (ws) {
                    try {
                        ws->close();
                    } catch (...) {}
                }
                if (reader.joinable() && std::this_thread::get_id() != reader.get_id())
                    reader.join();
                else if (reader.joinable())
                    reader.detach();
                open = false;
            }

            std::function<void(const std::string&)> onText;
            std::function<void()> onClosed;
            std::atomic<bool> open{false};

        private:
            std::unique_ptr<httplib::ws::WebSocketClient> ws;
            std::thread reader;
            std::atomic<bool> stop{false};
            std::mutex sendMutex;
        };

        struct Waiter {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::string error;
        };

        class Waiters {
        public:
            std::shared_ptr<Waiter> Add(const std::string& id) {
                auto w = std::make_shared<Waiter>();
                std::lock_guard lock(m);
                map[id] = w;
                return w;
            }
            void Remove(const std::string& id) {
                std::lock_guard lock(m);
                map.erase(id);
            }
            void Complete(const std::string& id, const std::string& error) {
                std::shared_ptr<Waiter> w;
                {
                    std::lock_guard lock(m);
                    const auto it = map.find(id);
                    if (it == map.end())
                        return;
                    w = it->second;
                }
                std::lock_guard lock(w->m);
                w->done = true;
                w->error = error;
                w->cv.notify_all();
            }
            void FailAll(const std::string& error) {
                std::lock_guard lock(m);
                for (auto& [id, w] : map) {
                    std::lock_guard inner(w->m);
                    w->done = true;
                    w->error = error;
                    w->cv.notify_all();
                }
            }
            static bool Wait(const std::shared_ptr<Waiter>& w, int ms, std::string& error,
                             bool timeoutIsError) {
                std::unique_lock lock(w->m);
                if (!w->cv.wait_for(lock, std::chrono::milliseconds(ms), [&] { return w->done; }))
                    return !timeoutIsError;
                error = w->error;
                return w->error.empty();
            }

        private:
            std::mutex m;
            std::map<std::string, std::shared_ptr<Waiter>> map;
        };

        bool ParseCredentials(const json::View servers, int& expiry, std::vector<IceServerInfo>& out) {
            expiry = servers["ExpirationInSeconds"].integer();
            out.clear();
            for (const json::View s : servers["TurnAuthServers"].items()) {
                IceServerInfo info;
                info.username = s["Username"].str();
                info.password = s["Password"].str();
                for (const json::View u : s["Urls"].items())
                    info.urls.push_back(u.str());
                if (!info.urls.empty())
                    out.push_back(std::move(info));
            }
            return true;
        }

        class WebSocketSignaling : public Signaling {
        public:
            ~WebSocketSignaling() override { Close(); }

            bool Open(const std::string& baseUri, const std::string& authorization, std::string& error) {
                networkId = std::to_string(Rng()() | 1);
                link.onText = [this](const std::string& text) { OnText(text); };
                link.onClosed = [this] {
                    waiters.FailAll("the signalling connection closed");
                    cv.notify_all();
                };
                std::string base = baseUri;
                while (!base.empty() && base.back() == '/')
                    base.pop_back();
                if (!link.Open(base + "/ws/v1.0/signaling/" + networkId, {{"Authorization", authorization}},
                               error))
                    return false;
                pinger = std::thread([this] {
                    std::unique_lock lock(pingMutex);
                    while (!closing) {
                        if (pingCv.wait_for(lock, std::chrono::seconds(15),
                                            [this] { return closing.load(); }))
                            break;
                        lock.unlock();
                        link.Send("{\"Type\":0}");
                        lock.lock();
                    }
                });
                return true;
            }

            bool Send(const std::string& to, const std::string& text, bool waitDelivery,
                      std::string& error) override {
                const std::string id = NewUuid();
                std::shared_ptr<Waiter> waiter;
                if (waitDelivery)
                    waiter = waiters.Add(id);
                const std::string message = "{\"Type\":1,\"To\":" + Q(to) + ",\"Message\":" + Q(text) +
                                            ",\"MessageId\":" + Q(id) + "}";
                if (!link.Send(message)) {
                    waiters.Remove(id);
                    error = "the signalling connection is closed";
                    return false;
                }
                if (!waiter)
                    return true;
                const bool ok = Waiters::Wait(waiter, 15000, error, false);
                waiters.Remove(id);
                return ok;
            }

            void SetHandler(std::function<void(const std::string&, const std::string&)> h) override {
                std::lock_guard lock(handlerMutex);
                handler = std::move(h);
            }

            bool Credentials(std::vector<IceServerInfo>& servers, std::string& error) override {
                std::unique_lock lock(credMutex);
                if (!cv.wait_for(lock, std::chrono::seconds(15),
                                 [this] { return haveCredentials || !link.open; })) {
                    error = "the signalling service sent no relay servers";
                    return false;
                }
                if (!haveCredentials) {
                    error = "the signalling connection closed";
                    return false;
                }
                servers = credentials;
                return true;
            }

            bool Alive() const override { return link.open; }

            void Close() override {
                {
                    std::lock_guard lock(pingMutex);
                    closing = true;
                }
                pingCv.notify_all();
                link.Close();
                if (pinger.joinable())
                    pinger.join();
                waiters.FailAll("closed");
            }

        private:
            void OnText(const std::string& text) {
                auto doc = json::Document::Parse(text);
                if (!doc)
                    return;
                const json::View root = doc->root();
                const int type = root["Type"].integer(-1);
                const std::string from = root["From"].str();
                const std::string message = root["Message"].str();
                const std::string id = root["MessageId"].str();
                switch (type) {
                case 2: {
                    if (from != "Server")
                        return;
                    auto creds = json::Document::Parse(message);
                    if (!creds)
                        return;
                    int expiry = 0;
                    std::vector<IceServerInfo> parsed;
                    ParseCredentials(creds->root(), expiry, parsed);
                    std::lock_guard lock(credMutex);
                    credentials = std::move(parsed);
                    haveCredentials = true;
                    cv.notify_all();
                    break;
                }
                case 1: {
                    std::function<void(const std::string&, const std::string&)> h;
                    {
                        std::lock_guard lock(handlerMutex);
                        h = handler;
                    }
                    if (h)
                        h(from, message);
                    break;
                }
                case 0: {
                    std::string why = "the signalling service refused the message";
                    if (auto err = json::Document::Parse(message)) {
                        const int code = err->root()["Code"].integer();
                        why = code == 1   ? "the host isn't online"
                              : code == 2 ? "the message could not be delivered"
                                          : why;
                        if (err->root()["Message"].isString() && !err->root()["Message"].str().empty())
                            why += " (" + err->root()["Message"].str() + ")";
                    }
                    if (!id.empty())
                        waiters.Complete(id, why);
                    break;
                }
                case 4:
                    if (!id.empty())
                        waiters.Complete(id, {});
                    break;
                default:
                    break;
                }
            }

            Link link;
            Waiters waiters;
            std::string networkId;
            std::mutex handlerMutex;
            std::function<void(const std::string&, const std::string&)> handler;
            std::mutex credMutex;
            std::condition_variable cv;
            bool haveCredentials = false;
            std::vector<IceServerInfo> credentials;
            std::thread pinger;
            std::mutex pingMutex;
            std::condition_variable pingCv;
            std::atomic<bool> closing{false};
        };

        class JsonRpcSignaling : public Signaling {
        public:
            ~JsonRpcSignaling() override { Close(); }

            bool Open(const std::string& baseUri, const std::string& authorization, std::string& error) {
                networkId = std::to_string(Rng()() | 1);
                link.onText = [this](const std::string& text) { OnText(text); };
                link.onClosed = [this] { waiters.FailAll("the signalling connection closed"); };
                std::string base = baseUri;
                while (!base.empty() && base.back() == '/')
                    base.pop_back();
                if (!link.Open(base + "/ws/v1.0/messaging/connect", {{"Authorization", authorization}},
                               error))
                    return false;
                pinger = std::thread([this] {
                    std::unique_lock lock(pingMutex);
                    while (!closing) {
                        if (pingCv.wait_for(lock, std::chrono::seconds(15),
                                            [this] { return closing.load(); }))
                            break;
                        lock.unlock();
                        std::string unused;
                        Call("System_Ping_v1_0", "{}", 5000, unused);
                        lock.lock();
                    }
                });
                return true;
            }

            bool Send(const std::string& to, const std::string& text, bool waitDelivery,
                      std::string& error) override {
                const std::string id = NewUuid();
                std::shared_ptr<Waiter> waiter;
                if (waitDelivery)
                    waiter = waiters.Add(id);
                const std::string inner = "{\"params\":{\"netherNetId\":" + Q(networkId) +
                                          ",\"message\":" + Q(text) +
                                          "},\"jsonrpc\":\"2.0\",\"method\":\"Signaling_WebRtc_v1_0\"}";
                std::string reply;
                if (!Call("Signaling_SendClientMessage_v1_0",
                          "{\"toPlayerId\":" + Q(to) + ",\"messageId\":" + Q(id) +
                              ",\"message\":" + Q(inner) + "}",
                          10000, reply)) {
                    waiters.Remove(id);
                    error = reply.empty() ? "the signalling service did not answer" : reply;
                    return false;
                }
                if (!waiter)
                    return true;
                const bool ok = Waiters::Wait(waiter, 15000, error, false);
                waiters.Remove(id);
                return ok;
            }

            void SetHandler(std::function<void(const std::string&, const std::string&)> h) override {
                std::lock_guard lock(handlerMutex);
                handler = std::move(h);
            }

            bool Credentials(std::vector<IceServerInfo>& servers, std::string& error) override {
                std::string reply;
                if (!Call("Signaling_TurnAuth_v1_0", "{}", 15000, reply)) {
                    error = reply.empty() ? "the signalling service sent no relay servers" : reply;
                    return false;
                }
                auto doc = json::Document::Parse(reply);
                if (!doc) {
                    error = "unreadable relay server list";
                    return false;
                }
                int expiry = 0;
                ParseCredentials(doc->root()["result"], expiry, servers);
                return true;
            }

            bool Alive() const override { return link.open; }

            void Close() override {
                {
                    std::lock_guard lock(pingMutex);
                    closing = true;
                }
                pingCv.notify_all();
                link.Close();
                if (pinger.joinable())
                    pinger.join();
                waiters.FailAll("closed");
                FailCalls("closed");
            }

        private:
            struct Pending {
                std::mutex m;
                std::condition_variable cv;
                bool done = false;
                std::string text;
            };

            bool Call(const std::string& method, const std::string& params, int timeoutMs,
                      std::string& reply) {
                const int id = ++nextId;
                auto pending = std::make_shared<Pending>();
                {
                    std::lock_guard lock(callMutex);
                    calls[id] = pending;
                }
                const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                                            ",\"method\":" + Q(method) + ",\"params\":" + params + "}";
                bool ok = link.Send(request);
                if (ok) {
                    std::unique_lock lock(pending->m);
                    ok = pending->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                              [&] { return pending->done; });
                    reply = ok ? pending->text : std::string("timed out");
                } else {
                    reply = "the signalling connection is closed";
                }
                {
                    std::lock_guard lock(callMutex);
                    calls.erase(id);
                }
                if (ok) {
                    if (auto doc = json::Document::Parse(reply))
                        if (doc->root()["error"].isObject()) {
                            reply = doc->root()["error"]["message"].str(
                                "the signalling service refused the request");
                            return false;
                        }
                }
                return ok;
            }

            void FailCalls(const std::string& why) {
                std::lock_guard lock(callMutex);
                for (auto& [id, p] : calls) {
                    std::lock_guard inner(p->m);
                    p->done = true;
                    p->text = "{\"error\":{\"message\":" + Q(why) + "}}";
                    p->cv.notify_all();
                }
            }

            void Reply(const json::View id) {
                if (!id.valid())
                    return;
                const std::string raw = id.isString() ? Q(id.str()) : id.text();
                link.Send("{\"jsonrpc\":\"2.0\",\"id\":" + raw + ",\"result\":null}");
            }

            void OnText(const std::string& text) {
                auto doc = json::Document::Parse(text);
                if (!doc)
                    return;
                const json::View root = doc->root();
                if (!root["method"].isString()) {
                    if (!root["id"].isNumber())
                        return;
                    std::shared_ptr<Pending> pending;
                    {
                        std::lock_guard lock(callMutex);
                        const auto it = calls.find(root["id"].integer());
                        if (it == calls.end())
                            return;
                        pending = it->second;
                    }
                    std::lock_guard lock(pending->m);
                    pending->done = true;
                    pending->text = text;
                    pending->cv.notify_all();
                    return;
                }
                const std::string method = root["method"].str();
                if (method == "Signaling_ReceiveMessage_v1_0") {
                    const json::View params = root["params"];
                    if (params.isArray()) {
                        for (const json::View envelope : params.items())
                            HandleEnvelope(envelope);
                    } else {
                        HandleEnvelope(params);
                    }
                } else if (method == "Signaling_ReceiveError_v1_0") {
                    const json::View params = root["params"];
                    const std::string id = params["messageId"].str();
                    if (!id.empty()) {
                        const int code = params["code"].integer();
                        std::string why =
                            code == 1 ? "the host isn't online" : "the message could not be delivered";
                        if (params["message"].isString() && !params["message"].str().empty())
                            why += " (" + params["message"].str() + ")";
                        waiters.Complete(id, why);
                    }
                }
                Reply(root["id"]);
            }

            void HandleEnvelope(const json::View envelope) {
                if (!envelope.isObject())
                    return;
                const std::string from = envelope["From"].str();
                const std::string id = envelope["Id"].str();
                auto inner = json::Document::Parse(envelope["Message"].str());
                if (!inner)
                    return;
                const std::string method = str::Lower(inner->root()["method"].str());
                if (method == "signaling_deliverynotification_v1_0") {
                    waiters.Complete(inner->root()["params"]["messageId"].str(), {});
                } else if (method == "signaling_webrtc_v1_0") {
                    std::function<void(const std::string&, const std::string&)> h;
                    {
                        std::lock_guard lock(handlerMutex);
                        h = handler;
                    }
                    if (h)
                        h(from, inner->root()["params"]["message"].str());
                    const std::string ack =
                        "{\"params\":{\"messageId\":" + Q(id) +
                        "},\"jsonrpc\":\"2.0\",\"method\":\"Signaling_DeliveryNotification_V1_0\"}";
                    const int callId = ++nextId;
                    link.Send("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(callId) +
                              ",\"method\":\"Signaling_SendClientMessage_v1_0\",\"params\":{\"toPlayerId\":" +
                              Q(from) + ",\"messageId\":" + Q(NewUuid()) + ",\"message\":" + Q(ack) + "}}");
                } else if (inner->root()["Code"].isNumber() && !id.empty()) {
                    waiters.Complete(id, inner->root()["Message"].str("the message could not be delivered"));
                }
            }

            Link link;
            Waiters waiters;
            std::string networkId;
            std::mutex handlerMutex;
            std::function<void(const std::string&, const std::string&)> handler;
            std::atomic<int> nextId{0};
            std::mutex callMutex;
            std::map<int, std::shared_ptr<Pending>> calls;
            std::thread pinger;
            std::mutex pingMutex;
            std::condition_variable pingCv;
            std::atomic<bool> closing{false};
        };

        class Rta {
        public:
            ~Rta() { Close(); }

            bool Open(const std::string& authorization, std::string& error) {
                link.onText = [this](const std::string& text) { OnText(text); };
                if (!link.Open(
                        "wss://rta.xboxlive.com/connect",
                        {{"Authorization", authorization}, {"Sec-WebSocket-Protocol", "rta.xboxlive.com.V2"}},
                        error))
                    return false;
                link.Send("[1,1,\"https://sessiondirectory.xboxlive.com/connections/\"]");
                std::unique_lock lock(m);
                if (!cv.wait_for(lock, std::chrono::seconds(15), [this] { return answered; })) {
                    error = "Xbox Live's real-time service did not answer";
                    return false;
                }
                if (status != 0 || connectionId.empty()) {
                    error = "Xbox Live's real-time service refused the subscription (status " +
                            std::to_string(status) + ")";
                    return false;
                }
                return true;
            }

            void Close() { link.Close(); }

            std::string connectionId;
            std::atomic<uint64_t> changes{0};

        private:
            void OnText(const std::string& text) {
                auto doc = json::Document::Parse(text);
                if (!doc || !doc->root().isArray())
                    return;
                const json::View root = doc->root();
                const int type = root[0].integer();
                if (type == 1) {
                    std::lock_guard lock(m);
                    status = root[2].integer(-1);
                    connectionId = root[4]["ConnectionId"].str();
                    answered = true;
                    cv.notify_all();
                } else if (type == 3 || type == 4) {
                    changes++;
                }
            }

            Link link;
            std::mutex m;
            std::condition_variable cv;
            bool answered = false;
            int status = -1;
        };

        http::Response Mpsd(const char* method, const std::string& path, const std::string& body,
                            const std::string& authorization,
                            const std::map<std::string, std::string>& extra = {}) {
            std::map<std::string, std::string> headers{{"Authorization", authorization},
                                                       {"x-xbl-contract-version", "107"},
                                                       {"Accept", "application/json"}};
            for (const auto& [k, v] : extra)
                headers[k] = v;
            return http::Request("sessiondirectory.xboxlive.com", 443, true, method, path, body,
                                 "application/json", headers, 20);
        }

        std::string Failure(const http::Response& r, const char* what) {
            if (r.status == 0)
                return std::string("could not reach ") + what;
            std::string text = std::string(what) + " answered HTTP " + std::to_string(r.status);
            if (auto doc = json::Document::Parse(r.body)) {
                for (const char* key : {"description", "errorMsg", "message"})
                    if (doc->root()[key].isString() && !doc->root()[key].str().empty()) {
                        text += ": " + doc->root()[key].str();
                        break;
                    }
            }
            return text;
        }

        struct Connection {
            int type = 0;
            std::string hostNetworkId;
        };

        bool PickConnection(const json::View custom, Connection& out) {
            bool found = false;
            for (const json::View c : custom["SupportedConnections"].items()) {
                const int type = c["ConnectionType"].integer();
                Connection candidate;
                candidate.type = type;
                if (type == kConnectionJsonRpc)
                    candidate.hostNetworkId = str::Lower(c["PmsgId"].str());
                else if (type == kConnectionWebSocket)
                    candidate.hostNetworkId = c["RakNetGUID"].text();
                else
                    continue;
                if (candidate.hostNetworkId.empty() ||
                    candidate.hostNetworkId == "00000000-0000-0000-0000-000000000000")
                    continue;
                if (!found || candidate.type == kConnectionJsonRpc) {
                    out = candidate;
                    found = true;
                }
            }
            return found;
        }

        std::string SessionPath(std::string location) {
            const size_t scheme = location.find("://");
            if (scheme != std::string::npos) {
                const size_t slash = location.find('/', scheme + 3);
                location = slash == std::string::npos ? "/" : location.substr(slash);
            }
            return location;
        }

        struct SessionState {
            std::string authorization;
            std::string path;
            Rta rta;
            bool joined = false;

            void Leave() {
                if (joined) {
                    joined = false;
                    const http::Response r =
                        Mpsd("PUT", path, "{\"members\":{\"me\":null}}", authorization, {{"If-Match", "*"}});
                    Log("friends", "left the session (HTTP {})", r.status);
                }
                rta.Close();
            }
        };
    }

    namespace {
        bool OpenSignaling(const xbox::ServiceInfo& service, int type, Lease& lease, std::string& error) {
            if (type == kConnectionJsonRpc) {
                if (service.signalingAfdUri.empty()) {
                    error = "Minecraft did not name a signalling service";
                    return false;
                }
                const http::Response config = http::GetUrl(
                    service.signalingAfdUri + "/api/v1.0/configuration",
                    {{"Authorization", service.authorization}, {"User-Agent", "libhttpclient/1.0.0.0"}}, 20);
                std::string signalingUri;
                if (auto doc = config.status == 200 ? json::Document::Parse(config.body) : std::nullopt)
                    signalingUri = doc->root()["result"]["signalingUri"].str();
                if (signalingUri.empty()) {
                    error = Failure(config, "the Minecraft signalling directory");
                    return false;
                }
                auto signaling = std::make_shared<JsonRpcSignaling>();
                if (!signaling->Open(signalingUri, service.authorization, error))
                    return false;
                lease.signaling = signaling;
                return true;
            }
            if (service.signalingUri.empty()) {
                error = "Minecraft did not name a signalling service";
                return false;
            }
            auto signaling = std::make_shared<WebSocketSignaling>();
            if (!signaling->Open(service.signalingUri, service.authorization, error))
                return false;
            lease.signaling = signaling;
            return true;
        }
    }

    bool JoinRealm(xbox::Account& account, const std::string& address, const std::string& protocol,
                   const std::string& gameVersion, const std::string& realmName, std::shared_ptr<Lease>& out,
                   std::string& error) {
        out.reset();
        xbox::ServiceInfo service;
        if (!account.ServiceSession(gameVersion, service, error))
            return false;
        auto lease = std::make_shared<Lease>();
        const int type = protocol == "NETHERNET_JSONRPC" ? kConnectionJsonRpc : kConnectionWebSocket;
        if (!OpenSignaling(service, type, *lease, error))
            return false;
        lease->hostNetworkId = type == kConnectionJsonRpc ? str::Lower(address) : address;
        lease->issuer = service.issuer;
        lease->worldName = realmName;
        lease->hostName = realmName;
        out = std::move(lease);
        return true;
    }

    bool Join(xbox::Account& account, const std::string& friendXuid, const std::string& gameVersion,
              std::shared_ptr<Lease>& out, const ProgressFn& progress, std::string& error) {
        out.reset();
        const auto say = [&](const std::string& text) {
            if (progress)
                progress(text);
        };

        say("Signing in to Minecraft...");
        xbox::ServiceInfo service;
        if (!account.ServiceSession(gameVersion, service, error))
            return false;
        std::string authorization, myXuid;
        if (!account.AuthorizationHeader("http://xboxlive.com", authorization, myXuid, error))
            return false;
        if (myXuid.empty()) {
            error = "Xbox Live did not say who you are";
            return false;
        }

        say("Looking for the world...");
        const std::string query = "{\"type\":\"activity\",\"scid\":\"" + std::string(kScid) +
                                  "\",\"owners\":{\"xuids\":[" + Q(friendXuid) + "]}}";
        const http::Response found =
            Mpsd("POST", "/handles/query?include=relatedInfo,customProperties", query, authorization);
        if (found.status != 200) {
            error = Failure(found, "Xbox Live's session directory");
            return false;
        }
        std::string handleId, hostName, worldName, version;
        int protocol = 0;
        Connection connection;
        {
            auto doc = json::Document::Parse(QuoteBigIds(found.body));
            if (!doc) {
                error = "Xbox Live sent an unreadable session list";
                return false;
            }
            for (const json::View handle : doc->root()["results"].items()) {
                if (handle["relatedInfo"]["closed"].boolean())
                    continue;
                const json::View custom = handle["customProperties"];
                Connection c;
                if (!PickConnection(custom, c))
                    continue;
                if (custom["TransportLayer"].integer() != kTransportNetherNet)
                    continue;
                handleId = handle["id"].str();
                connection = c;
                hostName = custom["hostName"].str();
                worldName = custom["worldName"].str();
                version = custom["version"].str();
                protocol = custom["protocol"].integer();
                break;
            }
        }
        if (handleId.empty()) {
            error = "that friend isn't hosting a world you can join";
            return false;
        }
        Log("friends", "world '{}' hosted by {}, connection type {}", worldName, hostName, connection.type);

        say("Connecting to Xbox Live...");
        auto state = std::make_shared<SessionState>();
        state->authorization = authorization;
        if (!state->rta.Open(authorization, error))
            return false;
        const uint64_t changesAtJoin = state->rta.changes.load();
        (void)changesAtJoin;

        say("Joining " + (hostName.empty() ? std::string("the world") : hostName) + "'s session...");
        const std::string joinBody =
            "{\"members\":{\"me\":{\"constants\":{\"system\":{\"initialize\":true,\"xuid\":" + Q(myXuid) +
            "}},\"properties\":{\"system\":{\"active\":true,\"connection\":" + Q(state->rta.connectionId) +
            ",\"subscription\":{\"id\":" + Q(Upper(NewUuid())) + ",\"changeTypes\":[\"everything\"]}}}}}}";
        http::Response joined;
        std::string ifMatch = "*";
        for (int attempt = 0; attempt < 3; attempt++) {
            joined = Mpsd("PUT", "/handles/" + handleId + "/session", joinBody, authorization,
                          {{"If-Match", ifMatch}});
            if (joined.status != 412)
                break;
            const auto etag = joined.headers.find("etag");
            ifMatch = etag == joined.headers.end() || etag->second.empty() ? "*" : etag->second;
        }
        if (joined.status != 200 && joined.status != 201) {
            error = Failure(joined, "Xbox Live's session directory");
            state->rta.Close();
            return false;
        }
        const auto location = joined.headers.find("content-location");
        if (location == joined.headers.end() || location->second.empty()) {
            error = "Xbox Live did not say where the session is";
            state->rta.Close();
            return false;
        }
        state->path = SessionPath(location->second);
        state->joined = true;
        const auto fail = [&](const std::string& why) {
            error = why;
            state->Leave();
            return false;
        };

        say("Waiting for the host to let you in...");
        std::string nonce;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        std::string body = joined.body;
        uint64_t seenChanges = state->rta.changes.load();
        while (true) {
            if (auto doc = json::Document::Parse(QuoteBigIds(body))) {
                const json::View custom = doc->root()["properties"]["custom"];
                const json::View nonces = custom["nonces"];
                if (nonces[myXuid].isString()) {
                    nonce = nonces[myXuid].str();
                    if (nonce.empty())
                        return fail("the host refused to let you in");
                    Connection updated;
                    if (PickConnection(custom, updated))
                        connection = updated;
                    if (custom["hostName"].isString())
                        hostName = custom["hostName"].str();
                    break;
                }
            }
            if (std::chrono::steady_clock::now() > deadline)
                return fail("the host did not let you in (is the world still open?)");
            for (int i = 0; i < 20 && state->rta.changes.load() == seenChanges; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            seenChanges = state->rta.changes.load();
            const http::Response session = Mpsd("GET", state->path, {}, authorization);
            if (session.status == 404 || session.status == 204)
                return fail("the session ended");
            if (session.status == 200)
                body = session.body;
        }
        Log("friends", "received a nonce from the host");

        say("Opening a connection to the host...");
        auto lease = std::make_shared<Lease>();
        {
            std::string openError;
            if (!OpenSignaling(service, connection.type, *lease, openError))
                return fail(openError);
        }
        lease->hostNetworkId = connection.hostNetworkId;
        lease->nonce = nonce;
        lease->issuer = service.issuer;
        lease->hostName = hostName;
        lease->worldName = worldName;
        lease->version = version;
        lease->protocol = protocol;
        lease->leave = [state] { state->Leave(); };
        out = std::move(lease);
        return true;
    }

}

#endif
