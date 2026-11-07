#include "network/Realms.hpp"

#include "network/Http.hpp"
#include "network/Transport.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "util/Log.hpp"

namespace perlin::realms {
    namespace {
        constexpr const char* kRelyingParty = "https://pocket.realms.minecraft.net/";
        constexpr const char* kHost = "bedrock.frontendlegacy.realms.minecraft-services.net";

        std::string Q(const std::string& s) {
            return "\"" + s + "\"";
        }

        http::Response Call(const std::string& method, const std::string& path, const std::string& body,
                            const std::string& authorization) {
            std::map<std::string, std::string> headers{
                {"User-Agent", "libhttpclient/1.0.0.0"},
                {"Client-Version", kBedrockVersion},
                {"X-ClientPlatform", "Android"},
                {"X-NetworkProtocolVersion", std::to_string(kBedrockProtocol)},
                {"Authorization", authorization},
            };
            return http::Request(kHost, 443, true, method, path, body,
                                 body.empty() ? std::string() : "application/json", headers, 20);
        }

        bool OptInToTimeline(int64_t id, const std::string& authorization) {
            const std::string path = "/worlds/" + std::to_string(id) + "/stories/settings";
            const http::Response g = Call("GET", path, {}, authorization);
            Log("realms", "stories settings GET answered {}: {}", g.status, g.body.substr(0, 300));
            auto doc = g.status == 200 ? json::Document::Parse(g.body) : std::nullopt;
            if (!doc)
                return false;
            const json::View v = doc->root();
            const auto flag = [&](const char* k) { return v[k].boolean() ? "true" : "false"; };
            const std::string realmOptIn = v["realmOptIn"].isString() ? v["realmOptIn"].str() : "NONE";
            const std::string body = std::string("{\"autostories\":") + flag("autostories") +
                                     ",\"coordinates\":" + flag("coordinates") +
                                     ",\"notifications\":" + flag("notifications") +
                                     ",\"playerOptIn\":\"OPT_IN\",\"realmOptIn\":\"" + realmOptIn +
                                     "\",\"timeline\":" + flag("timeline") + "}";
            const http::Response p = Call("POST", path, body, authorization);
            Log("realms", "timeline opt-in answered {}: {}", p.status, p.body.substr(0, 300));
            return p.status >= 200 && p.status < 300;
        }

        std::string Failure(const http::Response& r, const char* what) {
            if (r.status == 0)
                return std::string("could not reach ") + what;
            std::string text = std::string(what) + " answered HTTP " + std::to_string(r.status);
            if (auto doc = json::Document::Parse(r.body)) {
                const std::string why = doc->root()["errorMsg"].isString()  ? doc->root()["errorMsg"].str()
                                        : doc->root()["message"].isString() ? doc->root()["message"].str()
                                                                            : std::string();
                if (!why.empty())
                    text += ": " + why;
            }
            return text;
        }
    }

    bool List(xbox::Account& account, std::vector<Realm>& out, std::string& error) {
        out.clear();
        std::string authorization, xuid;
        if (!account.AuthorizationHeader(kRelyingParty, authorization, xuid, error))
            return false;
        const http::Response r = Call("GET", "/worlds", {}, authorization);
        auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
        if (!doc) {
            error = Failure(r, "Realms");
            return false;
        }
        for (const json::View s : doc->root()["servers"].items()) {
            Realm realm;
            realm.id = s["id"].isNumber() ? static_cast<int64_t>(s["id"].number()) : 0;
            realm.name = s["name"].str();
            realm.owner = s["owner"].str();
            realm.motd = s["motd"].str();
            realm.state = s["state"].str();
            realm.expired = s["expired"].boolean() || s["expiredTrial"].boolean();
            realm.member = s["ownerUUID"].str() != xuid;
            realm.maxPlayers = s["maxPlayers"].isNumber() ? static_cast<int>(s["maxPlayers"].number()) : 0;
            if (realm.id != 0)
                out.push_back(std::move(realm));
        }
        return true;
    }

