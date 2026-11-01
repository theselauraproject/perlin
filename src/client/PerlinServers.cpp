#include "client/PerlinInternal.hpp"
#include "util/Log.hpp"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace perlin {
    namespace {
        std::string StripFormatting(const std::string& text) {
            std::string out;
            for (size_t i = 0; i < text.size(); i++) {
                if (static_cast<unsigned char>(text[i]) == 0xC2 && i + 1 < text.size() &&
                    static_cast<unsigned char>(text[i + 1]) == 0xA7) {
                    i += 2;
                    continue;
                }
                out += text[i];
            }
            return out;
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
    }

    void Perlin::FetchMissingGamertags() {
        if (!workers)
            return;
        signIn.gamertagFetchAt = std::chrono::steady_clock::now();
        for (const AccountEntry& e : accounts.Entries()) {
            if (!e.microsoft || !e.name.empty())
                continue;
            workers->Submit([this, id = e.id, path = accounts.AuthPath(e)] {
                xbox::Account acct(path);
                std::string error, gamertag;
                if (!acct.HasSavedLogin() || !acct.SignInSaved(error) ||
                    !acct.FetchGamertag(gamertag, error)) {
                    if (!error.empty())
                        Log("auth", "couldn't learn an account's gamertag: {}", error);
                    return;
                }
                std::lock_guard lock(signIn.mutex);
                signIn.pendingRenames.emplace_back(id, gamertag);
            });
        }
    }

    void Perlin::ApplyActiveAccount() {
        const AccountEntry* a = accounts.Active();
        if (!a)
            return;
        options.onlineLogin = a->microsoft;
        if (!a->microsoft)
            options.playerName = a->name;
        onlineThisRun = options.onlineLogin && !args.offline;
        if (a->skinKnown && (a->skin != options.skin || a->cape != options.cape)) {
            options.skin = a->skin;
            options.cape = a->cape;
            SkinConfig cfg = MakeSkinConfig(options.skin, options.cape);
            LoadedSkin loaded;
            std::string err;
            if (LoadSkinConfig(cfg, loaded, err))
                options.slimArms = loaded.slimArms;
            else
                Log("accounts", "skin: {}", err);
            wardrobe.init = false;
        } else if (!a->skinKnown) {
            accounts.SetSkin(a->id, options.skin, options.cape);
        }
        options.Save();
        PrewarmSignIn();
    }

    // the Xbox Live / PlayFab / Minecraft token round trips take seconds: do them while the player is in the menus
    void Perlin::PrewarmSignIn() {
        if (!onlineThisRun || PERLIN_ENV("PERLIN_PREVIEW"))
            return;
        auto account = std::make_shared<xbox::Account>(ActiveAuthPath());
        if (account->HasSavedLogin())
            BedrockSession::PrewarmLogin(std::move(account), ActiveAuthPath());
    }

    std::string Perlin::ActiveAuthPath() const {
        const AccountEntry* a = accounts.Active();
        return a && a->microsoft ? accounts.AuthPath(*a) : paths::DataDir() + "/auth.json";
    }

    void Perlin::StartAccountSignIn() {
        CancelAccountSignIn();
        std::lock_guard lock(signIn.mutex);
        signIn.authFile = accounts.NewAuthFile();
        signIn.account = std::make_shared<xbox::Account>(paths::DataDir() + "/" + signIn.authFile);
        signIn.url.clear();
        signIn.code.clear();
        signIn.status.clear();
        signIn.gamertag.clear();
#ifdef __EMSCRIPTEN__
        if (!http::WebRelayReady()) {
            signIn.status =
                "Microsoft sign-in in the browser needs the site's sign-in relay (see HOSTING.txt)";
            signIn.state = 3;
            return;
        }
#endif
        signIn.state = 1;
        workers->Submit([this, acct = signIn.account] {
            std::string error, gamertag;
            const bool ok = acct->SignIn(
                [this, acct](const std::string& url, const std::string& code) {
                    std::lock_guard lock(signIn.mutex);
                    if (signIn.account != acct)
                        return;
                    signIn.url = url;
                    signIn.code = code;
                    signIn.status = "Waiting for you to sign in...";
                    platform->ShowSignInCode(url, code);
                },
                error);
            const bool named = ok && acct->FetchGamertag(gamertag, error);
            std::lock_guard lock(signIn.mutex);
            if (signIn.account != acct)
                return;
            if (ok) {
                signIn.gamertag = named ? gamertag : std::string();
                signIn.state = 2;
            } else {
                signIn.status = "Sign-in failed: " + error;
                signIn.state = 3;
            }
        });
    }

    void Perlin::CancelAccountSignIn() {
        std::lock_guard lock(signIn.mutex);
        if (!signIn.account)
            return;
        signIn.account->Cancel();
        signIn.account.reset();
        if (signIn.state != 2 && !signIn.authFile.empty()) {
            std::error_code ec;
            std::filesystem::remove(paths::DataDir() + "/" + signIn.authFile, ec);
        }
        signIn.state = 0;
    }

    void Perlin::PingServers(const std::vector<std::string>& addresses) {
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
                const bool parsed = detail::ParseServer(text, host, port);
                const bool ok = parsed && RakNet::Ping(host, port, ad, error, 2500);
                NetherNetServerInfo info;
                const bool nether = parsed && !ok && NetherNet::Probe(host, port, info, error, 3);
                r.pingMs = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
                r.status = ok || nether ? ui::ServerEntry::Status::Online : ui::ServerEntry::Status::Offline;
                if (nether) {
                    r.motd = StripFormatting(info.name);
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
                        r.motd = StripFormatting(f[1]);
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

    void Perlin::JoinSingleplayer() {
#ifdef PERLIN_SINGLEPLAYER
        ReleaseFriendLease();
        if (!singleplayerServer)
            singleplayerServer = std::make_unique<EmbeddedServer>();
        if (!singleplayerServer->Running()) {
            const std::string dir = paths::DataDir() + "/singleplayer";
            paths::EnsureDir(dir);
            // the server reads its block tables from files: write the copies the client carries
            for (const char* name : {"block_states.json", "block_destroy.tsv", "extra_items.txt"}) {
                std::string bytes;
                const std::string path = dir + "/" + name;
                std::error_code ec;
                if (resources::Read(name, bytes) && (!std::filesystem::exists(path, ec) ||
                                                     std::filesystem::file_size(path, ec) != bytes.size()))
                    std::ofstream(path, std::ios::binary) << bytes;
            }
            // the browser has a fixed pool of worker threads shared with the client: the world generator gets two
#ifdef __EMSCRIPTEN__
            const std::string threads = ",\"max_threads\":2";
#else
            const std::string threads;
#endif
            const std::string config =
                "{\"view_distance\":" + std::to_string(std::clamp(options.renderDistance, 2, 32)) + threads +
                ",\"block_data_dir\":\"" + dir + "\"}";
            std::string error;
            if (!singleplayerServer->Start(dir, config, error)) {
                Log("singleplayer", "{}", error);
                SetStatusHint("Singleplayer could not start: " + error);
                return;
            }
        }
        pendingSingleplayer = singleplayerServer->Connect();
        if (!pendingSingleplayer) {
            SetStatusHint("Singleplayer could not open a connection.");
            return;
        }
        titleScreen.active = false;
        Connect();
#else
        SetStatusHint("This build has no singleplayer: it was built without PERLIN_BUILD_SERVER.");
#endif
    }

    void Perlin::JoinServer(const std::string& address) {
        if (address.empty())
            return;
        ReleaseFriendLease();
        options.server = address;
        options.Save();
        titleScreen.active = false;
        Connect();
    }

    void Perlin::SetJoinMessage(const std::string& text) {
        if (leaseJoining) {
            SetStatusHint(text);
            return;
        }
        std::lock_guard lock(tabMutex);
        tabJoinMsg = text;
    }

    void Perlin::BeginLeaseJoin(const std::string& text) {
        leaseJoining = true;
        SetStatusHint(text);
        titleScreen.active = false;
    }

    void Perlin::FailLeaseJoin(const std::string& text) {
        const bool loading = leaseJoining.exchange(false);
        SetStatusHint({});
        {
            std::lock_guard lock(tabMutex);
            tabJoinMsg = text;
        }
        if (loading)
            leaseJoinFailed = true;
    }

    void Perlin::RefreshTab(ui::ServerTab tab) {
        using T = ui::ServerTab;
        const bool microsoft = accounts.Active() && accounts.Active()->microsoft;
        const std::string authPath = ActiveAuthPath();
        if (tab == T::Lan) {
            if (lanBusy.exchange(true))
                return;
            {
                std::lock_guard lock(tabMutex);
                tabLan.clear();
                tabLanMsg = "Scanning for games on your local network";
                tabDirty = true;
            }
            std::thread([this] {
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
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
                    if (ifaddrs* list = nullptr; getifaddrs(&list) == 0) {
                        for (ifaddrs* a = list; a; a = a->ifa_next) {
                            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET ||
                                (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP))
                                continue;
                            const uint32_t ip =
                                ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
                            for (uint32_t last = 1; last < 255; last++) {
                                const uint32_t host = (ip & 0xFFFFFF00u) | last;
                                char text[32];
                                std::snprintf(text, sizeof text, "%u.%u.%u.%u", host >> 24,
                                              (host >> 16) & 255, (host >> 8) & 255, host & 255);
                                hosts.push_back(text);
                            }
                        }
                        freeifaddrs(list);
                    }
#endif
                    std::sort(hosts.begin(), hosts.end());
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
                            if (std::none_of(rows.begin(), rows.end(), [&](const ui::ServerEntry& r) {
                                    return r.address == e.address;
                                }))
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
            }).detach();
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
        std::thread([this, forRealms, authPath, &busy] {
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
                            if (f.pictureUrl.empty() || ui::UiAssets::Instance().HasHead(key))
                                continue;
                            const http::Response pic = http::GetUrl(
                                f.pictureUrl + (!f.pictureUrl.contains('?') ? "?w=64&h=64" : ""), {}, 8);
                            Texture t;
                            if (pic.status == 200 &&
                                LoadTextureMemory(reinterpret_cast<const unsigned char*>(pic.body.data()),
                                                  pic.body.size(), t))
                                ui::UiAssets::Instance().SetHead(key, std::move(t));
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
                    if (ui::UiAssets::Instance().HasHead("gp:" + f.xuid))
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
        }).detach();
    }

    void Perlin::RefreshFeatured(bool force) {
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            std::lock_guard lock(tabMutex);
            tabFeatured.clear();
            return;
        }
        if (!force && featuredAt != Clock::time_point{} &&
            Clock::now() - featuredAt < std::chrono::minutes(10))
            return;
        if (featuredBusy.exchange(true))
            return;
        const std::string authPath = ActiveAuthPath();
        std::thread([this, authPath] {
            xbox::Account acct(authPath);
            std::string error;
            std::vector<featured::Server> list;
            const bool ok = acct.SignInSaved(error) && featured::List(acct, list, error);
            if (!ok)
                Log("featured", "{}", error);
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
                const std::string key = "fs:" + e.address;
                if (!f.logoUrl.empty() && !ui::UiAssets::Instance().HasHead(key)) {
                    const http::Response pic = http::GetUrl(f.logoUrl, {}, 8);
                    Texture t;
                    if (pic.status == 200 &&
                        LoadTextureMemory(reinterpret_cast<const unsigned char*>(pic.body.data()),
                                          pic.body.size(), t))
                        ui::UiAssets::Instance().SetHead(key, std::move(t));
                }
                if (ui::UiAssets::Instance().HasHead(key))
                    e.icon = key;
                rows.push_back(std::move(e));
            }
            {
                std::lock_guard lock(tabMutex);
                if (ok) {
                    tabFeatured = std::move(rows);
                    featuredAt = Clock::now();
                    featuredPing = true;
                }
            }
            featuredBusy = false;
        }).detach();
    }

    void Perlin::AddRealm(const std::string& code) {
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            SetJoinMessage("Sign in with a Microsoft account to add a Realm");
            return;
        }
        if (realmAddBusy.exchange(true))
            return;
        SetJoinMessage("Adding the Realm...");
        const std::string authPath = ActiveAuthPath();
        std::thread([this, code, authPath] {
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
        }).detach();
    }

    void Perlin::JoinRealm(long long id) {
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            SetJoinMessage("Sign in with a Microsoft account to join a Realm");
            return;
        }
        if (realmJoinBusy.exchange(true))
            return;
        BeginLeaseJoin("Finding your Realm...");
        const std::string authPath = ActiveAuthPath();
        std::thread([this, id, authPath] {
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
        }).detach();
    }

    void Perlin::JoinFriend(const ui::ServerEntry& entry) {
        if (!entry.hosting) {
            SetJoinMessage(entry.name +
                           (entry.offline ? " is offline" : " isn't hosting a world you can join"));
            return;
        }
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            SetJoinMessage("Sign in with a Microsoft account to join a friend");
            return;
        }
        if (friendJoinBusy.exchange(true))
            return;
        BeginLeaseJoin("Joining " + entry.name + "...");
        const std::string authPath = ActiveAuthPath();
        const std::string xuid = entry.xuid;
        const std::string name = entry.name;
        std::thread([this, authPath, xuid, name] {
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
        }).detach();
    }

    void Perlin::JoinLanGame(const ui::ServerEntry& entry) {
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
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
        const std::string authPath = ActiveAuthPath();
        const uint64_t id = std::strtoull(entry.xuid.c_str() + 4, nullptr, 10);
        const std::string name = entry.name, version = entry.version;
        std::thread([this, authPath, id, name, version, session] {
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
        }).detach();
    }

    void Perlin::ReleaseFriendLease() {
        std::shared_ptr<friendworld::Lease> lease = std::move(friendLease);
        friendLease.reset();
        if (!lease)
            return;
        std::thread([lease = std::move(lease)]() mutable { lease.reset(); }).detach();
    }
}
