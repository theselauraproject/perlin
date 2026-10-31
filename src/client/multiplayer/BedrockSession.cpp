#include "util/Paths.hpp"
#include "util/Env.hpp"
#include "client/multiplayer/BedrockSession.hpp"

#include "client/mods/ModManager.hpp"
#include "util/SimClock.hpp"
#include "util/SystemInfo.hpp"
#include "client/multiplayer/NbtJson.hpp"
#include "client/multiplayer/PacketLog.hpp"
#include "client/multiplayer/DeviceProfile.hpp"
#include "client/multiplayer/DefaultSkinGeometry.hpp"

#include "network/ClientIdentity.hpp"

#include <openssl/ec.h>
#include "network/NetherNet.hpp"
#include "network/RakNet.hpp"
#include "network/VarInt.hpp"
#include "network/LoginClaims.hpp"
#include "network/LoginBuilder.hpp"
#include "network/Uuid.hpp"
#include "protocol/Packets.hpp"
#include "util/Json.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/chunk/ChunkCodec.hpp"
#include "client/Options.hpp"

#include <miniz.h>

#include <algorithm>
#include <unordered_set>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <streambuf>
#include <thread>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        // a finished Minecraft login, reusable while its token lives: the token and chain are bound to this key pair
        struct CachedLogin {
            std::shared_ptr<EC_KEY> key;
            xbox::Credentials creds;
            int64_t expires = 0;  // unix seconds
            std::shared_future<bool> pending;
        };
        std::mutex gLoginMutex;
        std::map<std::string, CachedLogin> gLogins;

        int64_t TokenExpiry(const std::string& jwt) {
            const size_t a = jwt.find('.');
            const size_t b = a == std::string::npos ? a : jwt.find('.', a + 1);
            if (b == std::string::npos)
                return 0;
            const auto payload = json::Document::Parse(B64UrlDecode(jwt.substr(a + 1, b - a - 1)));
            return payload ? static_cast<int64_t>(payload->root()["exp"].number(0)) : 0;
        }

        int64_t NowSeconds() {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        // 5 minutes of margin: a token about to lapse could expire during the join
        bool Fresh(const CachedLogin& c) { return c.key && c.expires - NowSeconds() > 300; }

        void StoreLogin(const std::string& loginKey, EC_KEY* key, const xbox::Credentials& creds) {
            if (loginKey.empty() || !key)
                return;
            EC_KEY_up_ref(key);
            std::lock_guard lock(gLoginMutex);
            CachedLogin& c = gLogins[loginKey];
            c.key = std::shared_ptr<EC_KEY>(key, EC_KEY_free);
            c.creds = creds;
            c.expires = TokenExpiry(creds.multiplayerToken);
        }
    }


    using proto::LevelChunkPacket;
    using proto::PlayerAuthInputPacket;
    using proto::SubChunkEntryEntry;
    using proto::SubChunkEntryEntryResult;
    using proto::SubchunkPacket;
    using proto::SubchunkRequestPacket;

    namespace {
        bool IsRequestMode(const LevelChunkPacket& p) { return p.highestSubchunkCount.has_value(); }
        int RequestSlots(const LevelChunkPacket& p, int dimensionSlots) {
            if (!p.highestSubchunkCount)
                return 0;
            const int32_t limit = *p.highestSubchunkCount;
            if (limit < 0)
                return dimensionSlots;
            return std::min(limit + 1, dimensionSlots);
        }
    }

    BedrockSession::BedrockSession() {
        RegisterPacketHandlers();
        StartChunkWorker();
    }
    BedrockSession::~BedrockSession() {
        Disconnect();
        StopChunkWorker();
    }

    void BedrockSession::StartChunkWorker() {
        chunkStop = false;
        if (!decodePool)
            decodePool = std::make_unique<ThreadPool>(std::max(1u, HardwareThreads() / 2), true);
        chunkThread = std::thread([this] { ChunkWorkerMain(); });
    }

    void BedrockSession::StopChunkWorker() {
        {
            std::lock_guard<std::mutex> lk(chunkMutex);
            chunkStop = true;
        }
        chunkCv.notify_all();
        if (chunkThread.joinable())
            chunkThread.join();
        if (decodePool)
            decodePool->Stop();
    }

    void BedrockSession::ChunkWorkerMain() {
        std::deque<std::pair<uint32_t, std::vector<uint8_t>>> batch;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(chunkMutex);
                chunkBusy = false;
                chunkCv.wait(lk, [&] { return chunkStop || !chunkIn.empty(); });
                if (chunkStop)
                    return;
                batch.swap(chunkIn);
                chunkBusy = true;
            }
            for (auto& [id, body] : batch)
                dispatcher.Dispatch(id, body.data(), body.size());
            batch.clear();
        }
    }

    namespace {

        bool SnappyCompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
            out.clear();
            size_t n = in.size();
            do {
                uint8_t b = n & 0x7F;
                n >>= 7;
                out.push_back(n ? (b | 0x80) : b);
            } while (n);
            const auto literal = [&](size_t from, size_t to) {
                while (from < to) {
                    const size_t chunk = std::min<size_t>(to - from, 65536);
                    const size_t l = chunk - 1;
                    if (l < 60) {
                        out.push_back(static_cast<uint8_t>(l << 2));
                    } else if (l < 256) {
                        out.push_back(60 << 2);
                        out.push_back(static_cast<uint8_t>(l));
                    } else {
                        out.push_back(61 << 2);
                        out.push_back(static_cast<uint8_t>(l & 0xFF));
                        out.push_back(static_cast<uint8_t>(l >> 8));
                    }
                    out.insert(out.end(), in.begin() + from, in.begin() + from + chunk);
                    from += chunk;
                }
            };
            const auto copy = [&](size_t offset, size_t len) {
                while (len > 0) {
                    const size_t l = len >= 68 ? 64 : (len > 64 ? 60 : len);
                    out.push_back(static_cast<uint8_t>(((l - 1) << 2) | 2));
                    out.push_back(static_cast<uint8_t>(offset & 0xFF));
                    out.push_back(static_cast<uint8_t>(offset >> 8));
                    len -= l;
                }
            };
            constexpr size_t kBlock = 65536;
            const size_t total = in.size();
            for (size_t base = 0; base < total; base += kBlock) {
                const size_t end = std::min(total, base + kBlock);
                std::vector<int32_t> table(1 << 14, -1);
                const auto load = [&](size_t i) {
                    return static_cast<uint32_t>(in[i]) | (static_cast<uint32_t>(in[i + 1]) << 8) | (static_cast<uint32_t>(in[i + 2]) << 16) |
                           (static_cast<uint32_t>(in[i + 3]) << 24);
                };
                size_t pos = base, lit = base;
                while (pos + 4 <= end) {
                    const uint32_t h = (load(pos) * 0x1e35a7bdu) >> 18;
                    const int32_t cand = table[h];
                    table[h] = static_cast<int32_t>(pos);
                    if (cand >= static_cast<int32_t>(base) && load(static_cast<size_t>(cand)) == load(pos)) {
                        size_t len = 4;
                        while (pos + len < end && in[cand + len] == in[pos + len])
                            len++;
                        literal(lit, pos);
                        copy(pos - static_cast<size_t>(cand), len);
                        pos += len;
                        lit = pos;
                    } else {
                        pos++;
                    }
                }
                literal(lit, end);
            }
            return true;
        }

        bool SnappyDecompress(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
            out.clear();
            size_t p = 0;
            uint64_t total = 0;
            int shift = 0;
            while (true) {
                if (p >= inLen || shift > 35)
                    return false;
                const uint8_t b = in[p++];
                total |= static_cast<uint64_t>(b & 0x7F) << shift;
                shift += 7;
                if (!(b & 0x80))
                    break;
            }
            if (total > (1u << 28))
                return false;
            out.reserve(total);
            while (p < inLen) {
                const uint8_t tag = in[p++];
                const int type = tag & 3;
                if (type == 0) {
                    size_t len = tag >> 2;
                    if (len >= 60) {
                        const int extra = static_cast<int>(len) - 59;
                        if (p + extra > inLen)
                            return false;
                        len = 0;
                        for (int i = 0; i < extra; i++)
                            len |= static_cast<size_t>(in[p + i]) << (8 * i);
                        p += extra;
                    }
                    len += 1;
                    if (p + len > inLen)
                        return false;
                    out.insert(out.end(), in + p, in + p + len);
                    p += len;
                } else {
                    size_t len, offset;
                    if (type == 1) {
                        if (p >= inLen)
                            return false;
                        len = 4 + ((tag >> 2) & 7);
                        offset = (static_cast<size_t>(tag >> 5) << 8) | in[p++];
                    } else if (type == 2) {
                        if (p + 2 > inLen)
                            return false;
                        len = 1 + (tag >> 2);
                        offset = in[p] | (static_cast<size_t>(in[p + 1]) << 8);
                        p += 2;
                    } else {
                        if (p + 4 > inLen)
                            return false;
                        len = 1 + (tag >> 2);
                        offset = in[p] | (static_cast<size_t>(in[p + 1]) << 8) | (static_cast<size_t>(in[p + 2]) << 16) |
                                 (static_cast<size_t>(in[p + 3]) << 24);
                        p += 4;
                    }
                    if (offset == 0 || offset > out.size())
                        return false;
                    for (size_t i = 0; i < len; i++)
                        out.push_back(out[out.size() - offset]);
                }
                if (out.size() > total)
                    return false;
            }
            return out.size() == total;
        }

        bool RawDeflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
            out.clear();
            mz_stream strm = {};
            if (mz_deflateInit2(&strm, MZ_DEFAULT_LEVEL, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9,
                                MZ_DEFAULT_STRATEGY) != MZ_OK)
                return false;
            strm.next_in = in.data();
            strm.avail_in = (unsigned)in.size();
            uint8_t chunk[65536];
            int rc;
            do {
                strm.next_out = chunk;
                strm.avail_out = sizeof(chunk);
                rc = mz_deflate(&strm, MZ_FINISH);
                if (rc != MZ_OK && rc != MZ_STREAM_END) {
                    mz_deflateEnd(&strm);
                    return false;
                }
                out.insert(out.end(), chunk, chunk + (sizeof(chunk) - strm.avail_out));
            } while (rc != MZ_STREAM_END);
            mz_deflateEnd(&strm);
            return true;
        }

        bool RawInflate(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
            out.clear();
            mz_stream strm = {};
            if (mz_inflateInit2(&strm, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
                return false;
            strm.next_in = in;
            strm.avail_in = (unsigned)inLen;
            uint8_t chunk[65536];
            int rc;
            do {
                strm.next_out = chunk;
                strm.avail_out = sizeof(chunk);
                rc = mz_inflate(&strm, MZ_SYNC_FLUSH);
                if (rc != MZ_OK && rc != MZ_STREAM_END) {
                    mz_inflateEnd(&strm);
                    return false;
                }
                out.insert(out.end(), chunk, chunk + (sizeof(chunk) - strm.avail_out));
                if (out.size() > 64 * 1024 * 1024) {
                    mz_inflateEnd(&strm);
                    return false;
                }
            } while (rc != MZ_STREAM_END);
            mz_inflateEnd(&strm);
            return true;
        }
    }

    const char* BedrockSession::PacketName(uint32_t id) {
        return proto::PacketName(id);
    }

    namespace {

        std::optional<proto::LoginPacket> BuildOfflineLogin(int protocol, const std::string& playerName,
                                                            const std::string& gameVersion,
                                                            const std::string& serverAddress,
                                                            ClientIdentity& identity, int maxViewDistance,
                                                            const LoadedSkin& loadedSkin,
                                                            const xbox::Credentials* online, const std::string& nonce,
                                                            std::string& error) {
            if (!identity.Valid() || identity.MultiplayerToken().empty()) {
                error = "no client identity";
                return std::nullopt;
            }
            std::string cpk = identity.PublicKeyB64();
            if (cpk.empty()) {
                error = "no identity public key";
                return std::nullopt;
            }

            auto signJwt = [&](const auto& header, const auto& payload) -> std::string {
                std::string h = B64Url(json::Write(header));
                std::string p = B64Url(json::Write(payload));
                std::vector<uint8_t> raw;
                if (!identity.Sign(h + "." + p, raw))
                    return {};
                return h + "." + p + "." + B64Url(raw.data(), raw.size());
            };

            const std::string identityJson = BuildIdentityJson(identity, online ? &online->chainJson : nullptr, error, playerName);
            if (identityJson.empty())
                return std::nullopt;

            const std::string skinData = loadedSkin.hasSkin()
                                             ? std::string(loadedSkin.skinRgba.begin(), loadedSkin.skinRgba.end())
                                             : std::string(64 * 64 * 4, '\xFF');
            std::random_device rd;
            const DeviceProfile device = LoadDeviceProfile();
            const int64_t clientRandomId = static_cast<int64_t>(((static_cast<uint64_t>(rd()) << 32) | rd()) >> 1);
            claims::ClientData skin;
            skin.ClientRandomId = clientRandomId;
            skin.MaxViewDistance = maxViewDistance;
            skin.DeviceId = device.id;
            skin.GameVersion = gameVersion;
            skin.SelfSignedId = DeriveSelfSignedId(clientRandomId, device.id);
            skin.SkinId = std::string("c18e65aa-7b21-4637-9b63-8ad63622ef01.") + (loadedSkin.slimArms ? "CustomSlim" : "Custom");
            skin.SkinColor = "#b37b62";
            skin.ServerAddress = serverAddress;
            skin.Nonce = nonce;
            skin.SkinData = B64Standard(skinData);
            skin.ThirdPartyName = playerName;
            if (loadedSkin.hasSkin()) {
                skin.SkinImageWidth = loadedSkin.skinWidth;
                skin.SkinImageHeight = loadedSkin.skinHeight;
                skin.SkinId = loadedSkin.skinId;
            }
            const char* geometry = loadedSkin.slimArms ? "geometry.humanoid.customSlim" : "geometry.humanoid.custom";
            skin.ArmSize = loadedSkin.slimArms ? "slim" : "wide";
            skin.SkinGeometryData = kDefaultSkinGeometryB64;
            skin.SkinGeometryDataEngineVersion = B64Standard("0.0.0");
            skin.SkinResourcePatch = B64Standard(std::string("{\"geometry\":{\"default\":\"") + geometry + "\"}}");
            if (loadedSkin.hasCape()) {
                skin.CapeData = B64Standard(std::string(loadedSkin.capeRgba.begin(), loadedSkin.capeRgba.end()));
                skin.CapeImageWidth = loadedSkin.capeWidth;
                skin.CapeImageHeight = loadedSkin.capeHeight;
                skin.CapeId = loadedSkin.capeId;
                skin.CapeOnClassicSkin = true;
            }
            skin.DeviceOS = 1;
            skin.DeviceModel = device.model;
            skin.PlatformType = 1;
            skin.CurrentInputMode = 1;
            skin.DefaultInputMode = 2;
            {
                const uint64_t gib = sysinfo::TotalMemory() >> 20;
                skin.MemoryTier = gib < 1536 ? 0 : gib < 2048 ? 1 : gib < 4096 ? 2 : gib < 8192 ? 3 : 4;
            }
            const claims::Header skinHeader{.x5u = cpk};
            std::string skinJwt = signJwt(skinHeader, skin);
            if (skinJwt.empty()) {
                error = "skin JWT failed";
                return std::nullopt;
            }

            if (online) {
                auto part = [](const std::string& jwt, size_t index) {
                    size_t from = 0;
                    for (size_t i = 0; i < index; i++) {
                        from = jwt.find('.', from);
                        if (from == std::string::npos)
                            return std::string();
                        from++;
                    }
                    const size_t to = jwt.find('.', from);
                    return to == std::string::npos ? std::string() : B64UrlDecode(jwt.substr(from, to - from));
                };
                std::string tokenInfo = "unreadable", chainInfo = "unreadable";
                if (auto t = json::Document::Parse(part(identity.MultiplayerToken(), 1))) {
                    const json::View r = t->root();
                    tokenInfo = "iss=" + r["iss"].str() + " aud=" + r["aud"].str() + " xid=" + r["xid"].str() +
                                " xname=" + r["xname"].str() + " mid=" + r["mid"].str() + " ipt=" + r["ipt"].str() +
                                " cpk " + (r["cpk"].str() == cpk ? "matches" : "DIFFERS FROM") + " our key";
                }
                if (auto c = json::Document::Parse(online->chainJson)) {
                    chainInfo.clear();
                    int links = 0;
                    for (const json::View link : c->root()["chain"].items()) {
                        links++;
                        if (auto p = json::Document::Parse(part(link.str(), 1))) {
                            const json::View extra = p->root()["extraData"];
                            if (extra["XUID"].isString())
                                chainInfo += "xuid=" + extra["XUID"].str() + " name=" + extra["displayName"].str() +
                                             " titleId=" + extra["titleId"].str() + " ";
                        }
                    }
                    chainInfo += "(" + std::to_string(links) + " links)";
                }
                Log("login", "multiplayer token: {}", tokenInfo);
                Log("login", "certificate chain: {}", chainInfo);
                Log("login", "client data: DeviceOS={} model='{}' deviceId={} playFabId={} selfSignedId={} thirdPartyName={} serverAddress={} gameVersion={} inputMode={}/{} platformType={}", skin.DeviceOS, skin.DeviceModel, skin.DeviceId, skin.PlayFabId, skin.SelfSignedId, skin.ThirdPartyName, skin.ServerAddress, skin.GameVersion, skin.CurrentInputMode, skin.DefaultInputMode, skin.PlatformType);
            }
            proto::LoginPacket login;
            login.protocolVersion = protocol;
            login.tokens.identity = identityJson;
            login.tokens.client = skinJwt;
            return login;
        }

    }

    bool BedrockSession::Connect(const std::string& host, uint16_t port) { return ConnectImpl(host, port, false); }

    void BedrockSession::SetDisconnectInfo(int reason, const std::string& message, bool lost) {
        std::lock_guard<std::mutex> lk(disconnectMutex);
        if (disconnectInfo.happened && !lost)
            return;
        disconnectInfo.happened = true;
        disconnectInfo.reason = reason;
        disconnectInfo.message = message;
        disconnectInfo.connectionLost = lost;
    }

    void BedrockSession::SetOutdated(int who) {
        const int theirs = net ? net->Info().advertisedProtocol : 0;
        // plain version mismatch doesn't say which side is behind; the ping reply's protocol does
        if (who == 0 && theirs > 0 && theirs != kBedrockProtocol)
            who = theirs > kBedrockProtocol ? 1 : 2;
        if (who == 0)
            return;
        std::lock_guard<std::mutex> lk(disconnectMutex);
        disconnectInfo.happened = true;
        disconnectInfo.outdated = who;
        disconnectInfo.serverProtocol = theirs;
        disconnectInfo.serverVersion = net ? net->Info().advertisedVersion : std::string();
    }

    bool BedrockSession::RequestReconnect() {
        if (lastHost.empty() || transferring)
            return false;
        transferHost = lastHost;
        transferPort = lastPort;
        {
            std::lock_guard<std::mutex> lk(disconnectMutex);
            disconnectInfo = DisconnectInfo{};
        }
        transferRequested = true;
        return true;
    }

    bool BedrockSession::ConnectImpl(const std::string& host, uint16_t port, bool transfer) {
        lastHost = host;
        lastPort = port;
        {
            std::lock_guard<std::mutex> lk(disconnectMutex);
            disconnectInfo = DisconnectInfo{};
        }
        if (!transfer)
            Disconnect();
        lastError.clear();
        state = State::Connecting;

        {
            std::lock_guard lk(attrMutex);
            ownFlagWords = {};
            ownFlagWordsKnown = {};
            ownAttributes.clear();
            movementAttribute.reset();
        }
        {
            std::lock_guard lk(statusMutex);
            pendingMovementEffects.clear();
            pendingEffectUpdates.clear();
            serverMotions.clear();
        }

        {
            std::lock_guard<std::mutex> lk(txMutex);
            cipher.reset();
            net.reset();
        }
        packs.Reset();
        packsExpected = 0;
        if (transfer && identity && identity->Valid()) {
        } else {
        // an early or earlier sign-in for this account skips the whole login
        std::shared_ptr<EC_KEY> cachedKey;
        int64_t cachedMinutes = 0;
        // a sign-in still running in the background: connect meanwhile, take its result before the Login packet
        std::shared_future<bool> pendingLogin;
        if (account && !accountKey.empty()) {
            std::lock_guard lock(gLoginMutex);
            const CachedLogin& c = gLogins[accountKey];
            if (c.pending.valid() && c.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                pendingLogin = c.pending;
            else if (Fresh(c)) {
                cachedKey = c.key;
                onlineCreds = c.creds;
                cachedMinutes = (c.expires - NowSeconds()) / 60;
            }
        }
        identity = cachedKey ? std::make_unique<ClientIdentity>(cachedKey.get()) : std::make_unique<ClientIdentity>();
        if (!identity->Valid()) {
            lastError = "identity keygen failed";
            state = State::Failed;
            return false;
        }
        playerUuid = RandomUuid();
        if (cachedKey) {
            Log("auth", "reusing the sign-in from earlier ({} min left)", cachedMinutes);
            authTask = std::async(std::launch::deferred, [] { return std::make_pair(true, std::string()); });
        } else if (pendingLogin.valid()) {
            const std::string publicKey = identity->PublicKeyB64();
            ClientIdentity* identityPtr = identity.get();
            authTask = std::async(std::launch::async, [this, pendingLogin, publicKey, identityPtr] {
                pendingLogin.wait();
                {
                    std::lock_guard lock(gLoginMutex);
                    if (const CachedLogin& c = gLogins[accountKey]; Fresh(c)) {
                        onlineCreds = c.creds;
                        swapKey = c.key;
                        return std::make_pair(true, std::string());
                    }
                }
                std::string authError;
                const bool ok = account->LoginMinecraft(publicKey, "1.26.60", onlineCreds, authError);
                if (ok)
                    StoreLogin(accountKey, identityPtr->Key(), onlineCreds);
                return std::make_pair(ok, authError);
            });
        } else if (account) {
            constexpr const char* kServiceGameVersion = "1.26.60";
            ClientIdentity* identityPtr = identity.get();
            const std::string publicKey = identity->PublicKeyB64();
            authTask = std::async(std::launch::async, [this, publicKey, kServiceGameVersion, identityPtr] {
                std::string authError;
                const bool ok = account->LoginMinecraft(publicKey, kServiceGameVersion, onlineCreds, authError);
                if (ok)
                    StoreLogin(accountKey, identityPtr->Key(), onlineCreds);
                return std::make_pair(ok, authError);
            });
        } else if (identity->BuildMultiplayerToken(playerName, playerUuid).empty()) {
            lastError = "multiplayer token build failed";
            state = State::Failed;
            return false;
        }
        }
        const auto finishAuth = [this]() -> bool {
            if (!authTask.valid())
                return true;
            const auto result = authTask.get();
            if (!result.first) {
                lastError = "online login failed: " + result.second;
                state = State::Failed;
                return false;
            }
            if (swapKey) {
                identity = std::make_unique<ClientIdentity>(swapKey.get());
                swapKey.reset();
            }
            identity->SetMultiplayerToken(onlineCreds.multiplayerToken);
            if (!onlineCreds.gamertag.empty())
                playerName = onlineCreds.gamertag;
            return true;
        };
        bool useRak = transportMode == "raknet";
        if (friendLease) {
            useRak = false;
        } else if (transfer && transportMode == "auto") {
            useRak = usedRak;
        } else if (injectedTransport) {
            useRak = false;
        } else if (transportMode == "auto") {
            NetherNetServerInfo probe;
            std::string probeError;
            useRak = !NetherNet::Probe(host, port, probe, probeError, 3);
            Log("perlin", "transport: {}", (useRak ? "raknet (no NetherNet endpoint found)" : "nethernet"));
        }
        if (injectedTransport) {
            if (!finishAuth())
                return false;
            net = std::move(injectedTransport);
        } else if (!useRak) {
            if (!finishAuth())
                return false;
            auto nether = std::make_unique<NetherNet>();
            if (friendLease ? nether->ConnectFriend(*friendLease, *identity) : nether->Connect(host, port, *identity)) {
                net = std::move(nether);
            } else if (!friendLease && transportMode == "auto") {
                Log("perlin", "nethernet failed ({}), falling back to raknet", nether->LastError());
                useRak = true;
            } else {
                lastError = "transport: " + nether->LastError();
                state = State::Failed;
                return false;
            }
        }
        usedRak = useRak;
        if (useRak) {
            auto rak = std::make_unique<RakNet>();
            if (!rak->Connect(host, port)) {
                lastError = "transport: " + rak->LastError();
                state = State::Failed;
                return false;
            }
            net = std::move(rak);
        }
        serverProtocol = net->Info().protocol;
        if (serverProtocol <= 0) {
            lastError = "server reported no protocol version";
            state = State::Failed;
            return false;
        }

        serverAddress = friendLease ? friendLease->hostNetworkId : host + ":" + std::to_string(port);
        gameVersion = net->Info().version;
        if (gameVersion.empty())
            gameVersion = "1.26.60";
        if (!SendRequestNetworkSettings()) {
            lastError = "failed to send RequestNetworkSettings";
            state = State::Failed;
            return false;
        }
        state = State::WaitingNetworkSettings;
        if (!finishAuth())
            return false;

        auto start = std::chrono::steady_clock::now();
        uint64_t seenPackets = diagPackets;
        while (state != State::InGame && state != State::Failed) {
            Update();
            if (diagPackets != seenPackets) {
                seenPackets = diagPackets;
                start = std::chrono::steady_clock::now();
            }
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(60)) {
                lastError = "handshake timed out in state " + std::to_string((int)state.load());
                state = State::Failed;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return state == State::InGame;
    }

    void BedrockSession::Disconnect() {
        transferRequested = false;
        if (transferThread.joinable() && std::this_thread::get_id() != transferThreadId)
            transferThread.join();
        state = State::Disconnected;
        if (net)
            net->Disconnect();
        compressEnabled = false;
        cacheStatusSent = false;
        packsCompletedSent = false;
        radiusSent = false;
    }

    bool BedrockSession::Transmit(uint32_t packetId, const std::vector<uint8_t>& body, bool reliable) {
        if (replayMode)
            return true;
        packetlog::Record(true, packetId, body.data(), body.size());
        if ((packetId == 144 || packetId == 36 || packetId == 44 || packetId == 31) && tapActive.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> tl(tapMutex);
            if (recordTap)
                recordTap(1, packetId, body);
        }
        if (dropOutActive.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk(dropMutex);
            if (std::find(droppedOut.begin(), droppedOut.end(), packetId) != droppedOut.end())
                return true;
        }
        std::lock_guard<std::mutex> txLock(txMutex);
        if (!net || !net->IsConnected())
            return false;
        txPackets++;
        std::vector<uint8_t> content;
        wire::WriteVarUint32(content, static_cast<uint32_t>(body.size() + wire::VarUint32Size(packetId)));
        wire::WriteVarUint32(content, packetId);
        content.insert(content.end(), body.begin(), body.end());

        std::vector<uint8_t> batch;
        if (!compressEnabled) {
            batch = std::move(content);
        } else if (compressAlgorithm == 0xFFFF || content.size() < compressThreshold) {
            batch.push_back(0xFF);
            batch.insert(batch.end(), content.begin(), content.end());
        } else {
            std::vector<uint8_t> deflated;
            if (compressAlgorithm == 1) {
                SnappyCompress(content, deflated);
                batch.push_back(0x01);
            } else {
                if (!RawDeflate(content, deflated))
                    return false;
                batch.push_back(0x00);
            }
            batch.insert(batch.end(), deflated.begin(), deflated.end());
        }
        if (cipher && cipher->Active())
            batch = cipher->Encrypt(batch);
        return reliable ? net->SendReliable(batch) : net->SendUnreliable(batch);
    }

    void BedrockSession::PruneChunks(int centerX, int centerZ, int radius) {
        auto outside = [&](const ChunkPos& p) {
            return std::abs(p.x - centerX) > radius || std::abs(p.z - centerZ) > radius;
        };
        std::vector<ChunkPos> loaded;
        {
            std::lock_guard<std::mutex> lock(netLevel.mutex);
            for (const auto& entry : netLevel.chunks)
                if (outside(entry.first))
                    loaded.push_back(entry.first);
        }
        for (const ChunkPos& p : loaded) {
            netLevel.RemoveChunk(p.x, p.z);
            blockEntities.RemoveChunk(p.x, p.z);
            NoteModChunk(false, p.x, p.z);
        }
        std::lock_guard<std::mutex> lk(reqMutex);
        std::vector<ChunkPos> doomed = std::move(loaded);
        for (const auto& entry : columns)
            if (outside(entry.first))
                doomed.push_back(entry.first);
        for (const ChunkPos& p : doomed) {
            if (const auto found = columns.find(p); found != columns.end()) {
                const ColumnInfo& c = found->second;
                for (int i = 0; i < c.slots; i++)
                    requestedSlots.erase(std::make_tuple(c.dim, p.x, c.firstSubY + i, p.z));
                columns.erase(found);
            }
            seenColumns.erase({p.x, p.z});
            ForgetColumnRequests(p.x, p.z);
        }
    }

    void BedrockSession::ForgetColumnRequests(int cx, int cz) {
        const auto inColumn = [cx, cz](const std::tuple<int, int, int, int>& key) {
            return std::get<1>(key) == cx && std::get<3>(key) == cz;
        };
        std::erase_if(inflight, [&](const auto& entry) { return inColumn(entry.first); });
        std::erase_if(retries, [&](const auto& entry) { return inColumn(entry.first); });
        std::erase_if(requestedSlots, inColumn);
        colPending.erase({cx, cz});
    }

    void BedrockSession::Update() {
        if (transferring && std::this_thread::get_id() != transferThreadId)
            return;
        std::unique_lock<std::mutex> updateLock(updateMutex, std::defer_lock);
        if (std::this_thread::get_id() != transferThreadId)
            updateLock.lock();
        if (transferRequested && !transferring && std::this_thread::get_id() != transferThreadId) {
            BeginTransfer();
            return;
        }
        ExpirePredictions();
        if (state == State::InGame) {
            UpdateColumnLod();
            if (clientGen)
                FillUnstreamedColumns();
        }
        if (startGameSeen && !initializedSent)
            FinishLoadingIfReady();
        if (dimAckPending) {
            const int minX = static_cast<int>(std::floor(pose.x - 16.0)) >> 4;
            const int maxX = static_cast<int>(std::floor(pose.x + 16.0)) >> 4;
            const int minZ = static_cast<int>(std::floor(pose.z - 16.0)) >> 4;
            const int maxZ = static_cast<int>(std::floor(pose.z + 16.0)) >> 4;
            bool ready = true;
            {
                std::lock_guard<std::mutex> lk(reqMutex);
                for (int cx = minX; cx <= maxX && ready; ++cx)
                    for (int cz = minZ; cz <= maxZ; ++cz)
                        if (!seenColumns.count({cx, cz}) || colPending.count({cx, cz})) {
                            ready = false;
                            break;
                        }
            }
            if (ready || std::chrono::steady_clock::now() - dimAckSince > std::chrono::seconds(4))
                FinishDimensionChange();
        }
        if (!replayMode && state == State::InGame && !transferring && !transferRequested && !diagLostLogged &&
            lastPacketMs.load() != 0 && net && !net->HasLivenessTimeout()) {
            const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            // a quiet server legitimately sends nothing for a while (a flat empty world, a plugin server): wait a full two minutes
            if (nowMs - lastPacketMs.load() > 120000) {
                diagLostLogged = true;
                std::string why = "The server stopped responding (no data for 2 minutes).";
                {
                    std::lock_guard<std::mutex> lk(disconnectMutex);
                    if (!lastViolation.empty())
                        why += "\nThe server had just reported: " + lastViolation;
                }
                SetDisconnectInfo(-1, why, true);
                lastError = "connection lost";
                state = State::Failed;
                Log("net", "CONNECTION LOST (silence watchdog) {}", why);
            }
        }
        if (net && !net->IsConnected() && !diagLostLogged && state != State::Disconnected) {
            diagLostLogged = true;
            if (state == State::InGame && !transferring && !transferRequested) {
                SetDisconnectInfo(-1, net->LastError(), true);
                lastError = "connection lost";
                state = State::Failed;
            }
            Log("net", "CONNECTION LOST (transport closed) state={} lastError='{}' transport='{}'", (int)state.load(), lastError, net->LastError());
        }
        if (!net || !net->IsConnected())
            return;
        {
            auto t = std::chrono::steady_clock::now();
            if (diagAt.time_since_epoch().count() == 0) {
                diagAt = t;
            } else if (t - diagAt >= std::chrono::seconds(5)) {
                diagAt = t;
                uint64_t n = diagPackets;
                bool known;
                {
                    std::lock_guard<std::mutex> lk(timeMutex);
                    known = timeTotal >= 0;
                }
                Log("net", "5s: {} packets (last id {} {}), state={}, time {}, chunks={}", (n - diagPrevPackets), diagLastId.load(), PacketName(diagLastId), (int)state.load(), (known ? "known" : "UNKNOWN"), chunksReceived.load());
                diagPrevPackets = n;
                std::map<uint32_t, uint64_t> counts;
                {
                    std::lock_guard<std::mutex> lk(idCountMutex);
                    counts = idCounts;
                }
                std::string line;
                for (const auto& [pid, c] : counts)
                    line += " " + std::string(PacketName(pid)) + "=" + std::to_string(c);
                Log("net", "totals by packet:{}", line);
            }
        }
        if (state == State::ResourcePacks) {
            const auto again = packs.Tick();
            if (again.step == PackDownloader::Step::RequestChunk)
                for (const uint32_t index : again.chunks)
                    Send(proto::ResourcePackChunkRequestPacket{.packId = again.packId, .chunkIndex = index});
        }
        std::vector<uint8_t> msg;
        const auto receiveStart = std::chrono::steady_clock::now();
        int handled = 0;
        while (net->TryReceive(msg)) {
            HandleBatch(msg);
            if (state == State::Failed)
                return;
            if ((++handled & 3) == 0 && std::chrono::steady_clock::now() - receiveStart > std::chrono::microseconds(handled > 400 ? 9000 : 4000)) {
                if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                    Log("net", "receive budget reached batches={} afterAuthTick={}", handled, LastAuthTick());
                break;
            }
        }
    }

    void BedrockSession::HandleBatch(const std::vector<uint8_t>& received) {
        std::vector<uint8_t> decrypted;
        if (cipher && cipher->Active()) {
            if (!cipher->Decrypt(received, decrypted)) {
                lastError = "packet checksum mismatch (encryption out of step)";
                state = State::Failed;
                return;
            }
        }
        const std::vector<uint8_t>& msg = decrypted.empty() && !(cipher && cipher->Active()) ? received : decrypted;
        const uint8_t* data = msg.data();
        size_t size = msg.size();
        std::vector<uint8_t> decompressed;
        if (compressEnabled) {
            if (size < 1) {
                lastError = "empty batch";
                state = State::Failed;
                return;
            }
            if (msg[0] == 0x00) {
                if (!RawInflate(data + 1, size - 1, decompressed)) {
                    lastError = "batch raw-inflate failed (size " + std::to_string(size) + ")";
                    state = State::Failed;
                    return;
                }
                data = decompressed.data();
                size = decompressed.size();
            } else if (msg[0] == 0x01) {
                if (!SnappyDecompress(data + 1, size - 1, decompressed)) {
                    lastError = "batch snappy decompress failed (size " + std::to_string(size) + ")";
                    state = State::Failed;
                    return;
                }
                data = decompressed.data();
                size = decompressed.size();
            } else if (msg[0] == 0xFF) {
                data = data + 1;
                size = size - 1;
            } else {
                lastError = "bad compressor byte " + std::to_string(msg[0]);
                state = State::Failed;
                return;
            }
        }
        wire::Reader r(data, size);
        while (r.Remaining() > 0) {
            uint32_t len = 0;
            if (!r.ReadVarUint32(len) || r.Remaining() < len) {
                lastError = "truncated batch";
                state = State::Failed;
                return;
            }
            const uint8_t* pktStart = r.Cursor();
            wire::Reader pr(pktStart, len);
            uint32_t id = 0;
            if (!pr.ReadVarUint32(id)) {
                lastError = "truncated packet id";
                state = State::Failed;
                return;
            }
            id &= 0x3FF;
            size_t headerLen = (size_t)(pr.Cursor() - pktStart);
            std::vector<uint8_t> body(pktStart + headerLen, pktStart + len);
            OnGamePacket(id, body);
            r.Skip(len);
            if (state == State::Failed)
                return;
        }
    }

    void BedrockSession::OnGamePacket(uint32_t id, const std::vector<uint8_t>& body) {
        diagPackets++;
        diagLastId = id;
        lastPacketMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        {
            std::lock_guard<std::mutex> lk(idCountMutex);
            idCounts[id]++;
        }
        if (onPacket)
            onPacket(id, body);
        if (tapActive.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> tl(tapMutex);
            if (recordTap)
                recordTap(0, id, body);
        }
        if (dropActive.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk(dropMutex);
            if (std::find(dropped.begin(), dropped.end(), id) != dropped.end())
                return;
        }
        {
            static int dumped = 0;
            if (dumped < 12) {
                dumped++;
                static const char* hx = "0123456789abcdef";
                std::string h;
                for (size_t i = 0; i < body.size() && i < 64; i++) {
                    h += hx[body[i] >> 4];
                    h += hx[body[i] & 15];
                }
                Log("net", "packet #{} id {} len {} hex {}", dumped, id, body.size(), h);
            }
        }
        {
            static std::set<uint32_t> seen;
            if (seen.insert(id).second)
                Log("net", "first {} (id {}, {} bytes)", PacketName(id), id, body.size());
        }

        packetlog::Record(false, id, body.data(), body.size());
        if (id == LevelChunkPacket::kId || id == SubchunkPacket::kId) {
            {
                std::lock_guard<std::mutex> lk(chunkMutex);
                chunkIn.emplace_back(id, body);
            }
            chunkCv.notify_one();
            return;
        }
        dispatcher.Dispatch(id, body.data(), body.size());
    }

    bool BedrockSession::SendRequestNetworkSettings() {
        proto::RequestNetworkSettingsPacket request;
        request.clientProtocol = serverProtocol;
        return Send(request);
    }

    bool BedrockSession::SendLogin() {
        std::string error;
        const auto login =
            BuildOfflineLogin(serverProtocol, playerName, gameVersion, serverAddress, *identity, maxViewDistance, skin,
                              account ? &onlineCreds : nullptr, friendLease ? friendLease->nonce : std::string(), error);
        if (!login) {
            lastError = "login build failed: " + error;
            return false;
        }
        Log("bedrock", "sending Login (protocol {})", serverProtocol);
        return Send(*login);
    }

    bool BedrockSession::SendResourcePackResponse(proto::ResourcePackClientResponsePacketResponseStatus status) {
        using Status = proto::ResourcePackClientResponsePacketResponseStatus;
        const char* name = "resourcepackstackfinished";
        switch (status) {
            case Status::Refused: name = "cancel"; break;
            case Status::SendPacks: name = "downloading"; break;
            case Status::HaveAllPacks: name = "downloadingfinished"; break;
            default: break;
        }
        Log("bedrock", "ResourcePackClientResponse {}", static_cast<int>(status));
        proto::ResourcePackClientResponsePacket p;
        p.responseStatus = status;
        p.responseStatusName = name;
        return Send(p);
    }

    void BedrockSession::FinishLoadingIfReady() {
        const auto now = std::chrono::steady_clock::now();
        bool ready = false;
        if (spawnSeen) {
            size_t columns;
            {
                std::lock_guard<std::mutex> lk(reqMutex);
                columns = seenColumns.size();
            }
            ready = columns >= 9 || now - spawnAt > std::chrono::seconds(3);
        } else if (now - startGameAt > std::chrono::seconds(8)) {
            Log("bedrock", "no PlayerSpawn after 8s, initialising anyway");
            ready = true;
        }
        if (!ready)
            return;
        SendLoadingScreen(false);
        SendSetLocalPlayerAsInitialized();
    }

    bool BedrockSession::SendClientCacheStatus() {
        return Send(proto::ClientCacheStatusPacket{.enabled = false});
    }

    bool BedrockSession::SendRequestChunkRadius() {
        proto::RequestChunkRadiusPacket p;
        p.chunkRadius = chunkRadius;
        p.maxRadius = std::max(chunkRadius, maxViewDistance);
        Log("bedrock", "RequestChunkRadius {}", chunkRadius);
        return Send(p);
    }

    bool BedrockSession::SendSetLocalPlayerAsInitialized() {
        if (runtimeId == 0)
            return false;
        proto::SetLocalPlayerAsInitializedPacket p;
        p.runtimeEntityId = runtimeId;
        initializedSent = true;
        Log("bedrock", "SetLocalPlayerAsInitialized");
        return Send(p);
    }

    bool BedrockSession::SendLoadingScreen(bool start, std::optional<uint32_t> id) {
        proto::ServerboundLoadingScreenPacket p;
        p.type = start ? 1 : 2;
        p.loadingScreenId = id;
        return Send(p);
    }

    void BedrockSession::BeginTransfer() {
        transferRequested = false;
        if (transferThread.joinable())
            transferThread.join();
        transferring = true;
        transferThreadId = std::thread::id();
        const std::string host = transferHost;
        const uint16_t port = transferPort;
        transferThread = std::thread([this, host, port] {
            transferThreadId = std::this_thread::get_id();
            Log("bedrock", "transferring to {}:{}", host, port);
            ResetForTransfer();
            if (!ConnectImpl(host, port, true))
                Log("bedrock", "transfer failed: {}", lastError);
            else
                Log("bedrock", "transfer complete");
            transferring = false;
        });
        while (transferThreadId == std::thread::id())
            std::this_thread::yield();
    }

    void BedrockSession::ResetForTransfer() {
        {
            std::lock_guard<std::mutex> lk(txMutex);
            if (net)
                net->Disconnect();
        }
        ResetWorldForDimension();
        compressEnabled = false;
        cacheStatusSent = false;
        packsCompletedSent = false;
        radiusSent = false;
        startGameSeen = false;
        spawnSeen = false;
        initializedSent = false;
        gotSpawn = false;
        runtimeId = 0;
        uniqueId = 0;
        ResetPlayerState();
        pose = PlayerPose{};
        haveLastAuthPos = false;
        haveStartPos = false;
        pendingTeleportAck = false;
        dimAckPending = false;
        diagLostLogged = false;
        chunksReceived = 0;
        state = State::Connecting;
    }

    void BedrockSession::ResetWorldForDimension() {
        {
            std::lock_guard<std::mutex> lk(chunkMutex);
            chunkIn.clear();
        }
        netLevel.Clear();
        blockEntities.Clear();
        std::lock_guard<std::mutex> lk(reqMutex);
        seenColumns.clear();
        requestedSlots.clear();
        colPending.clear();
        reqQueue = {};
        reqQueueFar = {};
        columns.clear();
        lodScanCentre[0] = lodScanCentre[1] = INT32_MIN;
        inflight.clear();
        retries.clear();
    }

    void BedrockSession::NoteHurt() {
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock(hurtMutex);
            lastHurtAt = now;
            const bool recentKnock = knockAt.time_since_epoch().count() != 0 && now - knockAt < std::chrono::seconds(1);
            hurtSource.known = recentKnock;
            hurtSource.dx = recentKnock ? -knockX : 0.0f;
            hurtSource.dz = recentKnock ? -knockZ : 0.0f;
            hurtSource.serial++;
        }
        hurtCounter++;
    }

    void BedrockSession::NoteKnockback(float vx, float vz) {
        if (std::hypot(vx, vz) <= 1.0e-6f)
            return;
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(hurtMutex);
        knockX = vx;
        knockZ = vz;
        knockAt = now;
        if (!hurtSource.known && lastHurtAt.time_since_epoch().count() != 0 &&
            now - lastHurtAt < std::chrono::milliseconds(150)) {
            hurtSource.known = true;
            hurtSource.dx = -vx;
            hurtSource.dz = -vz;
            hurtSource.serial++;
        }
    }

    void BedrockSession::FinishDimensionChange() {
        if (!dimAckPending.exchange(false))
            return;
        Log("bedrock", "dimension {} loaded, acknowledging", dimensionId.load());
        if (PERLIN_ENV("PERLIN_BLOCK_DEBUG")) {
            const auto current = GetPose();
            const bool settled = ColumnSettled(static_cast<int>(std::floor(current.x)) >> 4,
                                               static_cast<int>(std::floor(current.z)) >> 4);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - dimAckSince).count();
            Log("bedrock", "dimension_ack afterAuthTick={} elapsedMs={} columnSettled={} pos=({},{},{})", LastAuthTick(), elapsed, settled, current.x, current.y, current.z);
        }
        SendPlayerAction(static_cast<uint32_t>(proto::Action::DimensionChangeAck), 0, 0, 0, 0);
        SendLoadingScreen(false, dimLoadingId);
    }

    bool BedrockSession::SendPlayerAuthInput() {
        return SendPlayerAuthInputWithFlags({}, pose.pitch, pose.yaw);
    }

    bool BedrockSession::SendPlayerAuthInputWithFlags(const std::vector<uint32_t>& flags, float pitchOverride,
                                                      float yawOverride, uint32_t action, int bx, int by,
                                                      int bz, int face) {
        if (!pose.known)
            return false;
        MoveInput in;
        in.flags = flags;
        in.yaw = yawOverride;
        in.pitch = pitchOverride;
        in.position[0] = pose.x;
        in.position[1] = pose.y;
        in.position[2] = pose.z;
        auto has = [&](uint32_t id) { return std::find(flags.begin(), flags.end(), id) != flags.end(); };
        in.moveX = (has(InputFlag::Left) ? 1.0f : 0.0f) - (has(InputFlag::Right) ? 1.0f : 0.0f);
        in.moveZ = (has(InputFlag::Up) ? 1.0f : 0.0f) - (has(InputFlag::Down) ? 1.0f : 0.0f);
        if (haveLastAuthPos) {
            in.delta[0] = pose.x - lastAuthPos[0];
            in.delta[1] = pose.y - lastAuthPos[1];
            in.delta[2] = pose.z - lastAuthPos[2];
        }
        in.hasDelta = true;
        if (action != UINT32_MAX) {
            in.hasBlockAction = true;
            in.blockAction = {action, bx, by, bz, face};
        }
        return SendMoveInput(in);
    }

    bool BedrockSession::SendMoveInput(const MoveInput& input) {
        if (!initializedSent)
            return false;
        auto finite = [](float v) { return std::isfinite(v); };
        for (float v : {input.position[0], input.position[1], input.position[2], input.yaw, input.pitch,
                        input.eyeHeight, input.moveX, input.moveZ})
            if (!finite(v))
                return false;
        if (std::fabs(input.position[0]) > 100000.0f || std::fabs(input.position[1]) > 10000.0f ||
            std::fabs(input.position[2]) > 100000.0f)
            return false;

        std::vector<uint32_t> inputFlags;
        inputFlags.reserve(input.flags.size() + 3);
        auto add = [&](uint32_t id) {
            if (std::find(inputFlags.begin(), inputFlags.end(), id) == inputFlags.end())
                inputFlags.push_back(id);
        };
        for (uint32_t id : input.flags)
            if (id <= 65)
                add(id);

        {
            std::lock_guard lock(pendingInputMutex);
            for (uint32_t id : pendingInputFlags)
                add(id);
            pendingInputFlags.clear();
        }
        if (pendingTeleportAck) {
            add(InputFlag::HandledTeleport);
            pendingTeleportAck = false;
        }

        bool hasAction = input.hasBlockAction;
        BlockActionData action = input.blockAction;
        if (!hasAction && !pendingBlockActions.empty()) {
            hasAction = true;
            action = pendingBlockActions.front();
            pendingBlockActions.pop_front();
        }
        if (hasAction)
            add(InputFlag::PerformBlockActions);
        // SendPlayerInputPacketSystem 0x146dab080: bit 45 goes with the vehicle rotation and id, never alone
        if (input.predictedVehicle)
            add(static_cast<uint32_t>(proto::InputData::ClientPredictedVehicle));

        float mx = input.moveX, mz = input.moveZ;
        const float mlen = std::sqrt(mx * mx + mz * mz);
        if (mlen > 1.0f) {
            mx /= mlen;
            mz /= mlen;
        }

        constexpr float kPi = 3.14159265358979323846f;
        const float yaw = input.yaw * (kPi / 180.0f);
        const float pitch = input.pitch * (kPi / 180.0f);
        const float cp = std::cos(pitch);

        lastAuthPos[0] = input.position[0];
        lastAuthPos[1] = input.position[1];
        lastAuthPos[2] = input.position[2];
        haveLastAuthPos = true;

        PlayerAuthInputPacket f;
        f.pitch = input.pitch;
        f.yaw = input.yaw;
        f.position = {input.position[0], input.position[1] + input.eyeHeight, input.position[2]};
        f.moveVector = {mx, mz};
        f.headYaw = input.yaw;
        f.inputData.reserve(inputFlags.size());
        for (uint32_t id : inputFlags)
            f.inputData.push_back(static_cast<proto::InputData>(id));
        f.inputMode = proto::PlayerAuthInputPacketInputMode::Mouse;
        f.playMode = proto::PlayerAuthInputPacketPlayMode::Normal;
        f.interactionModel = proto::PlayerAuthInputPacketInteractionModel::Crosshair;
        f.interactRotation = {input.pitch, input.yaw};
        f.tick = ++authTick;
        f.delta = {input.delta[0], input.delta[1], input.delta[2]};
        if (hasAction) {
            proto::PlayerAuthInputPacketBlockActionEntry entry;
            entry.action = static_cast<proto::Action>(action.action);
            entry.position = {action.x, action.y, action.z};
            entry.face = action.face;
            f.blockAction = std::vector<proto::PlayerAuthInputPacketBlockActionEntry>{entry};
        }
        if (input.predictedVehicle) {
            f.vehicleRotation = proto::Vec2F{(*input.vehicleRotation)[0], (*input.vehicleRotation)[1]};
            f.predictedVehicle = *input.predictedVehicle;
        }
        f.analogueMoveVector = {0, 0};
        f.cameraOrientation = {-std::sin(yaw) * cp, -std::sin(pitch), std::cos(yaw) * cp};
        {
            float rx = input.rawX, rz = input.rawZ;
            const float rl = rx * rx + rz * rz;
            if (rl > 1.0f) {
                const float inv = 1.0f / std::sqrt(rl);
                rx *= inv;
                rz *= inv;
            }
            f.rawMoveVector = {rx, rz};
        }
        if (movePacketsSent < 3 || movePacketsSent % 200 == 0)
            Log("move", "input #{} tick={} eye=({},{},{}) move=({},{}) flags={}", movePacketsSent, f.tick, f.position.x, f.position.y, f.position.z, f.moveVector.x, f.moveVector.z, f.inputData.size());
        movePacketsSent++;
        const bool sent = Send(f);
        if (predictionSyncCountdown > 0 && --predictionSyncCountdown == 0 && SendPredictionSync())
            predictionSyncCountdown = -1;
        return sent;
    }

    bool BedrockSession::SendPredictionSync() {
        proto::ClientMovementPredictionSyncPacket p;
        {
            std::lock_guard lk(attrMutex);
            const auto value = [&](const char* name, float fallback, bool required, bool& ok) {
                const auto it = ownAttributes.find(name);
                if (it == ownAttributes.end() || !std::isfinite(it->second)) {
                    if (required)
                        ok = false;
                    return fallback;
                }
                return it->second;
            };
            bool ok = true;
            p.movementSpeed = value("minecraft:movement", 0.0f, true, ok);
            p.underwaterMovementSpeed = value("minecraft:underwater_movement", 0.0f, true, ok);
            p.lavaMovementSpeed = value("minecraft:lava_movement", 0.0f, true, ok);
            p.jumpStrength = value("minecraft:horse.jump_strength", 0.0f, true, ok);
            p.health = value("minecraft:health", 0.0f, true, ok);
            p.hunger = value("minecraft:player.hunger", 0.0f, true, ok);
            p.unknownAttribute1 = value("minecraft:friction_modifier", 1.0f, false, ok);
            p.unknownAttribute2 = value("minecraft:bounciness", 0.0f, false, ok);
            p.unknownAttribute3 = value("minecraft:air_drag_modifier", 1.0f, false, ok);
            if (!ok)
                return false;
            p.dataFlags = ownFlagWords;
            p.boundingBox.scale = ownScale;
            p.boundingBox.width = ownWidth;
            p.boundingBox.height = ownHeight;
        }
        p.entityUniqueId = uniqueId;
        p.isFlying = flying;
        Log("move", "prediction sync");
        return Send(p);
    }

    bool BedrockSession::SendPlayerAuthInputBlockAction(uint32_t action, int bx, int by, int bz, int face) {
        if (!serverAuthBreaking.load()) {
            if (action == 26 || action == 27)
                return true;
            proto::PlayerActionPacket a;
            a.runtimeEntityId = runtimeId;
            a.action = static_cast<proto::Action>(action);
            a.position = {bx, by, bz};
            a.resultPosition = {0, 0, 0};
            a.face = face;
            const bool ok = Send(a);
            if (action == 2) {
                proto::InventoryTransactionPacket t;
                t.transaction.transactionType = proto::TransactionTransactionType::ItemUse;
                proto::TransactionUseItem u;
                int selected = 0;
                {
                    std::lock_guard lock(statusMutex);
                    selected = std::clamp(inventory.selected, 0, 8);
                    u.heldItem = inventoryRaw[selected];
                }
                u.actionType = proto::TransactionUseItemActionType::BreakBlock;
                u.triggerType = proto::TransactionUseItemTriggerType::PlayerInput;
                u.blockPosition = {bx, by, bz};
                u.face = static_cast<uint8_t>(face);
                u.hotbarSlot = selected;
                u.hand = proto::TransactionUseItemHand::MainHand;
                u.playerPos = {lastAuthPos[0], lastAuthPos[1] + 1.62f, lastAuthPos[2]};
                u.clickPos = {0.5f, 0.5f, 0.5f};
                bool loaded = false;
                const uint16_t id = netLevel.GetBlock(bx, by, bz, loaded);
                u.blockRuntimeId = loaded ? static_cast<uint32_t>(BlockPalette::Instance().RuntimeId(id)) : 0;
                u.clientPrediction = proto::TransactionUseItemClientPrediction::Success;
                t.transaction.transactionData = u;
                Send(t);
            }
            return ok;
        }
        if (pendingBlockActions.size() < 64)
            pendingBlockActions.push_back({action, bx, by, bz, face});
        return true;
    }

    void BedrockSession::QueueServerMotion(ServerMotion motion) {
        if (motion.poseChanged) {
            motion.pos[0] = pose.x;
            motion.pos[1] = pose.y;
            motion.pos[2] = pose.z;
        }
        serverMotions.push_back(motion);
        if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
            Log("move", "queued_motion depth={} teleport={} correction={} stamped={} tick={} ground={}", serverMotions.size(), motion.teleport, motion.correction, motion.stampedVelocity, motion.tick, motion.onGround);
    }

    bool BedrockSession::TakeServerMotion(ServerMotion& out) {
        if (serverMotions.empty())
            return false;
        out = serverMotions.front();
        serverMotions.pop_front();
        return true;
    }

    bool BedrockSession::TakeVehicleCorrection(VehicleCorrection& out) {
        std::lock_guard lock(vehicleCorrectionMutex);
        if (vehicleCorrections.empty())
            return false;
        out = vehicleCorrections.front();
        vehicleCorrections.pop_front();
        return true;
    }

    void BedrockSession::PushChat(const std::string& line) {
        std::lock_guard lock(chatMutex);
        if (chatInbox.size() < 256)
            chatInbox.push_back(line);
    }

    void BedrockSession::PushSound(SoundRequest request) {
        std::lock_guard lock(soundMutex);
        if (soundInbox.size() < 256)
            soundInbox.push_back(std::move(request));
    }

    std::vector<BedrockSession::SoundRequest> BedrockSession::TakeSounds() {
        std::lock_guard lock(soundMutex);
        std::vector<SoundRequest> out;
        out.swap(soundInbox);
        return out;
    }

    void BedrockSession::PushParticle(ParticleEvent event) {
        std::lock_guard lock(particleMutex);
        if (particleInbox.size() < 512)
            particleInbox.push_back(std::move(event));
    }

    std::vector<BedrockSession::ParticleEvent> BedrockSession::TakeParticles() {
        std::lock_guard lock(particleMutex);
        std::vector<ParticleEvent> out;
        out.swap(particleInbox);
        return out;
    }

    std::vector<std::string> BedrockSession::TakeChat() {
        std::lock_guard lock(chatMutex);
        std::vector<std::string> out;
        out.swap(chatInbox);
        return out;
    }

    bool BedrockSession::SendChat(const std::string& message) {
        proto::TextPacket p;
        p.category = proto::TextPacketCategory::Authored;
        p.type = proto::TextPacketType::Chat;
        p.sourceName = playerName;
        p.message = message;
        Log("bedrock", "chat> {}", message);
        return Send(p);
    }

    bool BedrockSession::SendTickSync(uint64_t timestamp) {
        proto::NetworkStackLatencyPacket p;
        p.timestamp = timestamp * 1000000ULL;
        p.needsResponse = 1;
        return Send(p);
    }

    bool BedrockSession::SendCommand(const std::string& command) {
        proto::CommandRequestPacket p;
        p.command = !command.empty() && command[0] == '/' ? command : "/" + command;
        p.origin.type = "player";
        p.origin.uuid = net::RandomUuidV4();
        p.origin.requestId = std::string();
        p.origin.playerEntityId = 0;
        p.internal = false;
        p.version = "latest";
        Log("bedrock", "command> {}", command);
        return Send(p);
    }

    bool BedrockSession::SendSettingsCommand(const std::string& command) {
        proto::SettingsCommandPacket p;
        p.commandLine = !command.empty() && command[0] == '/' ? command : "/" + command;
        p.suppressOutput = false;
        Log("bedrock", "settings command> {}", command);
        return Send(p);
    }

    bool BedrockSession::SendSkinUpdate() {
        if (!skin.hasSkin() && !skin.hasCape())
            return false;
        proto::PlayerSkinPacket p;
        std::string hex;
        for (char c : playerUuid)
            if (c != '-')
                hex += c;
        for (size_t i = 0; i < 16 && i * 2 + 1 < hex.size(); i++)
            p.uuid.bytes[i] = static_cast<uint8_t>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
        p.skin.skinId = skin.skinId;
        p.skin.fullSkinId = skin.skinId;
        if (skin.hasSkin()) {
            p.skin.skinData.width = skin.skinWidth;
            p.skin.skinData.height = skin.skinHeight;
            p.skin.skinData.data = skin.skinRgba;
        }
        if (skin.hasCape()) {
            p.skin.capeData.width = skin.capeWidth;
            p.skin.capeData.height = skin.capeHeight;
            p.skin.capeData.data = skin.capeRgba;
            p.skin.capeId = skin.capeId;
            p.skin.capeOnClassic = true;
        }
        const char* geometry = skin.slimArms ? "geometry.humanoid.customSlim" : "geometry.humanoid.custom";
        p.skin.skinResourcePack = std::string("{\"geometry\":{\"default\":\"") + geometry + "\"}}";
        p.skin.armSize = skin.slimArms ? proto::SkinArmSize::Slim : proto::SkinArmSize::Wide;
        p.skin.geometryData = B64UrlDecode(kDefaultSkinGeometryB64);
        p.skin.geometryDataVersion = "0.0.0";
        p.skin.animationData = std::string();
        p.skin.skinColor = static_cast<int32_t>(0xffb37b62u);
        p.skin.premium = false;
        p.skin.persona = false;
        p.skin.primaryUser = true;
        p.skin.trusted = "true";
        p.oldSkinName = lastSentSkinName;
        lastSentSkinName = skin.skinId;
        p.skinName = skin.skinId;
        return Send(p);
    }

    void BedrockSession::SetPose(float x, float y, float z, float yaw, float pitch) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw) ||
            !std::isfinite(pitch) || std::fabs(x) > 100000.0f || std::fabs(y) > 10000.0f ||
            std::fabs(z) > 100000.0f)
            return;

        pose.x = x;
        pose.y = y;
        pose.z = z;
        pose.yaw = yaw;
        pose.pitch = pitch;
        pose.headYaw = yaw;
        pose.known = true;
    }

    void BedrockSession::RequestRespawn() {
        proto::RespawnPacket r;
        r.position = {0, 0, 0};
        r.state = 2;
        r.runtimeEntityId = runtimeId;
        Log("bedrock", "respawn requested (client ready, runtime {})", runtimeId);
        Send(r);
    }

    bool BedrockSession::SendPlayerAction(uint32_t action, int bx, int by, int bz, int face) {
        proto::PlayerActionPacket p;
        p.runtimeEntityId = runtimeId;
        p.action = static_cast<proto::Action>(action);
        p.position = {bx, by, bz};
        p.resultPosition = p.position;
        p.face = face;
        Log("bedrock", "PlayerAction {} at ({},{},{})", action, bx, by, bz);
        return Send(p);
    }

    namespace {
        void LogHex(const char* what, const std::string& why, const std::vector<uint8_t>& body) {
            static int budget = 12;
            if (budget-- <= 0)
                return;
            std::cout << "[chunk] " << what << " parse failed: " << why << " (" << body.size()
                      << " bytes) head:";
            for (size_t i = 0; i < body.size() && i < 40; i++) {
                char b[4];
                snprintf(b, sizeof(b), " %02x", body[i]);
                std::cout << b;
            }
            std::cout << std::endl;
        }
    }

    void BedrockSession::PrewarmLogin(std::shared_ptr<xbox::Account> account, const std::string& loginKey) {
        if (!account || loginKey.empty())
            return;
        std::lock_guard lock(gLoginMutex);
        CachedLogin& c = gLogins[loginKey];
        if (Fresh(c) || (c.pending.valid() && c.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready))
            return;
        c.pending = std::async(std::launch::async, [account, loginKey] {
            auto identity = std::make_unique<ClientIdentity>();
            if (!identity->Valid())
                return false;
            // other startup requests to PlayFab can collide with this one ("A conflict occurred..."): try again
            for (int attempt = 1; attempt <= 3; attempt++) {
                xbox::Credentials creds;
                std::string error;
                if (account->LoginMinecraft(identity->PublicKeyB64(), "1.26.60", creds, error)) {
                    StoreLogin(loginKey, identity->Key(), creds);
                    Log("auth", "signed in ahead of the join");
                    return true;
                }
                Log("auth", "early sign-in attempt {} failed: {}", attempt, error);
                std::this_thread::sleep_for(std::chrono::milliseconds(800 * attempt));
            }
            return false;
        }).share();
    }

    void BedrockSession::NoteModChunk(bool loaded, int x, int z) const {
        auto& mm = mods::ModManager::Instance();
        const abi::Event event = loaded ? abi::Event::ChunkLoaded : abi::Event::ChunkUnloaded;
        if (!mm.Wants(event))
            return;
        abi::Writer w;
        w.write(abi::EventChunk{x, z, static_cast<abi::Dimension>(dimensionId.load())});
        mm.Queue(event, std::move(w));
    }

    void BedrockSession::HandleLevelChunk(const LevelChunkPacket& pkt) {
        NoteModChunk(true, pkt.x, pkt.z);
        static int logged = 0;
        static int loggedFull = 0;
        const bool interesting = pkt.payload.size() > 0 || (int32_t)pkt.subChunkCount != 0;
        if (logged < 4 || (interesting && loggedFull < 40)) {
            logged++;
            if (interesting)
                loggedFull++;
            Log("chunk", "LevelChunk ({},{}) dim={} subChunkCount={} requestMode={} limit={} cache={} payload={}B", pkt.x, pkt.z, pkt.dimension, (int32_t)pkt.subChunkCount, IsRequestMode(pkt), pkt.highestSubchunkCount.value_or(-1), pkt.cacheEnabled, pkt.payload.size());
        }
        if (pkt.cacheEnabled) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::cout << "[chunk] server enabled the blob cache even though we asked for it off; "
                             "chunk data will be missing"
                          << std::endl;
            }
            return;
        }
        if (pkt.dimension != dimensionId) {
            static int staleLogged = 0;
            if (staleLogged++ < 4)
                Log("chunk", "ignoring chunk for dimension {} (in {})", pkt.dimension, dimensionId.load());
            return;
        }
        const DimensionInfo& dim = ChunkCodec::Dimension(pkt.dimension);
        const bool requestMode = IsRequestMode(pkt);
        const bool far = requestMode &&
                         std::max(std::abs(pkt.x - viewCenterChunk[0]), std::abs(pkt.z - viewCenterChunk[1])) > nearRadius;
        netLevel.EnsureChunk(pkt.x, pkt.z, true, far);
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            seenColumns.insert({pkt.x, pkt.z});
        }
        chunksReceived++;
        statLevelChunks++;
        lastColumnMs = static_cast<int64_t>(Level::NowMs());

        if (!requestMode) {
            const uint8_t* p = pkt.payload.data();
            size_t left = pkt.payload.size();
            std::vector<std::pair<int, SubChunkData>> parsed;
            size_t used = 0;
            const char* why = nullptr;
            if (!ChunkCodec::ParseColumn(p, left, pkt.subChunkCount, dim.MinSubChunk(), dim.slots, parsed, used, &why)) {
                LogHex("inline column", why ? why : "?", pkt.payload);
                {
                    std::lock_guard<std::mutex> lk(reqMutex);
                    ColumnInfo& c = columns[ChunkPos{pkt.x, pkt.z}];
                    c.dim = pkt.dimension;
                    c.firstSubY = dim.MinSubChunk();
                    c.slots = dim.slots;
                }
                QueueSubChunkRequests(pkt.dimension, pkt.x, pkt.z, dim.MinSubChunk(), dim.slots);
                return;
            }
            p += used;
            left -= used;
            BlockPalette& blockPalette = BlockPalette::Instance();
            if (blockPalette.Sequential() && !parsed.empty()) {
                const auto top = std::max_element(parsed.begin(), parsed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                if (!top->second.storages.empty() && !top->second.storages[0].persistent && top->second.storages[0].palette.size() == 1) {
                    const int32_t seen = top->second.storages[0].palette[0];
                    if (!blockPalette.SequenceVerified() && blockPalette.SequenceLoaded()) {
                        if (!blockPalette.CalibrateSequence(seen) && blockPalette.SequentialAir() != seen) {
                            blockPalette.SetSequentialAir(seen);
                            Log("block", "server uses sequential block ids but its air id {} does not match our table; treating it as air (table says '{}')", seen, blockPalette.Name(blockPalette.InternRuntime(seen)));
                        }
                    } else if (blockPalette.SequentialAir() < 0) {
                        blockPalette.SetSequentialAir(seen);
                    }
                }
            }
            for (auto& [absY, sc] : parsed)
                ApplySubChunk(pkt.x, pkt.z, absY, sc);
            if (left > 0)
                ParseBlockEntities(p, left, true);
            netLevel.MarkComplete(pkt.x, pkt.z);
        } else {
            ApplyBiomeColumn(pkt.x, pkt.z, pkt.payload);
            const int slots = RequestSlots(pkt, dim.slots);
            {
                std::lock_guard<std::mutex> lk(reqMutex);
                ColumnInfo& c = columns[ChunkPos{pkt.x, pkt.z}];
                c.dim = pkt.dimension;
                c.firstSubY = dim.MinSubChunk();
                c.slots = slots;
                c.biomes = pkt.payload;
            }
            if (far) {
                constexpr int kFarSlots = 3;
                const int take = std::min(slots, kFarSlots);
                QueueSubChunkRequests(pkt.dimension, pkt.x, pkt.z, dim.MinSubChunk() + slots - take, take, true);
            } else {
                QueueSubChunkRequests(pkt.dimension, pkt.x, pkt.z, dim.MinSubChunk(), slots);
            }
        }
    }

    void BedrockSession::ParseBlockEntities(const uint8_t* data, size_t size, bool scan) {
        size_t at = 0;
        while (at + 3 <= size) {
            if (scan && !(data[at] == 0x0A && data[at + 1] == 0x00)) {
                at++;
                continue;
            }
            ui::Json nbt;
            const size_t used = NetworkNbtPrefix(data + at, size - at, nbt);
            if (!used) {
                if (!scan)
                    return;
                at++;
                continue;
            }
            const ui::Json* x = nbt.Find("x");
            const ui::Json* y = nbt.Find("y");
            const ui::Json* z = nbt.Find("z");
            if (x && y && z && x->IsNumber() && y->IsNumber() && z->IsNumber())
                blockEntities.Set(static_cast<int>(x->number), static_cast<int>(y->number), static_cast<int>(z->number), nbt);
            at += used;
        }
    }

    void BedrockSession::ApplyBiomeColumn(int cx, int cz, const std::vector<uint8_t>& payload) {
        const DimensionInfo& dim = ChunkCodec::Dimension(dimensionId);
        size_t off = 0;
        int parsed = 0;
        for (int i = 0; i < dim.slots; i++) {
            if (off >= payload.size())
                break;
            if (payload[off] == 0xFF) {
                off += 1;
                continue;
            }
            uint8_t hdr = payload[off++];
            if (!(hdr & 1)) {
                break;
            }
            unsigned bpb = hdr >> 1;
            if (bpb != 0 && bpb != 1 && bpb != 2 && bpb != 3 && bpb != 4 && bpb != 5 && bpb != 6 &&
                bpb != 8 && bpb != 16) {
                break;
            }
            size_t nwords = 0;
            if (bpb != 0) {
                unsigned perWord = 32 / bpb;
                nwords = (4096 + perWord - 1) / perWord;
                if (off + nwords * 4 > payload.size())
                    break;
            }
            std::array<uint16_t, ChunkCodec::kCells> idx;
            if (bpb == 0) {
                idx.fill(0);
            } else {
                const uint8_t* wp = payload.data() + off;
                uint32_t mask = (bpb == 32) ? 0xFFFFFFFFu : ((1u << bpb) - 1);
                unsigned perWord = 32 / bpb;
                for (unsigned pos = 0; pos < (unsigned)ChunkCodec::kCells; pos++) {
                    size_t wi = pos / perWord;
                    const uint8_t* wb = wp + wi * 4;
                    uint32_t w = (uint32_t)wb[0] | ((uint32_t)wb[1] << 8) | ((uint32_t)wb[2] << 16) |
                                 ((uint32_t)wb[3] << 24);
                    idx[pos] = (uint16_t)((w >> ((pos % perWord) * bpb)) & mask);
                }
                off += nwords * 4;
            }
            std::vector<int32_t> pal;
            if (bpb == 0) {
                wire::Reader pr(payload.data() + off, payload.size() - off);
                int32_t v = 0;
                if (!pr.ReadZigzag32(v))
                    break;
                off = (size_t)(pr.Cursor() - payload.data());
                pal.push_back(v);
            } else {
                wire::Reader pr(payload.data() + off, payload.size() - off);
                int32_t cnt = 0;
                if (!pr.ReadZigzag32(cnt) || cnt <= 0 || cnt > 4096)
                    break;
                pal.resize((size_t)cnt);
                bool ok = true;
                for (int32_t k = 0; k < cnt; k++) {
                    if (!pr.ReadZigzag32(pal[(size_t)k])) {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                    break;
                off = (size_t)(pr.Cursor() - payload.data());
                bool valid = true;
                for (auto v : idx) {
                    if ((size_t)v >= pal.size()) {
                        valid = false;
                        break;
                    }
                }
                if (!valid)
                    continue;
            }
            std::array<uint8_t, ChunkCodec::kCells> cells;
            for (int c = 0; c < ChunkCodec::kCells; c++) {
                int32_t b = pal[idx[(size_t)c]];
                if (b < 0)
                    b = 0;
                if (b > 255)
                    b = 1;
                cells[(size_t)c] = (uint8_t)b;
            }
            netLevel.SetBiomeSubChunk(cx, cz, dim.MinSubChunk() + i, cells.data());
            parsed++;
        }
        static int logged = 0;
        if (logged < 3) {
            logged++;
            Log("chunk", "biome column ({},{}) subchunks={} payload={}B", cx, cz, parsed, payload.size());
        }
    }

    void BedrockSession::HandleSubChunk(const SubchunkPacket& pkt) {
        statSubChunkReplies++;
        if (pkt.dimension != dimensionId)
            return;
        const DimensionInfo& dim = ChunkCodec::Dimension(pkt.dimension);
        (void)dim;
        struct DecodeJob { int cx, absY, cz; const std::vector<uint8_t>* body; };
        std::vector<DecodeJob> decodeJobs;
        decodeJobs.reserve(pkt.entries.size());
        for (const SubChunkEntryEntry& e : pkt.entries) {
            int cx = pkt.origin.x + e.dx, absY = pkt.origin.y + e.dy, cz = pkt.origin.z + e.dz;
            const auto result = e.result;
            const std::vector<uint8_t> noPayload;
            const std::vector<uint8_t>& body = e.payload ? *e.payload : noPayload;
            const unsigned resultId = static_cast<unsigned>(e.result);
            statResult[resultId < 7 ? resultId : 7]++;
            {
                static int shown = 0;
                if (shown++ < 12)
                    Log("chunk", "reply ({},{},{}) result={} payload={}B", cx, absY, cz, resultId, body.size());
            }
            if (result == SubChunkEntryEntryResult::SuccessAllAir) {
                static const std::array<uint16_t, ChunkCodec::kCells> air{};
                netLevel.SetSubChunk(cx, cz, absY, air.data());
                CompleteRequest(std::make_tuple(pkt.dimension, cx, absY, cz), false);
                continue;
            }
            if (result != SubChunkEntryEntryResult::Success) {
                CompleteRequest(std::make_tuple(pkt.dimension, cx, absY, cz),
                    result != SubChunkEntryEntryResult::YIndexOutOfBounds &&
                    result != SubChunkEntryEntryResult::InvalidDimension);
                static int logged = 0;
                if (logged++ < 8)
                    Log("chunk", "SubChunk ({},{},{}) result={}", cx, absY, cz, resultId);
                continue;
            }
            decodeJobs.push_back({cx, absY, cz, &body});
        }
        const auto decode = [this, &decodeJobs](size_t i) {
            const DecodeJob& job = decodeJobs[i];
            SubChunkData sc;
            size_t used = 0;
            const char* why = nullptr;
            if (!ChunkCodec::ParseSubChunk(job.body->data(), job.body->size(), sc, used, &why)) {
                LogHex("SubChunk payload", why ? why : "?", *job.body);
                CompleteRequest(std::make_tuple(dimensionId.load(), job.cx, job.absY, job.cz), true);
                return;
            }
            const bool applied = ApplySubChunk(job.cx, job.cz, job.absY, sc);
            CompleteRequest(std::make_tuple(dimensionId.load(), job.cx, job.absY, job.cz), !applied);
            if (used < job.body->size())
                ParseBlockEntities(job.body->data() + used, job.body->size() - used, false);
        };
        if (decodeJobs.size() >= 4 && decodePool)
            decodePool->ParallelFor(decodeJobs.size(), decode);
        else
            for (size_t i = 0; i < decodeJobs.size(); i++)
                decode(i);
    }

    bool BedrockSession::ApplySubChunk(int cx, int cz, int absSubY, const SubChunkData& sc) {
        if (sc.storages.empty()) {
            // zero-storage sub-chunk: the server means all air, store it so the column fills in
            static const std::array<uint16_t, ChunkCodec::kCells> air{};
            netLevel.SetSubChunk(cx, cz, absSubY, air.data());
            statSubChunksApplied++;
            return true;
        }
        if (sc.hasYIndex && sc.yIndex != absSubY) {
            static std::atomic<int> mismatches{0};
            if (mismatches++ < 16)
                Log("chunk", "sub-chunk ({},{}) y mismatch: placed at {} but payload says {}", cx, cz, absSubY, static_cast<int>(sc.yIndex));
            absSubY = sc.yIndex;
        }
        const PaletteStorage& st = sc.storages[0];
        ChunkCodec::CellIndices idx;
        if (!ChunkCodec::ExpandIndices(st, idx)) {
            static std::atomic<int> logged{0};
            if (logged++ < 8)
                Log("chunk", "sub-chunk ({},{},{}) storage did not expand (bpb={} palette={})", cx, absSubY, cz, st.bitsPerBlock, st.palette.size());
            return false;
        }
        std::vector<uint16_t> local(st.palette.size());
        for (size_t i = 0; i < local.size(); i++) {
            local[i] = st.persistent ? BlockPalette::Instance().Intern(st.palette[i])
                       : (st.palette[i] == 0 && !BlockPalette::Instance().Sequential()) ? uint16_t(0)
                                                                                       : BlockPalette::Instance().InternRuntime(st.palette[i]);
            static std::atomic<int> unknownLogged{0};
            if (local[i] != 0 && unknownLogged < 20 && BlockPalette::Instance().NamesLoaded() &&
                BlockPalette::Instance().Name(local[i]).empty()) {
                unknownLogged++;
                Log("chunk", "unknown block state hash {} in sub-chunk ({},{},{})", st.palette[i], cx, absSubY, cz);
            }
        }
        std::array<uint16_t, ChunkCodec::kCells> cells;
        for (int i = 0; i < ChunkCodec::kCells; i++)
            cells[(size_t)i] = local[idx[(size_t)i]];
        std::array<uint16_t, ChunkCodec::kCells> liquid{};
        const uint16_t* liquidPtr = nullptr;
        if (sc.storages.size() > 1) {
            const PaletteStorage& ls = sc.storages[1];
            ChunkCodec::CellIndices lidx;
            if (ChunkCodec::ExpandIndices(ls, lidx)) {
                std::vector<uint16_t> llocal(ls.palette.size());
                for (size_t i = 0; i < llocal.size(); i++)
                    llocal[i] = ls.persistent ? BlockPalette::Instance().Intern(ls.palette[i]) : BlockPalette::Instance().InternRuntime(ls.palette[i]);
                bool any = false;
                for (int i = 0; i < ChunkCodec::kCells; i++) {
                    liquid[(size_t)i] = llocal[lidx[(size_t)i]];
                    any = any || liquid[(size_t)i] != 0;
                }
                if (any)
                    liquidPtr = liquid.data();
            }
        }
        netLevel.SetSubChunk(cx, cz, absSubY, cells.data(), liquidPtr);
        statSubChunksApplied++;
        {
            static std::atomic<int> shown{0};
            if (shown++ < 12)
                Log("chunk", "applied ({},{},{}) bpb={} palette={} layers={}", cx, absSubY, cz, st.bitsPerBlock, st.palette.size(), sc.storages.size());
        }
        return true;
    }

    void BedrockSession::QueueSubChunkRequests(int dim, int cx, int cz, int firstSubY, int count, bool far) {
        bool idle = false;
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            for (int i = 0; i < count; i++) {
                auto key = std::make_tuple(dim, cx, firstSubY + i, cz);
                if (!requestedSlots.insert(key).second)
                    continue;
                colPending[{cx, cz}]++;
                retries.erase(key);
                (far ? reqQueueFar : reqQueue).push(key);
            }
            idle = colPending.find({cx, cz}) == colPending.end();
        }
        if (idle)
            netLevel.MarkComplete(cx, cz);
    }

    void BedrockSession::ReloadChunks() {
        std::vector<std::pair<ChunkPos, ColumnInfo>> cols;
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            for (const auto& [pos, info] : columns)
                cols.emplace_back(pos, ColumnInfo{info.dim, info.firstSubY, info.slots, {}});
            requestedSlots.clear();
            colPending.clear();
            retries.clear();
            inflight.clear();
            reqQueue = {};
            reqQueueFar = {};
        }
        for (const auto& [pos, info] : cols) {
            if (info.dim != dimensionId)
                continue;
            const bool far = std::max(std::abs(pos.x - viewCenterChunk[0]), std::abs(pos.z - viewCenterChunk[1])) > nearRadius;
            if (far) {
                const int take = std::min(info.slots, 3);
                QueueSubChunkRequests(info.dim, pos.x, pos.z, info.firstSubY + info.slots - take, take, true);
            } else {
                QueueSubChunkRequests(info.dim, pos.x, pos.z, info.firstSubY, info.slots);
            }
        }
        netLevel.MarkAllDirty();
        Log("chunk", "reload requested for {} columns", cols.size());
    }

    void BedrockSession::DropSlot(const std::tuple<int, int, int, int>& key,
                                  std::vector<std::pair<int, int>>& completed) {
        if (requestedSlots.erase(key) == 0)
            return;
        const std::pair<int, int> column{std::get<1>(key), std::get<3>(key)};
        const auto it = colPending.find(column);
        if (it != colPending.end() && --it->second <= 0) {
            colPending.erase(it);
            completed.push_back(column);
        }
    }

    void BedrockSession::FinishColumns(const std::vector<std::pair<int, int>>& columns) {
        for (const auto& column : columns)
            netLevel.MarkComplete(column.first, column.second);
    }

    bool BedrockSession::ColumnSettled(int cx, int cz) const {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(reqMutex));
        if (!seenColumns.count({cx, cz}))
            return false;
        return colPending.find({cx, cz}) == colPending.end();
    }

    void BedrockSession::NoteTime(int64_t v) {
        std::lock_guard<std::mutex> lk(timeMutex);
        timeTotal = v >= 0 ? v : ((v % 24000) + 24000) % 24000;
        timeAt = SimClock::Now();
    }

    void BedrockSession::NoteClock(int64_t time, bool paused) {
        std::lock_guard<std::mutex> lk(timeMutex);
        timeTotal = time;
        clockPaused = paused;
        timeAt = SimClock::Now();
    }

    void BedrockSession::SetDayCycle(bool running) {
        std::lock_guard<std::mutex> lk(timeMutex);
        const auto now = SimClock::Now();
        if (timeTotal >= 0 && dayCycleRunning && !clockPaused && timeAt.time_since_epoch().count() != 0)
            timeTotal += std::chrono::duration_cast<std::chrono::milliseconds>(now - timeAt).count() / 50;
        timeAt = now;
        dayCycleRunning = running;
    }

    bool BedrockSession::GetWorldTicks(double& outTicks) const {
        std::lock_guard<std::mutex> lk(timeMutex);
        if (timeTotal < 0)
            return false;
        double advance = 0.0;
        if (dayCycleRunning && !clockPaused && timeAt.time_since_epoch().count() != 0)
            advance = std::chrono::duration<double, std::milli>(SimClock::Now() - timeAt).count() / 50.0;
        outTicks = static_cast<double>(timeTotal) + advance;
        return true;
    }

    bool BedrockSession::GetWorldTime(int64_t& outDay, int& outTicksOfDay) const {
        std::lock_guard<std::mutex> lk(timeMutex);
        if (timeTotal < 0)
            return false;
        int64_t adv = dayCycleRunning && !clockPaused ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                            SimClock::Now() - timeAt)
                                                .count() /
                                            50
                                      : 0;
        int64_t total = timeTotal + adv;
        outDay = total / 24000;
        outTicksOfDay = (int)(total % 24000);
        return true;
    }

    void BedrockSession::CompleteRequest(const std::tuple<int, int, int, int>& key, bool retry) {
        std::vector<std::pair<int, int>> completed;
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            inflight.erase(key);
            if (retry && requestedSlots.count(key) && ++retries[key] <= kMaxSubChunkRetries) {
                reqQueue.push(key);
                return;
            }
            retries.erase(key);
            DropSlot(key, completed);
        }
        FinishColumns(completed);
    }

    int BedrockSession::PendingSubChunkRequests() const {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(reqMutex));
        return (int)reqQueue.size();
    }

    void BedrockSession::FillUnstreamedColumns() {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastFill < std::chrono::milliseconds(750))
            return;
        if (static_cast<int64_t>(Level::NowMs()) - lastColumnMs.load() < 1500 || lastColumnMs.load() == 0)
            return;
        lastFill = now;
        const int cx = viewCenterChunk[0], cz = viewCenterChunk[1];
        int radius = chunkRadius;
        if (const int granted = grantedRadius.load(); granted > 0)
            radius = std::min(radius, granted);
        if (viewRadiusChunks > 0)
            radius = std::min(radius, viewRadiusChunks);
        const DimensionInfo& dim = ChunkCodec::Dimension(dimensionId);
        constexpr size_t kPerScan = 48;
        std::vector<ChunkPos> missing;
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            for (int ring = 0; ring <= radius && missing.size() < kPerScan; ring++) {
                for (int dx = -ring; dx <= ring && missing.size() < kPerScan; dx++) {
                    for (int dz = -ring; dz <= ring && missing.size() < kPerScan; dz++) {
                        if (std::max(std::abs(dx), std::abs(dz)) != ring || dx * dx + dz * dz > radius * radius)
                            continue;
                        if (!seenColumns.count({cx + dx, cz + dz}))
                            missing.push_back({cx + dx, cz + dz});
                    }
                }
            }
        }
        if (missing.empty())
            return;
        for (const ChunkPos& p : missing) {
            const bool far = std::max(std::abs(p.x - cx), std::abs(p.z - cz)) > nearRadius;
            netLevel.EnsureChunk(p.x, p.z, true, far);
            {
                std::lock_guard<std::mutex> lk(reqMutex);
                seenColumns.insert({p.x, p.z});
                ColumnInfo& c = columns[p];
                c.dim = dimensionId;
                c.firstSubY = dim.MinSubChunk();
                c.slots = dim.slots;
            }
            if (far) {
                QueueSubChunkRequests(dimensionId, p.x, p.z, dim.MinSubChunk() + 6, 8, true);
            } else {
                QueueSubChunkRequests(dimensionId, p.x, p.z, dim.MinSubChunk(), dim.slots, false);
            }
        }
        statFilled += missing.size();
        static int logged = 0;
        if (logged++ < 8)
            Log("chunk", "client-side generation: requesting {} columns the server did not stream ({} so far, radius {})", missing.size(), statFilled, radius);
    }

    void BedrockSession::UpdateColumnLod() {
        const int cx = viewCenterChunk[0], cz = viewCenterChunk[1];
        if (cx == lodScanCentre[0] && cz == lodScanCentre[1] && nearRadius == lodScanRadius)
            return;
        lodScanRadius = nearRadius;
        lodScanCentre[0] = cx;
        lodScanCentre[1] = cz;
        std::vector<ChunkPos> candidates, demote;
        std::unordered_set<ChunkPos, ChunkPosHash> recoverable;
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            for (const auto& entry : columns) {
                recoverable.insert(entry.first);
                if (std::max(std::abs(entry.first.x - cx), std::abs(entry.first.z - cz)) <= nearRadius)
                    candidates.push_back(entry.first);
            }
        }
        {
            std::lock_guard<std::mutex> lk(netLevel.mutex);
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                            [&](const ChunkPos& p) {
                                                const auto found = netLevel.chunks.find(p);
                                                return found != netLevel.chunks.end() && !found->second.lodOnly;
                                            }),
                             candidates.end());
            for (const auto& entry : netLevel.chunks) {
                const ChunkPos& p = entry.first;
                if (!entry.second.lodOnly && recoverable.contains(p) &&
                    std::max(std::abs(p.x - cx), std::abs(p.z - cz)) > nearRadius + 3)
                    demote.push_back(p);
            }
        }
        std::vector<std::pair<ChunkPos, ColumnInfo>> promote;
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            for (const ChunkPos& p : candidates)
                if (const auto found = columns.find(p); found != columns.end())
                    promote.push_back({p, found->second});
        }
        for (const ChunkPos& p : demote) {
            netLevel.RemoveChunk(p.x, p.z);
            blockEntities.RemoveChunk(p.x, p.z);
            NoteModChunk(false, p.x, p.z);
        }
        if (!demote.empty()) {
            std::lock_guard<std::mutex> lk(reqMutex);
            for (const ChunkPos& p : demote)
                ForgetColumnRequests(p.x, p.z);
        }
        for (const auto& [pos, info] : promote) {
            if (info.slots <= 0)
                continue;
            if (!netLevel.PromoteLodOnly(pos.x, pos.z)) {
                netLevel.EnsureChunk(pos.x, pos.z, true, false);
                std::lock_guard<std::mutex> lk(reqMutex);
                seenColumns.insert({pos.x, pos.z});
            }
            ApplyBiomeColumn(pos.x, pos.z, info.biomes);
            QueueSubChunkRequests(info.dim, pos.x, pos.z, info.firstSubY, info.slots, false);
        }
    }

    void BedrockSession::PumpRequests(int budget) {
        constexpr size_t kMaxBatch = 32;
        auto now = std::chrono::steady_clock::now();
        std::vector<std::pair<int, int>> completed;
        {
            std::lock_guard<std::mutex> lk(reqMutex);
            for (auto it = inflight.begin(); it != inflight.end();) {
                if (now - it->second < std::chrono::seconds(5)) {
                    ++it;
                    continue;
                }
                auto key = it->first;
                it = inflight.erase(it);
                if (requestedSlots.count(key) && ++retries[key] <= kMaxSubChunkRetries) {
                    reqQueue.push(key);
                } else {
                    retries.erase(key);
                    DropSlot(key, completed);
                }
            }
        }
        FinishColumns(completed);
        for (int i = 0; i < budget; i++) {
            int dimension = 0, cx = 0, cz = 0, firstY = 0, count = 0;
            {
                std::lock_guard<std::mutex> lk(reqMutex);
                const bool farOk = netLevel.lodOnlyCount.load() < 1500 && inflight.size() < 3000;
                reqQueue.SetCenter(viewCenterChunk[0], viewCenterChunk[1]);
                reqQueueFar.SetCenter(viewCenterChunk[0], viewCenterChunk[1]);
                NearestQueue& queue = !reqQueue.empty() ? reqQueue : reqQueueFar;
                if (queue.empty() || (&queue == &reqQueueFar && !farOk))
                    break;
                auto key = queue.front();
                dimension = std::get<0>(key);
                cx = std::get<1>(key);
                firstY = std::get<2>(key);
                cz = std::get<3>(key);
                std::vector<int> ys;
                while (!queue.empty() && ys.size() < kMaxBatch) {
                    auto k = queue.front();
                    if (std::get<0>(k) != dimension || std::get<1>(k) != cx || std::get<3>(k) != cz)
                        break;
                    ys.push_back(std::get<2>(k));
                    inflight[k] = now;
                    queue.pop();
                }
                count = (int)ys.size();
                for (size_t j = 1; j < ys.size(); j++) {
                    if (ys[j] != ys[j - 1] + 1) {
                        for (size_t k = j; k < ys.size(); k++) {
                            auto rk = std::make_tuple(dimension, cx, ys[k], cz);
                            inflight.erase(rk);
                            queue.push(rk);
                        }
                        count = (int)j;
                        break;
                    }
                }
            }
            SubchunkRequestPacket req;
            req.dimension = dimension;
            req.origin = {cx, 0, cz};
            for (int i = 0; i < count; i++)
                req.requests.push_back({0, static_cast<int8_t>(firstY + i), 0});
            Send(req);
            statRequestsSent += count;
        }
        LogChunkStats();
    }

    void BedrockSession::LogChunkStats() {
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatLog < std::chrono::seconds(3))
            return;
        lastStatLog = now;
        if (statLevelChunks == 0 && statRequestsSent == 0)
            return;
        Log("chunk", "stats: levelChunks={} requestsSent={} replies={} subChunksApplied={} failed={} results[0..6,other]={},{},{},{},{},{},{},{} queued={}+{} lodOnly={} inflight={} blocksKnown={}", statLevelChunks, statRequestsSent, statSubChunkReplies, statSubChunksApplied.load(), statSubChunksFailed, statResult[0], statResult[1], statResult[2], statResult[3], statResult[4], statResult[5], statResult[6], statResult[7], reqQueue.size(), reqQueueFar.size(), netLevel.lodOnlyCount.load(), inflight.size(), BlockPalette::Instance().Count());
    }

}