    bool AcceptInvite(xbox::Account& account, const std::string& codeOrLink, Realm& out, std::string& error) {
        std::string code = codeOrLink.substr(0, codeOrLink.find_first_of("?#"));
        while (!code.empty() && (code.back() == '/' || code.back() == ' '))
            code.pop_back();
        if (const size_t slash = code.rfind('/'); slash != std::string::npos)
            code.erase(0, slash + 1);
        while (!code.empty() && code.front() == ' ')
            code.erase(code.begin());
        bool plain = !code.empty() && code.size() <= 32;
        for (char ch : code)
            plain = plain && (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-');
        if (!plain) {
            error = "that is not a Realm invite code or link";
            return false;
        }
        std::string authorization, xuid;
        if (!account.AuthorizationHeader(kRelyingParty, authorization, xuid, error))
            return false;
        const http::Response r = Call("POST", "/invites/v1/link/accept/" + code, {}, authorization);
        auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
        if (!doc) {
            error = r.status == 404 ? "that invite code was not found or has expired" : Failure(r, "Realms");
            return false;
        }
        const json::View s = doc->root();
        out = {};
        out.id = s["id"].isNumber() ? static_cast<int64_t>(s["id"].number()) : 0;
        out.name = s["name"].str();
        return out.id != 0;
    }

    bool Join(xbox::Account& account, int64_t id, Address& out, std::string& error) {
        std::string authorization, xuid;
        if (!account.AuthorizationHeader(kRelyingParty, authorization, xuid, error))
            return false;
        const std::string body = "{\"joinIntention\":\"VANILLA\",\"pingRegions\":[]}";
        const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        bool optedIn = false;
        while (true) {
            http::Response r = Call("POST", "/worlds/" + std::to_string(id) + "/join", body, authorization);
            if ((r.status == 503 || r.status == 277) && std::chrono::steady_clock::now() < giveUp) {
                int wait = 5;
                const auto it = r.headers.find("Retry-After");
                if (it != r.headers.end())
                    wait = std::clamp(std::atoi(it->second.c_str()), 1, 15);
                Log("realms", "Realm {} is starting, retrying in {}s", id, wait);
                std::this_thread::sleep_for(std::chrono::seconds(wait));
                continue;
            }
            if (r.status == 403 && !optedIn && (r.body.contains("Opt-In") || r.body.contains("opt-in"))) {
                optedIn = true;
                if (OptInToTimeline(id, authorization))
                    continue;
            }
            if (r.status >= 400) {
                Log("realms", "join POST answered {}: {}", r.status, r.body.substr(0, 400));
                if (r.status == 403 || r.status == 404 || r.status == 405) {
                    const http::Response g =
                        Call("GET", "/worlds/" + std::to_string(id) + "/join", {}, authorization);
                    Log("realms", "join GET answered {}: {}", g.status, g.body.substr(0, 400));
                    if (g.status == 200) {
                        r = g;
                    } else {
                        error = Failure(r, "Realms") +
                                (r.status == 403 ? " (the Realms service refused this account)" : "");
                        return false;
                    }
                }
            }
            auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
            if (!doc || !doc->root()["address"].isString()) {
                error = Failure(r, "Realms");
                return false;
            }
            const std::string address = doc->root()["address"].str();
            const size_t colon = address.rfind(':');
            out.host = colon == std::string::npos ? address : address.substr(0, colon);
            out.port = colon == std::string::npos
                           ? uint16_t(19132)
                           : static_cast<uint16_t>(std::atoi(address.c_str() + colon + 1));
            out.protocol =
                doc->root()["networkProtocol"].isString() ? doc->root()["networkProtocol"].str() : "DEFAULT";
            for (char& c : out.protocol)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            Log("realms", "Realm {} is at {}:{} ({})", id, out.host, out.port, out.protocol);
            return !out.host.empty();
        }
    }

}
