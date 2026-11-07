#include "network/Featured.hpp"

#include "network/Http.hpp"
#include "network/Transport.hpp"
#include "util/Json.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include "util/Log.hpp"

namespace perlin::featured {
    namespace {
        constexpr const char* kDiscoveryHost = "client.discovery.minecraft-services.net";
        constexpr const char* kFallbackHost = "gatherings-secondary.franchise.minecraft-services.net";

        std::string Localized(const json::View dict) {
            for (const char* key : {"en-US", "en_US", "NEUTRAL", "neutral"})
                if (dict[key].isString())
                    if (std::string v = str::Trim(dict[key].str()); !v.empty())
                        return v;
            return {};
        }

        bool ValidUrl(const std::string& u) {
            return u.starts_with("https://") || u.starts_with("http://");
        }

        std::string Image(const json::View images, std::initializer_list<const char*> tags) {
            for (const char* tag : tags)
                for (const json::View image : images.items()) {
                    if (!image["Tag"].isString() || !image["Url"].isString())
                        continue;
                    std::string a = image["Tag"].str(), b = tag;
                    std::transform(a.begin(), a.end(), a.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    std::transform(b.begin(), b.end(), b.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (a == b && ValidUrl(image["Url"].str()))
                        return image["Url"].str();
                }
            return {};
        }

        std::string GatheringsHost() {
            const http::Response r =
                http::Request(kDiscoveryHost, 443, true, "GET",
                              std::string("/api/v1.0/discovery/MinecraftPE/builds/") + kBedrockVersion, {},
                              {}, {{"Accept", "application/json"}}, 10);
            if (r.status == 200)
                if (auto doc = json::Document::Parse(r.body)) {
                    const json::View uri =
                        doc->root()["result"]["serviceEnvironments"]["gatherings"]["prod"]["serviceUri"];
                    const json::View alt =
                        doc->root()["serviceEnvironments"]["gatherings"]["prod"]["serviceUri"];
                    std::string s = uri.isString() ? uri.str() : alt.isString() ? alt.str() : std::string();
                    if (s.starts_with("https://")) {
                        s.erase(0, 8);
                        while (!s.empty() && s.back() == '/')
                            s.pop_back();
                        if (!s.empty() && !s.contains('/'))
                            return s;
                    }
                }
            return kFallbackHost;
        }
    }

    bool List(xbox::Account& account, std::vector<Server>& out, std::string& error) {
        out.clear();
        xbox::ServiceInfo info;
        if (!account.ServiceSession(kBedrockVersion, info, error))
            return false;
        const std::string host = GatheringsHost();
        const http::Response r = http::Request(host, 443, true, "POST", "/api/v2.0/discovery/blob/client",
                                               "{}", "application/json",
                                               {{"Authorization", info.authorization},
                                                {"Accept", "application/json"},
                                                {"Accept-Language", "en-US"},
                                                {"User-Agent", "libhttpclient/1.0.0.0"}},
                                               20);
        auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
        if (!doc) {
            error = r.status == 0 ? "could not reach the featured servers service"
                                  : "the featured servers service answered HTTP " + std::to_string(r.status);
            return false;
        }
        struct Ranked {
            Server server;
            bool hasRank = false;
        };
        std::vector<Ranked> found;
        std::set<std::string> seen;
        for (const json::View item : doc->root()["data"]["Items"].items()) {
            if (item["IsHidden"].boolean() || !item["ContentType"].isString() ||
                item["ContentType"].str() != "3PP_V2.0")
                continue;
            json::View props = item["DisplayProperties"];
            std::optional<json::Document> inner;
            if (props.isString()) {
                inner = json::Document::Parse(props.str());
                if (!inner)
                    continue;
                props = inner->root();
            }
            if (!props.isObject())
                continue;
            Server s;
            s.host = str::Trim(props["url"].isString() ? props["url"].str() : std::string());
            const int port = props["port"].isNumber() ? static_cast<int>(props["port"].number()) : 0;
            if (s.host.empty() || port <= 0 || port > 65535)
                continue;
            s.port = static_cast<uint16_t>(port);
            s.name = Localized(item["Title"]);
            if (s.name.empty())
                continue;
            const std::string key = s.host + ":" + std::to_string(s.port);
            if (!seen.insert(key).second)
                continue;
            s.description = Localized(item["Description"]);
            s.logoUrl = Image(item["Images"], {"Icon", "Thumbnail"});
            Ranked r2;
            if (props["rank"].isNumber()) {
                r2.hasRank = true;
                s.rank = static_cast<int>(props["rank"].number());
                s.ranked = true;
            }
            r2.server = std::move(s);
            found.push_back(std::move(r2));
        }
        std::stable_sort(found.begin(), found.end(), [](const Ranked& a, const Ranked& b) {
            if (a.hasRank != b.hasRank)
                return a.hasRank;
            return a.hasRank && a.server.rank > b.server.rank;
        });
        for (Ranked& r2 : found)
            out.push_back(std::move(r2.server));
        Log("featured", "{} featured servers", out.size());
        return true;
    }

}
