#include "client/features/servers/ServerBrowser.hpp"
#include "client/systems/resources/TextureLoader.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "network/Featured.hpp"
#include "network/Friends.hpp"
#include "network/Http.hpp"
#include "network/Interfaces.hpp"
#include "network/NetherNet.hpp"
#include "network/RakNet.hpp"
#include "network/Realms.hpp"
#include "network/XboxAuth.hpp"
#include "util/FileIo.hpp"
#include "util/JsonValue.hpp"
#include "util/Paths.hpp"
#include "util/ServerAddress.hpp"
#include "util/Log.hpp"
#include "network/Interfaces.hpp"

#include <algorithm>
#include <set>
#include "util/ResourcePaths.hpp"
#include "util/Strings.hpp"
#include <filesystem>
#include <future>
#include <thread>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace perlin::servers {
    ServerBrowser::ServerBrowser(Deps d)
        : options(d.options), uiAssets(d.uiAssets), workers(d.workers), tasks(d.tasks), activeIsMicrosoft(std::move(d.activeIsMicrosoft)),
          activeAuthPath(std::move(d.activeAuthPath)), setStatusHint(std::move(d.setStatusHint)),
          hideTitle(std::move(d.hideTitle)), connect(std::move(d.connect)) {}

    namespace {
        std::filesystem::path FeaturedDir() { return std::filesystem::path(paths::DataDir()) / "featured"; }

        std::filesystem::path FeaturedLogoPath(const std::string& url) {
            uint64_t h = 1469598103934665603ull;
            for (const unsigned char c : url)
                h = (h ^ c) * 1099511628211ull;
            return FeaturedDir() / "logos" / (std::to_string(h) + ".png");
        }

        void SaveFeatured(const std::vector<featured::Server>& list) {
            json::Value servers = json::Value::MakeArray();
            for (const featured::Server& f : list) {
                json::Value e = json::Value::MakeObject();
                e.Set("name", json::Value(f.name));
                e.Set("description", json::Value(f.description));
                e.Set("host", json::Value(f.host));
                e.Set("port", json::Value(static_cast<double>(f.port)));
                e.Set("logo", json::Value(f.logoUrl));
                e.Set("ranked", json::Value(f.ranked));
                e.Set("rank", json::Value(static_cast<double>(f.rank)));
                servers.Push(std::move(e));
            }
            json::Value root = json::Value::MakeObject();
            root.Set("servers", std::move(servers));
            std::error_code ec;
            std::filesystem::create_directories(FeaturedDir(), ec);
            file::Write(FeaturedDir() / "servers.json", root.Dump(2));
        }

        bool LoadFeatured(std::vector<featured::Server>& out) {
            std::string text, error;
            json::Value root;
            if (!file::Read(FeaturedDir() / "servers.json", text) || !json::Value::Parse(text, root, error))
                return false;
            const json::Value* servers = root.Find("servers");
            if (!servers || !servers->IsArray())
                return false;
            for (const json::Value& e : servers->array) {
                featured::Server f;
                const auto str = [&](const char* key) {
                    const json::Value* v = e.Find(key);
                    return v ? v->StringOr("") : std::string();
                };
                f.name = str("name");
                f.description = str("description");
                f.host = str("host");
                f.logoUrl = str("logo");
                if (const json::Value* v = e.Find("port"); v && v->IsNumber())
                    f.port = static_cast<uint16_t>(v->number);
                if (const json::Value* v = e.Find("ranked"))
                    f.ranked = v->BoolOr(false);
                if (const json::Value* v = e.Find("rank"); v && v->IsNumber())
                    f.rank = static_cast<int>(v->number);
                if (!f.host.empty())
                    out.push_back(std::move(f));
            }
            return true;
        }

        std::string FeaturedIconKey(const featured::Server& f) {
            return "fs:" + f.host + ":" + std::to_string(f.port);
        }

        bool LoadFeaturedLogo(ui::UiAssets& uiAssets, const featured::Server& f) {
            const std::string key = FeaturedIconKey(f);
            if (uiAssets.HasHead(key))
                return true;
            std::vector<uint8_t> bytes;
            Texture t;
            if (f.logoUrl.empty() || !file::ReadBytes(FeaturedLogoPath(f.logoUrl), bytes) ||
                !LoadTextureMemory(bytes.data(), bytes.size(), t))
                return false;
            uiAssets.SetHead(key, std::move(t));
            return true;
        }

        bool DownloadFeaturedLogo(ui::UiAssets& uiAssets, const featured::Server& f) {
            const http::Response pic = http::GetUrl(f.logoUrl, {}, 8);
            Texture t;
            if (pic.status != 200 ||
                !LoadTextureMemory(reinterpret_cast<const unsigned char*>(pic.body.data()), pic.body.size(), t))
                return false;
            const std::filesystem::path path = FeaturedLogoPath(f.logoUrl);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            file::Write(path, pic.body);
            uiAssets.SetHead(FeaturedIconKey(f), std::move(t));
            return true;
        }

        std::vector<ui::ServerEntry> FeaturedRows(ui::UiAssets& uiAssets, const std::vector<featured::Server>& list) {
            std::vector<ui::ServerEntry> rows;
            for (const featured::Server& f : list) {
                ui::ServerEntry e;
                e.kind = ui::ServerEntry::Kind::Lan;
                e.name = f.name;
                e.address = f.host + ":" + std::to_string(f.port);
                e.right = f.ranked ? "Featured" : "Creator";
                e.pingable = true;
                e.motd = f.description;
                e.note = e.address + (f.ranked ? "" : "  -  Creator");
                if (uiAssets.HasHead(FeaturedIconKey(f)))
                    e.icon = FeaturedIconKey(f);
                rows.push_back(std::move(e));
            }
            return rows;
        }
        struct SharedSignaling : friendworld::Signaling {
            std::shared_ptr<friendworld::Signaling> inner;
            explicit SharedSignaling(std::shared_ptr<friendworld::Signaling> s) : inner(std::move(s)) {}
            bool Send(const std::string& to, const std::string& t, bool w, std::string& e) override {
                return inner->Send(to, t, w, e);
            }
            void SetHandler(std::function<void(const std::string&, const std::string&)> h) override {
                inner->SetHandler(std::move(h));
            }
            bool Credentials(std::vector<friendworld::IceServerInfo>& s, std::string& e) override {
                return inner->Credentials(s, e);
            }
            bool Alive() const override { return inner->Alive(); }
            void Close() override { inner->SetHandler(nullptr); }
        };

        ui::ServerEntry SectionHeader(std::string name, size_t count, int sectionId) {
            ui::ServerEntry head;
            head.kind = ui::ServerEntry::Kind::Header;
            head.name = std::move(name) + " [" + std::to_string(count) + "]";
            head.realmId = sectionId;
            return head;
        }
    }


    void ServerBrowser::Ping(const std::vector<std::string>& addresses) {
        for (const std::string& address : addresses) {
            {
                std::lock_guard lock(pingMutex);
                ui::ServerEntry& r = pingResults[address];
                if (r.status == ui::ServerEntry::Status::Pinging)
                    continue;
                r.status = ui::ServerEntry::Status::Pinging;
            }
            workers->Submit([this, address] {
                ui::ServerEntry r;
                std::string host, ad, error, text = address;
                uint16_t port = 19132;
                for (const char* scheme : {"https://", "http://"})
                    if (text.starts_with(scheme))
                        text.erase(0, std::char_traits<char>::length(scheme));
                const auto start = Clock::now();
                const bool parsed = ParseServerAddress(text, host, port);
                const bool ok = parsed && RakNet::Ping(host, port, ad, error, 2500);
                NetherNetServerInfo info;
                const bool nether = parsed && !ok && NetherNet::Probe(host, port, info, error, 3);
                r.pingMs = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
                r.status = ok || nether ? ui::ServerEntry::Status::Online : ui::ServerEntry::Status::Offline;
                if (nether) {
                    r.motd = str::StripFormatting(info.name);
                    r.version = info.version;
                    r.players = info.players;
                    r.maxPlayers = info.maxPlayers;
                }
                if (ok) {
                    std::vector<std::string> f;
                    size_t from = 0;
                    for (size_t at; (at = ad.find(';', from)) != std::string::npos; from = at + 1)
                        f.push_back(ad.substr(from, at - from));
                    f.push_back(ad.substr(from));
                    if (f.size() > 1)
                        r.motd = str::StripFormatting(f[1]);
                    if (f.size() > 3)
                        r.version = f[3];
                    if (f.size() > 5) {
                        r.players = std::atoi(f[4].c_str());
                        r.maxPlayers = std::atoi(f[5].c_str());
                    }
                }
                std::lock_guard lock(pingMutex);
                pingResults[address] = std::move(r);
            });
        }
    }

    void ServerBrowser::JoinServer(const std::string& address) {
        if (address.empty())
            return;
        ReleaseFriendLease();
        options.server = address;
        options.Save();
        hideTitle();
        connect();
    }

    void ServerBrowser::SetJoinMessage(const std::string& text) {
        if (leaseJoining) {
            setStatusHint(text);
            return;
        }
        std::lock_guard lock(tabMutex);
        tabJoinMsg = text;
    }

    void ServerBrowser::BeginLeaseJoin(const std::string& text) {
        leaseJoining = true;
        setStatusHint(text);
        hideTitle();
    }

    void ServerBrowser::FailLeaseJoin(const std::string& text) {
        const bool loading = leaseJoining.exchange(false);
        setStatusHint({});
        {
            std::lock_guard lock(tabMutex);
            tabJoinMsg = text;
        }
        if (loading)
            leaseJoinFailed = true;
    }

    void ServerBrowser::RefreshTab(ui::ServerTab tab) {
        using T = ui::ServerTab;
        const bool microsoft = activeIsMicrosoft();
        const std::string authPath = activeAuthPath();
        if (tab == T::Lan) {
            if (lanBusy.exchange(true))
                return;
            {
                std::lock_guard lock(tabMutex);
                tabLan.clear();
                tabLanMsg = "Scanning for games on your local network";
                tabDirty = true;
            }
            tasks.Run([this] {
                std::vector<RakNet::LanServer> found;
                std::string error;
                RakNet::Discover(found, error, 2000);
                std::vector<ui::ServerEntry> rows;
                std::set<std::string> discoveredIps;
                {
                    std::string lanError;
                    std::shared_ptr<lan::Session> session;
                    {
                        std::lock_guard lock(lanSessionMutex);
                        if (!lanSession || !lanSession->Alive())
                            lanSession = lan::Session::Open(lanError);
                        session = lanSession;
                    }
                    if (session) {
                        for (int i = 0; i < 4; i++) {
                            session->Scan();
                            std::this_thread::sleep_for(std::chrono::milliseconds(400));
                        }
                        for (const lan::Host& h : session->Hosts()) {
                            ui::ServerEntry e;
                            e.kind = ui::ServerEntry::Kind::Lan;
                            e.xuid = "lan:" + std::to_string(h.networkId);
                            e.address = h.ip + ":" + std::to_string(h.port);
                            e.name = h.name.empty() ? "LAN Game" : h.name;
                            e.right = std::to_string(h.players) + "/" + std::to_string(h.maxPlayers);
                            e.motd = h.level;
                            e.version = h.version;
                            e.note = h.ip + (h.version.empty() ? "" : "  -  " + h.version);
                            discoveredIps.insert(h.ip);
                            rows.push_back(std::move(e));
                        }
                    } else if (!lanError.empty()) {
                        Log("lan", "discovery unavailable: {}", lanError);
                    }
                }
                for (const RakNet::LanServer& f : found) {
                    if (discoveredIps.contains(f.host))
                        continue;
                    std::vector<std::string> parts;
                    size_t a = 0;
                    for (size_t i = 0; i <= f.advertisement.size(); i++)
                        if (i == f.advertisement.size() || f.advertisement[i] == ';') {
                            parts.push_back(f.advertisement.substr(a, i - a));
                            a = i + 1;
                        }
                    ui::ServerEntry e;
                    e.kind = ui::ServerEntry::Kind::Lan;
                    e.address = f.host + ":" + std::to_string(f.port);
                    e.name = parts.size() > 1 && !parts[1].empty() ? parts[1] : "LAN Game";
                    std::string plain;
                    for (size_t i = 0; i < e.name.size(); i++) {
                        if (static_cast<unsigned char>(e.name[i]) == 0xC2 && i + 2 < e.name.size() &&
                            static_cast<unsigned char>(e.name[i + 1]) == 0xA7) {
                            i += 2;
                            continue;
                        }
                        plain += e.name[i];
                    }
                    e.name = plain;
                    e.right = parts.size() > 5 ? parts[4] + "/" + parts[5] : "";
                    e.motd = parts.size() > 7 ? parts[7] : "";
                    e.note = e.address + (parts.size() > 3 ? "  -  " + parts[3] : "");
                    rows.push_back(std::move(e));
                }
                {
                    std::vector<std::string> hosts{"127.0.0.1"};
#ifdef PERLIN_HAS_IFADDRS
                    net::ForEachInterface([&](const net::InterfaceAddress& i) {
                        if (!i.address || i.address->sa_family != AF_INET || i.loopback || !i.up)
                            return;
                        const uint32_t ip =
                            ntohl(reinterpret_cast<const sockaddr_in*>(i.address)->sin_addr.s_addr);
                        for (uint32_t last = 1; last < 255; last++) {
                            const uint32_t host = (ip & 0xFFFFFF00u) | last;
                            char text[32];
                            std::snprintf(text, sizeof text, "%u.%u.%u.%u", host >> 24, (host >> 16) & 255,
                                          (host >> 8) & 255, host & 255);
                            hosts.push_back(text);
                        }
                    });
#endif
                    std::ranges::sort(hosts);
                    hosts.erase(std::unique(hosts.begin(), hosts.end()), hosts.end());
                    std::mutex foundMutex;
                    std::atomic<size_t> next{0};
                    const auto worker = [&] {
                        for (size_t i = next++; i < hosts.size(); i = next++) {
                            NetherNetServerInfo info;
                            std::string why;
                            if (discoveredIps.contains(hosts[i]))
                                continue;
                            if (!NetherNet::Probe(hosts[i], 19132, info, why, 1))
                                continue;
                            ui::ServerEntry e;
                            e.kind = ui::ServerEntry::Kind::Lan;
                            e.address = hosts[i] + ":19132";
                            e.name = info.name.empty() ? "LAN Game" : info.name;
                            e.right = std::to_string(info.players) + "/" + std::to_string(info.maxPlayers);
                            e.motd = info.level;
                            e.note = e.address + (info.version.empty() ? "" : "  -  " + info.version);
                            std::lock_guard lock(foundMutex);
                            if (std::ranges::none_of(
                                    rows, [&](const ui::ServerEntry& r) { return r.address == e.address; }))
                                rows.push_back(std::move(e));
                        }
                    };
                    std::vector<std::thread> pool;
                    pool.reserve(48);
                    for (int i = 0; i < 48; i++)
                        pool.emplace_back(worker);
                    for (std::thread& t : pool)
                        t.join();
                }
                std::lock_guard lock(tabMutex);
                tabLan = std::move(rows);
                tabLanMsg = tabLan.empty() ? "No games found on your local network" +
                                                 (error.empty() ? std::string() : " (" + error + ")")
                                           : "";
                tabDirty = true;
                lanBusy = false;
            });
            return;
        }
        if (tab == T::Saved) {
            RefreshFeatured(false);
            return;
        }
        if (tab != T::Realms && tab != T::Friends)
            return;
        const bool forRealms = tab == T::Realms;
        std::atomic<bool>& busy = forRealms ? realmsBusy : friendsBusy;
        if (!microsoft) {
            std::lock_guard lock(tabMutex);
            (forRealms ? tabRealms : tabFriends).clear();
            (forRealms ? tabRealmsMsg : tabFriendsMsg) =
                std::string("Sign in with a Microsoft account to see your ") +
                (forRealms ? "Realms" : "friends");
            tabDirty = true;
            return;
        }
        if (busy.exchange(true))
            return;
        {
            std::lock_guard lock(tabMutex);
            (forRealms ? tabRealms : tabFriends).clear();
            (forRealms ? tabRealmsMsg : tabFriendsMsg) =
                forRealms ? "Loading your Realms..." : "Loading your friends...";
            tabDirty = true;
        }
        tasks.Run([this, forRealms, authPath, &busy] {
            xbox::Account acct(authPath);
            std::string error;
            std::vector<ui::ServerEntry> rows;
            bool ok = acct.SignInSaved(error);
            if (ok && forRealms) {
                std::vector<realms::Realm> list;
                ok = realms::List(acct, list, error);
                for (const realms::Realm& r : list) {
                    ui::ServerEntry e;
                    e.kind = ui::ServerEntry::Kind::Realm;
                    e.name = r.name.empty() ? "Realm" : r.name;
                    e.realmId = r.id;
                    const bool open = r.state == "OPEN" && !r.expired;
                    e.offline = !open;
                    e.right = r.expired ? "Expired" : (r.state == "OPEN" ? "Open" : "Closed");
                    e.motd = !r.owner.empty() ? "Owned by " + r.owner : (r.member ? "Invited" : "Your Realm");
                    e.note = r.motd;
                    rows.push_back(std::move(e));
                }
            } else if (ok) {
                std::vector<friends::Friend> list;
                ok = friends::List(acct, list, error);
                {
                    std::atomic<size_t> next{0};
                    const auto fetch = [&] {
                        for (size_t i = next++; i < list.size(); i = next++) {
                            const friends::Friend& f = list[i];
                            const std::string key = "gp:" + f.xuid;
                            if (f.pictureUrl.empty() || uiAssets.HasHead(key))
                                continue;
                            const http::Response pic = http::GetUrl(
                                f.pictureUrl + (!f.pictureUrl.contains('?') ? "?w=64&h=64" : ""), {}, 8);
                            Texture t;
                            if (pic.status == 200 &&
                                LoadTextureMemory(reinterpret_cast<const unsigned char*>(pic.body.data()),
                                                  pic.body.size(), t))
                                uiAssets.SetHead(key, std::move(t));
                        }
                    };
                    std::vector<std::thread> pool;
                    pool.reserve(6);
                    for (int i = 0; i < 6; i++)
                        pool.emplace_back(fetch);
                    for (std::thread& t : pool)
                        t.join();
                }
                for (const friends::Friend& f : list) {
                    ui::ServerEntry e;
                    e.kind = ui::ServerEntry::Kind::Friend;
                    e.name = f.gamertag;
                    e.xuid = f.xuid;
                    e.hosting = f.hosting;
                    e.offline = !f.online;
                    if (uiAssets.HasHead("gp:" + f.xuid))
                        e.icon = "gp:" + f.xuid;
                    e.right = f.hosting
                                  ? "Hosting"
                                  : (f.playingMinecraft ? "In Minecraft" : (f.online ? "Online" : "Offline"));
                    e.motd = f.hosting && !f.worldName.empty() ? f.worldName : f.presence;
                    e.note = f.hosting ? std::to_string(f.players) + "/" + std::to_string(f.maxPlayers) +
                                             " players  -  click to join"
                                       : "Not hosting a world";
                    rows.push_back(std::move(e));
                }
            }
            std::lock_guard lock(tabMutex);
            auto& target = forRealms ? tabRealms : tabFriends;
            auto& message = forRealms ? tabRealmsMsg : tabFriendsMsg;
            target = std::move(rows);
            message =
                !ok ? error
                    : (target.empty() ? (forRealms ? "You have no Realms yet" : "Your friends list is empty")
                                      : std::string());
            tabDirty = true;
            busy = false;
        });
    }

    void ServerBrowser::RefreshFeatured(bool force) {
        (void)force;
        if (!activeIsMicrosoft()) {
            std::lock_guard lock(tabMutex);
            tabFeatured.clear();
            return;
        }

        if (!featuredCacheLoaded.exchange(true)) {
            std::vector<featured::Server> cached;
            if (LoadFeatured(cached) && !cached.empty()) {
                int logos = 0;
                for (const featured::Server& f : cached)
                    logos += LoadFeaturedLogo(uiAssets, f) ? 1 : 0;
                Log("featured", "{} servers ({} logos) from the cache", cached.size(), logos);
                std::lock_guard lock(tabMutex);
                tabFeatured = FeaturedRows(uiAssets, cached);
                featuredPing = true;
            }
        }
        if (featuredBusy.exchange(true))
            return;
        const std::string authPath = activeAuthPath();
        tasks.Run([this, authPath] {
            xbox::Account acct(authPath);
            std::string error;
            std::vector<featured::Server> list;
            const bool ok = acct.SignInSaved(error) && featured::List(acct, list, error);
            if (!ok) {
                Log("featured", "{}", error);
                featuredBusy = false;
                return;
            }
            SaveFeatured(list);
            std::vector<const featured::Server*> missing;
            for (const featured::Server& f : list)
                if (!LoadFeaturedLogo(uiAssets, f) && !f.logoUrl.empty())
                    missing.push_back(&f);
            {
                std::lock_guard lock(tabMutex);
                tabFeatured = FeaturedRows(uiAssets, list);
                featuredPing = true;
            }

            std::vector<std::future<bool>> downloads;
            for (const featured::Server* f : missing)
                downloads.push_back(std::async(std::launch::async, [&assets = uiAssets, f] { return DownloadFeaturedLogo(assets, *f); }));
            bool fetched = false;
            for (std::future<bool>& d : downloads)
                fetched = d.get() || fetched;
            if (fetched) {
                const std::vector<ui::ServerEntry> rows = FeaturedRows(uiAssets, list);
                std::lock_guard lock(tabMutex);
                for (ui::ServerEntry& row : tabFeatured)
                    for (const ui::ServerEntry& fresh : rows)
                        if (fresh.address == row.address)
                            row.icon = fresh.icon;
            }
            featuredBusy = false;
        });
    }

    void ServerBrowser::AddRealm(const std::string& code) {
        if (!activeIsMicrosoft()) {
            SetJoinMessage("Sign in with a Microsoft account to add a Realm");
            return;
        }
        if (realmAddBusy.exchange(true))
            return;
        SetJoinMessage("Adding the Realm...");
        const std::string authPath = activeAuthPath();
        tasks.Run([this, code, authPath] {
            xbox::Account acct(authPath);
            std::string error;
            realms::Realm added;
            if (acct.SignInSaved(error) && realms::AcceptInvite(acct, code, added, error)) {
                SetJoinMessage("Added the Realm \"" + added.name + "\"");
                realmAddedAt = Clock::now();
                realmAddedFlag = true;
            } else {
                SetJoinMessage(error.empty() ? "Could not add the Realm" : error);
            }
            realmAddBusy = false;
        });
    }

    void ServerBrowser::JoinRealm(long long id) {
        if (!activeIsMicrosoft()) {
            SetJoinMessage("Sign in with a Microsoft account to join a Realm");
            return;
        }
        if (realmJoinBusy.exchange(true))
            return;
        BeginLeaseJoin("Finding your Realm...");
        const std::string authPath = activeAuthPath();
        tasks.Run([this, id, authPath] {
            xbox::Account acct(authPath);
            std::string error;
            realms::Address address;
            if (acct.SignInSaved(error) && realms::Join(acct, id, address, error)) {
                if (address.protocol != "DEFAULT") {
                    SetJoinMessage("Connecting to the Realm...");
                    std::shared_ptr<friendworld::Lease> lease;
                    if (friendworld::JoinRealm(acct, address.host, address.protocol, kBedrockVersion,
                                               address.host, lease, error)) {
                        std::lock_guard lock(tabMutex);
                        friendLeaseReady = std::move(lease);
                    } else {
                        FailLeaseJoin(error.empty() ? "Could not reach the Realm" : error);
                    }
                } else {
                    std::lock_guard lock(tabMutex);
                    realmJoinAddress = address.host + ":" + std::to_string(address.port);
                    realmJoinReady = true;
                }
            } else {
                FailLeaseJoin(error.empty() ? "Could not join the Realm" : error);
            }
            realmJoinBusy = false;
        });
    }

    void ServerBrowser::JoinFriend(const ui::ServerEntry& entry) {
        if (!entry.hosting) {
            SetJoinMessage(entry.name +
                           (entry.offline ? " is offline" : " isn't hosting a world you can join"));
            return;
        }
        if (!activeIsMicrosoft()) {
            SetJoinMessage("Sign in with a Microsoft account to join a friend");
            return;
        }
        if (friendJoinBusy.exchange(true))
            return;
        BeginLeaseJoin("Joining " + entry.name + "...");
        const std::string authPath = activeAuthPath();
        const std::string xuid = entry.xuid;
        const std::string name = entry.name;
        tasks.Run([this, authPath, xuid, name] {
            xbox::Account acct(authPath);
            std::string error;
            std::shared_ptr<friendworld::Lease> lease;
            if (acct.SignInSaved(error) &&
                friendworld::Join(
                    acct, xuid, kBedrockVersion, lease,
                    [this](const std::string& text) { SetJoinMessage(text); }, error)) {
                std::lock_guard lock(tabMutex);
                friendLeaseReady = std::move(lease);
            } else {
                Log("friends", "join failed: {}", error);
                FailLeaseJoin(error.empty() ? "Could not join " + name : error);
            }
            friendJoinBusy = false;
        });
    }

    void ServerBrowser::JoinLanGame(const ui::ServerEntry& entry) {
        if (!activeIsMicrosoft()) {
            SetJoinMessage("Sign in with a Microsoft account to join a LAN game");
            return;
        }
        std::shared_ptr<lan::Session> session;
        {
            std::lock_guard lock(lanSessionMutex);
            session = lanSession;
        }
        if (!session || !session->Alive()) {
            SetJoinMessage("Refresh the LAN list first");
            return;
        }
        if (friendJoinBusy.exchange(true))
            return;
        BeginLeaseJoin("Joining " + entry.name + "...");
        const std::string authPath = activeAuthPath();
        const uint64_t id = std::strtoull(entry.xuid.c_str() + 4, nullptr, 10);
        const std::string name = entry.name, version = entry.version;
        tasks.Run([this, authPath, id, name, version, session] {
            xbox::Account acct(authPath);
            std::string error;
            xbox::ServiceInfo info;
            if (acct.SignInSaved(error) && acct.ServiceSession(kBedrockVersion, info, error)) {
                auto lease = std::make_shared<friendworld::Lease>();
                lease->signaling = std::make_shared<SharedSignaling>(session);
                lease->hostNetworkId = std::to_string(id);
                lease->issuer = info.issuer;
                lease->hostName = name;
                lease->version = version;
                std::lock_guard lock(tabMutex);
                friendLeaseReady = std::move(lease);
            } else {
                Log("lan", "join failed: {}", error);
                FailLeaseJoin(error.empty() ? "Could not join " + name : error);
            }
            friendJoinBusy = false;
        });
    }

    void ServerBrowser::ReleaseFriendLease() {
        std::shared_ptr<friendworld::Lease> lease = std::move(friendLease);
        friendLease.reset();
        if (!lease)
            return;
        tasks.Run([lease = std::move(lease)]() mutable { lease.reset(); });
    }

    void ServerBrowser::Poll(ui::TitleModel& title) {
        std::string joinAddress;
        std::shared_ptr<friendworld::Lease> joinedFriend;
        {
            std::lock_guard lock(tabMutex);
            joinedFriend = std::move(friendLeaseReady);
            title.featured = tabFeatured;
            if (tabDirty) {
                title.lan = tabLan;
                title.realms = tabRealms;
                tabDirty = false;
            }
            RebuildFriendSections(title);
            title.tabMessage = title.tab == ui::ServerTab::Lan      ? tabLanMsg
                               : title.tab == ui::ServerTab::Realms ? tabRealmsMsg
                                                                    : tabFriendsMsg;
            title.joinMessage = tabJoinMsg;
            if (realmJoinReady) {
                realmJoinReady = false;
                joinAddress = realmJoinAddress;
                tabJoinMsg.clear();
            }
        }
        if (joinedFriend) {
            ReleaseFriendLease();
            friendLease = std::move(joinedFriend);
            SetJoinMessage({});
            hideTitle();
            connect();
        }
        if (!joinAddress.empty())
            JoinServer(joinAddress);
        if (realmAddedFlag.exchange(false)) {
            title.page = ui::TitlePage::Servers;
            title.tab = ui::ServerTab::Realms;
            title.selected = -1;
            RefreshTab(ui::ServerTab::Realms);
        }
    }

    void ServerBrowser::RebuildFriendSections(ui::TitleModel& title) {
        static constexpr const char* kNames[3] = {"Hosting", "Online", "Offline"};
        std::vector<ui::ServerEntry> sections[3];
        for (const ui::ServerEntry& f : tabFriends)
            sections[f.hosting ? 0 : (f.offline ? 2 : 1)].push_back(f);
        title.friends.clear();
        if (tabFriends.empty())
            return;
        for (int i = 0; i < 3; i++) {
            title.friends.push_back(SectionHeader(kNames[i], sections[i].size(), i));
            if (title.friendsOpen[i])
                title.friends.insert(title.friends.end(), sections[i].begin(), sections[i].end());
        }
    }

    void ServerBrowser::PollLeaseJoin() {
        if (!leaseJoining)
            return;
        std::shared_ptr<friendworld::Lease> lease;
        std::string address;
        {
            std::lock_guard lock(tabMutex);
            lease = std::move(friendLeaseReady);
            if (realmJoinReady) {
                realmJoinReady = false;
                address = realmJoinAddress;
            }
        }
        if (lease) {
            leaseJoining = false;
            setStatusHint({});
            ReleaseFriendLease();
            friendLease = std::move(lease);
            connect();
        } else if (!address.empty()) {
            leaseJoining = false;
            setStatusHint({});
            JoinServer(address);
        }
    }

    std::string ServerBrowser::RealmAddress() const {
        std::lock_guard lock(tabMutex);
        return realmJoinAddress;
    }

    ui::ServerEntry ServerBrowser::PingResult(const std::string& address) const {
        std::lock_guard lock(pingMutex);
        const auto it = pingResults.find(address);
        return it == pingResults.end() ? ui::ServerEntry{} : it->second;
    }

    void ServerBrowser::ForgetPing(const std::string& address) {
        std::lock_guard lock(pingMutex);
        pingResults.erase(address);
    }
}
