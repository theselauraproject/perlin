#include "client/multiplayer/SkinLoader.hpp"
#include "network/ClientIdentity.hpp"
#include "network/Http.hpp"
#include "network/LoginBuilder.hpp"
#include "network/PacketCipher.hpp"
#include "network/XboxAuth.hpp"
#include "util/Json.hpp"

#include <httplib.h>
#include <miniz.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

namespace {
    std::string Hex(const std::string& s) {
        std::string out;
        char buf[3];
        for (unsigned char c : s) {
            std::snprintf(buf, sizeof(buf), "%02x", c);
            out += buf;
        }
        return out;
    }
    std::string FromHex(const std::string& h) {
        std::string out;
        for (size_t i = 0; i + 1 < h.size(); i += 2)
            out += static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16));
        return out;
    }

    EC_KEY* KeyFromXY(const std::string& x, const std::string& y) {
        EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        BIGNUM* bx = BN_bin2bn(reinterpret_cast<const unsigned char*>(x.data()), 32, nullptr);
        BIGNUM* by = BN_bin2bn(reinterpret_cast<const unsigned char*>(y.data()), 32, nullptr);
        EC_KEY_set_public_key_affine_coordinates(key, bx, by);
        BN_free(bx);
        BN_free(by);
        return key;
    }

    bool VerifyRaw(EC_KEY* key, const std::string& digest, const std::string& rs) {
        ECDSA_SIG* sig = ECDSA_SIG_new();
        const size_t half = rs.size() / 2;
        BIGNUM* r =
            BN_bin2bn(reinterpret_cast<const unsigned char*>(rs.data()), static_cast<int>(half), nullptr);
        BIGNUM* s = BN_bin2bn(reinterpret_cast<const unsigned char*>(rs.data()) + half,
                              static_cast<int>(half), nullptr);
        ECDSA_SIG_set0(sig, r, s);
        const int ok = ECDSA_do_verify(reinterpret_cast<const unsigned char*>(digest.data()),
                                       static_cast<int>(digest.size()), sig, key);
        ECDSA_SIG_free(sig);
        return ok == 1;
    }

    std::string StdB64Decode(const std::string& s) {
        std::string out(s.size() / 4 * 3 + 3, '\0');
        const int n =
            EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                            reinterpret_cast<const unsigned char*>(s.data()), static_cast<int>(s.size()));
        size_t pad = 0;
        while (pad < s.size() && s[s.size() - 1 - pad] == '=')
            pad++;
        out.resize(n < 0 ? 0 : static_cast<size_t>(n) - pad);
        return out;
    }

    void WritePng(const std::string& path, int w, int h, const std::vector<unsigned char>& rgba) {
        size_t len = 0;
        void* png = tdefl_write_image_to_png_file_in_memory(rgba.data(), w, h, 4, &len);
        std::ofstream(path, std::ios::binary)
            .write(static_cast<const char*>(png), static_cast<std::streamsize>(len));
        mz_free(png);
    }

    std::vector<unsigned char> Gradient(int w, int h) {
        std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned char* p = &px[(static_cast<size_t>(y) * w + x) * 4];
                p[0] = static_cast<unsigned char>(x * 3);
                p[1] = static_cast<unsigned char>(y * 7);
                p[2] = 77;
                p[3] = 255;
            }
        return px;
    }

    std::string Jwt(const std::string& headerJson, const std::string& payloadJson) {
        return B64Url(headerJson) + "." + B64Url(payloadJson) + "." + B64Url(std::string("sig"));
    }
}

static void TestSignatures() {
    const std::string x = FromHex("063f059c42433992182ab753c50e2cae04e48de93a0c89770d4f29297384ce7b");
    const std::string y = FromHex("cab5b6658b20be402fa3f2ea003464c15541e2a45595cc049dbe6baeb930c39b");
    const std::string sig =
        FromHex("0000000101dc38fa295d5687d8fce073e3d52f8ba5de2eab06359c72ac53e858d987dd8ad9ad55127307d4abc1f4"
                "cc8f8a32ff8dd91c953fc92ac73c7e131ff9eaa51ef0d841ee08b59237ff");
    const std::string sig2 =
        FromHex("0000000101dc38fa295d5687c99704eaa97b3060d2252e6d79638d0e4391148aeec5ba432f85b41dae602cd2115c"
                "dae61538c28b0ff71f74201393c392ba645711f27c4f202ffe3a9d6413e0");
    const int64_t nanos = 1760000000123456700LL;
    EC_KEY* key = KeyFromXY(x, y);
    const std::string body = R"({"hello":"world"})";
    const std::string path = "/xsts/authorize?a=b";
    const std::string auth = "XBL3.0 x=abc;def";

    CHECK(sig.size() == 12 + 64);
    CHECK(VerifyRaw(key, xbox::detail::RequestDigest("POST", path, auth, body, nanos), sig.substr(12)));
    CHECK(VerifyRaw(key, xbox::detail::RequestDigest("POST", path, auth, body, nanos, 5), sig2.substr(12)));
    CHECK(
        !VerifyRaw(key, xbox::detail::RequestDigest("POST", path, auth, body + " ", nanos), sig.substr(12)));
    CHECK(!VerifyRaw(key, xbox::detail::RequestDigest("POST", path, "", body, nanos), sig.substr(12)));
    EC_KEY_free(key);

    EC_KEY* mine = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(mine);
    const std::string mySig = StdB64Decode(xbox::detail::SignRequest(mine, "POST", path, auth, body, nanos));
    CHECK(mySig.size() == 76);
    CHECK(mySig.substr(0, 12) == sig.substr(0, 12));
    CHECK(VerifyRaw(mine, xbox::detail::RequestDigest("POST", path, auth, body, nanos), mySig.substr(12)));
    EC_KEY_free(mine);
}

static void TestTime() {
    CHECK(xbox::detail::ParseIsoTime("2025-10-09T08:53:20.1234567Z") == 1760000000);
    CHECK(xbox::detail::ParseIsoTime("2025-10-09T08:53:20Z") == 1760000000);
    CHECK(xbox::detail::ParseIsoTime("garbage") == 0);
    CHECK(xbox::detail::ParseHttpDate("Thu, 09 Oct 2025 08:53:20 GMT") == 1760000000);
    CHECK(xbox::detail::ParseHttpDate("nonsense") == 0);
}

static void TestSkins() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "perlin_auth_selftest";
    fs::create_directories(dir);
    WritePng((dir / "skin64.png").string(), 64, 64, Gradient(64, 64));
    WritePng((dir / "skin32.png").string(), 64, 32, Gradient(64, 32));
    WritePng((dir / "skin128.png").string(), 128, 128, Gradient(128, 128));
    WritePng((dir / "bad.png").string(), 50, 50, Gradient(50, 50));
    WritePng((dir / "cape.png").string(), 64, 32, Gradient(64, 32));
    std::ofstream(dir / "notpng.png") << "hello";

    LoadedSkin out;
    std::string err;
    SkinConfig cfg;
    CHECK(LoadSkinConfig(cfg, out, err) && !out.hasSkin() && !out.hasCape());

    cfg.skinPath = (dir / "skin64.png").string();
    cfg.capePath = (dir / "cape.png").string();
    cfg.slimArms = true;
    CHECK(LoadSkinConfig(cfg, out, err));
    CHECK(out.skinWidth == 64 && out.skinHeight == 64 && out.skinRgba.size() == 64 * 64 * 4);
    CHECK(out.capeWidth == 64 && out.capeHeight == 32 && out.capeRgba.size() == 64 * 32 * 4);
    CHECK(out.slimArms && !out.skinId.empty() && !out.capeId.empty());
    const std::string id64 = out.skinId;

    cfg.skinPath = (dir / "skin128.png").string();
    CHECK(LoadSkinConfig(cfg, out, err) && out.skinWidth == 128 && out.skinHeight == 128 &&
          out.skinId != id64);

    cfg.skinPath = (dir / "skin32.png").string();
    CHECK(LoadSkinConfig(cfg, out, err) && out.skinWidth == 64 && out.skinHeight == 64);
    const std::vector<unsigned char> legacy = Gradient(64, 32);
    CHECK(std::memcmp(out.skinRgba.data(), legacy.data(), legacy.size()) == 0);
    auto pixel = [&](int px, int py) { return &out.skinRgba[(static_cast<size_t>(py) * 64 + px) * 4]; };
    CHECK(std::memcmp(pixel(23, 48), &legacy[(16 * 64 + 4) * 4], 4) == 0);
    CHECK(std::memcmp(pixel(20, 48), &legacy[(16 * 64 + 7) * 4], 4) == 0);
    CHECK(std::memcmp(pixel(39, 52), &legacy[(20 * 64 + 44) * 4], 4) == 0);
    CHECK(pixel(0, 40)[3] == 0);

    cfg.skinPath = (dir / "bad.png").string();
    CHECK(!LoadSkinConfig(cfg, out, err) && err.contains("50x50"));
    cfg.skinPath = (dir / "missing.png").string();
    CHECK(!LoadSkinConfig(cfg, out, err) && err.contains("missing.png"));
    cfg.skinPath = (dir / "notpng.png").string();
    CHECK(!LoadSkinConfig(cfg, out, err));
    cfg.skinPath.clear();
    cfg.capePath = (dir / "skin128.png").string();
    CHECK(!LoadSkinConfig(cfg, out, err) && err.contains("cape"));
    fs::remove_all(dir);
}

static bool VerifyJwtEs384(const std::string& jwt, const std::string& x5uB64) {
    const size_t a = jwt.find('.'), b = jwt.find('.', a + 1);
    const std::string input = jwt.substr(0, b);
    const std::string raw = B64UrlDecode(jwt.substr(b + 1));
    const std::string der = StdB64Decode(x5uB64);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
    EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size()));
    if (!pkey || raw.size() != 96)
        return false;
    EC_KEY* ec = EVP_PKEY_get1_EC_KEY(pkey);
    unsigned char digest[48];
    EVP_Digest(input.data(), input.size(), digest, nullptr, EVP_sha384(), nullptr);
    ECDSA_SIG* sig = ECDSA_SIG_new();
    ECDSA_SIG_set0(sig, BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()), 48, nullptr),
                   BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()) + 48, 48, nullptr));
    const int ok = ECDSA_do_verify(digest, 48, sig, ec);
    ECDSA_SIG_free(sig);
    EC_KEY_free(ec);
    EVP_PKEY_free(pkey);
    return ok == 1;
}

static void TestIdentity() {
    ClientIdentity id;
    CHECK(id.Valid());
    std::string err;
    CHECK(BuildIdentityJson(id, nullptr, err).empty());
    id.BuildMultiplayerToken("Tester", RandomUuid());

    const auto offline = json::Document::Parse(BuildIdentityJson(id, nullptr, err));
    CHECK(offline && offline->root()["AuthenticationType"].integer(-1) == 2);
    CHECK(offline && offline->root()["Token"].str() == id.MultiplayerToken());

    const std::string mojangA = "MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEAAAA";
    const std::string chainJson =
        "{\"chain\":[\"" +
        Jwt("{\"alg\":\"ES384\",\"x5u\":\"" + mojangA + "\"}", "{\"identityPublicKey\":\"B\"}") + "\",\"" +
        Jwt("{\"alg\":\"ES384\",\"x5u\":\"B\"}", "{\"extraData\":{\"displayName\":\"Tester\"}}") + "\"]}";
    id.SetMultiplayerToken("oidc.token.value");
    const std::string online = BuildIdentityJson(id, &chainJson, err);
    CHECK(!online.empty());
    auto doc = json::Document::Parse(online);
    CHECK(doc && doc->root()["AuthenticationType"].integer(-1) == 0);
    CHECK(doc && doc->root()["Token"].str() == "oidc.token.value");
    auto cert = json::Document::Parse(doc->root()["Certificate"].str());
    CHECK(cert && cert->root()["chain"].size() == 3);
    const std::string first = cert->root()["chain"][0].str();
    auto header = json::Document::Parse(B64UrlDecode(first.substr(0, first.find('.'))));
    CHECK(header && header->root()["x5u"].str() == id.PublicKeyB64());
    CHECK(VerifyJwtEs384(first, id.PublicKeyB64()));
    const size_t a = first.find('.'), b = first.find('.', a + 1);
    auto payload = json::Document::Parse(B64UrlDecode(first.substr(a + 1, b - a - 1)));
    CHECK(payload && payload->root()["identityPublicKey"].str() == mojangA);
    CHECK(payload && payload->root()["certificateAuthority"].boolean());
    CHECK(cert->root()["chain"][1].str() == cert->root()["chain"][1].str());
    CHECK(cert->root()["chain"][2].str().contains('.'));

    CHECK(BuildIdentityJson(id, new std::string("{\"chain\":[\"nodots\"]}"), err).empty());
}

namespace {
    struct Mock {
        httplib::Server server;
        std::thread thread;
        int port = 0;
        std::atomic<int> devicePolls{0};
        std::atomic<int> refreshes{0};
        std::atomic<int> deviceAuths{0};
        std::set<std::string> seen;
        std::string chainKey;
        std::string lastMcAuthorization, lastChainAuthorization, lastPublicKeyForToken;

        static std::string Token(const char* name, const char* extra = "") {
            return std::string("{\"IssueInstant\":\"2025-10-09T08:53:20.0000000Z\",\"NotAfter\":\"2099-01-"
                               "01T00:00:00.0000000Z\","
                               "\"Token\":\"") +
                   name + "\"" + extra + "}";
        }

        Mock() {
            auto note = [this](const httplib::Request& r) { seen.insert(r.path); };
            server.Post("/login.live.com/oauth20_connect.srf", [=](const httplib::Request& r,
                                                                   httplib::Response& res) {
                note(r);
                res.set_content(
                    "{\"user_code\":\"ABCD1234\",\"device_code\":\"dev-code\",\"verification_uri\":"
                    "\"https://www.microsoft.com/link\",\"interval\":1,\"expires_in\":900}",
                    "application/json");
            });
            server.Post("/login.live.com/oauth20_token.srf", [=](const httplib::Request& r,
                                                                 httplib::Response& res) {
                note(r);
                const std::string grant = r.get_param_value("grant_type");
                if (grant == "refresh_token") {
                    refreshes++;
                    if (r.get_param_value("refresh_token") != "refresh-1") {
                        res.status = 400;
                        res.set_content("{\"error\":\"invalid_grant\"}", "application/json");
                        return;
                    }
                    res.set_content(
                        "{\"access_token\":\"access-2\",\"refresh_token\":\"refresh-1\",\"expires_in\":3600}",
                        "application/json");
                    return;
                }
                if (devicePolls++ == 0) {
                    res.status = 400;
                    res.set_content("{\"error\":\"authorization_pending\"}", "application/json");
                    return;
                }
                res.set_content(
                    "{\"access_token\":\"access-1\",\"refresh_token\":\"refresh-1\",\"expires_in\":3600}",
                    "application/json");
            });
            server.Get("/title.mgt.xboxlive.com/titles/default/endpoints", [=](const httplib::Request& r,
                                                                               httplib::Response& res) {
                note(r);
                res.set_content(
                    "{\"EndPoints\":[{\"Protocol\":\"https\",\"Host\":\"*.xboxlive.com\",\"HostType\":"
                    "\"wildcard\","
                    "\"RelyingParty\":\"http://xboxlive.com\",\"SignaturePolicyIndex\":0},"
                    "{\"Protocol\":\"https\",\"Host\":\"sisu.xboxlive.com\",\"HostType\":\"fqdn\","
                    "\"RelyingParty\":\"http://xboxlive.com\",\"SignaturePolicyIndex\":1},"
                    "{\"Protocol\":\"https\",\"Host\":\"*.playfabapi.com\",\"HostType\":\"wildcard\","
                    "\"RelyingParty\":\"rp://playfabapi.com/\",\"SignaturePolicyIndex\":0}],"
                    "\"SignaturePolicies\":[{\"Version\":1,\"MaxBodyBytes\":8192,\"SupportedAlgorithms\":["
                    "\"ES256\"]},"
                    "{\"Version\":1,\"MaxBodyBytes\":0,\"ExtraHeaders\":[\"User-Agent\"]}]}",
                    "application/json");
            });
            server.Post("/device.auth.xboxlive.com/device/authenticate", [=](const httplib::Request& r,
                                                                             httplib::Response& res) {
                note(r);
                deviceAuths++;
                auto doc = json::Document::Parse(r.body);
                if (!doc || doc->root()["Properties"]["ProofKey"]["crv"].str() != "P-256" ||
                    !r.has_header("Signature") ||
                    doc->root()["Properties"]["AuthMethod"].str() != "ProofOfPossession") {
                    res.status = 400;
                    return;
                }
                res.set_header("Date", "Thu, 09 Oct 2025 08:53:20 GMT");
                res.set_content(Token("device-token", ",\"DisplayClaims\":{\"xdi\":{\"did\":\"1\"}}"),
                                "application/json");
            });
            server.Post("/sisu.xboxlive.com/authorize", [=](const httplib::Request& r,
                                                            httplib::Response& res) {
                note(r);
                auto doc = json::Document::Parse(r.body);
                if (!doc ||
                    doc->root()["AccessToken"].str() != "t=access-1" &&
                        doc->root()["AccessToken"].str() != "t=access-2" ||
                    doc->root()["DeviceToken"].str() != "device-token" || !r.has_header("Signature") ||
                    doc->root()["AppId"].str() != "0000000048183522") {
                    res.status = 400;
                    res.set_header("X-Err", "2148916233");
                    return;
                }
                res.set_content("{\"TitleToken\":" +
                                    Token("title-token", ",\"DisplayClaims\":{\"xti\":{\"tid\":\"1\"}}") +
                                    ",\"UserToken\":" +
                                    Token("user-token", ",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"uhs1\"}]}") +
                                    ",\"AuthorizationToken\":" +
                                    Token("sisu-xsts", ",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"uhs1\"}]}") +
                                    "}",
                                "application/json");
            });
            server.Post("/xsts.auth.xboxlive.com/xsts/authorize", [=](const httplib::Request& r,
                                                                      httplib::Response& res) {
                note(r);
                auto doc = json::Document::Parse(r.body);
                const json::View props = doc ? doc->root()["Properties"] : json::View();
                if (!doc || props["DeviceToken"].str() != "device-token" ||
                    props["TitleToken"].str() != "title-token" ||
                    props["UserTokens"][0].str() != "user-token" || !r.has_header("Signature")) {
                    res.status = 401;
                    res.set_content("{\"XErr\":2148916233}", "application/json");
                    return;
                }
                const std::string rp = doc->root()["RelyingParty"].str();
                const bool xboxLive = rp == "http://xboxlive.com";
                res.set_content(Token(rp == "rp://playfabapi.com/" ? "xsts-playfab"
                                      : xboxLive                   ? "xsts-xbl"
                                                                   : "xsts-multiplayer",
                                      xboxLive ? ",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"uhs1\",\"gtg\":"
                                                 "\"MockGamer\",\"xid\":\"2535400000000001\"}]}"
                                               : ",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"uhs1\"}]}"),
                                "application/json");
            });
            server.Post("/20ca2.playfabapi.com/Client/LoginWithXbox", [=](const httplib::Request& r,
                                                                          httplib::Response& res) {
                note(r);
                auto doc = json::Document::Parse(r.body);
                if (!doc || doc->root()["XboxToken"].str() != "XBL3.0 x=uhs1;xsts-playfab" ||
                    !doc->root()["CreateAccount"].boolean()) {
                    res.status = 400;
                    res.set_content("{\"code\":400,\"errorMessage\":\"bad xbox token\"}", "application/json");
                    return;
                }
                res.set_content("{\"code\":200,\"status\":\"OK\",\"data\":{\"SessionTicket\":\"ticket-1\","
                                "\"PlayFabId\":\"PF1\"}}",
                                "application/json");
            });
            server.Get(
                R"(/client.discovery.minecraft-services.net/api/v1.0/discovery/MinecraftPE/builds/(.*))",
                [=](const httplib::Request& r, httplib::Response& res) {
                    note(r);
                    res.set_content(
                        "{\"result\":{\"serviceEnvironments\":{\"auth\":{\"prod\":{\"serviceUri\":"
                        "\"https://authorization.franchise.minecraft-services.net\",\"issuer\":\"x\","
                        "\"playFabTitleId\":\"20CA2\"}}}}}",
                        "application/json");
                });
            server.Post("/authorization.franchise.minecraft-services.net/api/v1.0/session/start",
                        [=](const httplib::Request& r, httplib::Response& res) {
                            note(r);
                            auto doc = json::Document::Parse(r.body);
                            if (!doc || doc->root()["user"]["token"].str() != "ticket-1" ||
                                doc->root()["user"]["tokenType"].str() != "PlayFab" ||
                                doc->root()["device"]["playFabTitleId"].str() != "20CA2" ||
                                doc->root()["device"]["gameVersion"].str() != "1.26.52") {
                                res.status = 400;
                                return;
                            }
                            res.set_content(
                                "{\"result\":{\"authorizationHeader\":\"MCToken session.jwt.value\","
                                "\"validUntil\":\"2099-01-01T00:00:00Z\"}}",
                                "application/json");
                        });
            server.Post("/authorization.franchise.minecraft-services.net/api/v1.0/multiplayer/session/start",
                        [=](const httplib::Request& r, httplib::Response& res) {
                            note(r);
                            lastMcAuthorization = r.get_header_value("Authorization");
                            auto doc = json::Document::Parse(r.body);
                            lastPublicKeyForToken = doc ? doc->root()["publicKey"].str() : "";
                            res.set_content("{\"result\":{\"signedToken\":\"oidc.multiplayer.token\"}}",
                                            "application/json");
                        });
            server.Post("/multiplayer.minecraft.net/authentication", [=](const httplib::Request& r,
                                                                         httplib::Response& res) {
                note(r);
                lastChainAuthorization = r.get_header_value("Authorization");
                auto doc = json::Document::Parse(r.body);
                chainKey = doc ? doc->root()["identityPublicKey"].str() : "";
                res.set_content(
                    "{\"chain\":[\"" +
                        Jwt("{\"alg\":\"ES384\",\"x5u\":\"AAAA\"}", "{\"identityPublicKey\":\"BBBB\"}") +
                        "\",\"" +
                        Jwt("{\"alg\":\"ES384\",\"x5u\":\"BBBB\"}",
                            "{\"extraData\":{\"displayName\":\"MockGamer\",\"XUID\":\"2535400000000001\"}}") +
                        "\"]}",
                    "application/json");
            });
            port = server.bind_to_any_port("127.0.0.1");
            thread = std::thread([this] { server.listen_after_bind(); });
            server.wait_until_ready();
        }
        ~Mock() {
            server.stop();
            thread.join();
        }
    };
}

static void TestFlow() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "perlin_auth_flow";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string cache = (dir / "auth.json").string();

    Mock mock;
    xbox::detail::SetTestEndpoint("127.0.0.1", static_cast<uint16_t>(mock.port));

    std::string promptUrl, promptCode, err;
    ClientIdentity id;
    {
        xbox::Account account(cache);
        CHECK(!account.HasSavedLogin());
        CHECK(account.SignIn(
            [&](const std::string& u, const std::string& c) {
                promptUrl = u;
                promptCode = c;
            },
            err));
        CHECK(promptUrl == "https://www.microsoft.com/link" && promptCode == "ABCD1234");
        CHECK(mock.devicePolls >= 2);
        CHECK(account.HasSavedLogin());

        xbox::Credentials creds;
        CHECK(account.LoginMinecraft(id.PublicKeyB64(), "1.26.52", creds, err));
        if (!err.empty())
            std::printf("  flow error: %s\n", err.c_str());
        CHECK(creds.multiplayerToken == "oidc.multiplayer.token");
        CHECK(creds.gamertag == "MockGamer" && creds.xuid == "2535400000000001");
        auto chain = json::Document::Parse(creds.chainJson);
        CHECK(chain && chain->root()["chain"].size() == 2);
        CHECK(mock.lastMcAuthorization == "MCToken session.jwt.value");
        CHECK(mock.lastChainAuthorization == "XBL3.0 x=uhs1;xsts-multiplayer");
        CHECK(mock.chainKey == id.PublicKeyB64() && mock.lastPublicKeyForToken == id.PublicKeyB64());
        for (const char* path :
             {"/login.live.com/oauth20_connect.srf", "/sisu.xboxlive.com/authorize",
              "/device.auth.xboxlive.com/device/authenticate", "/xsts.auth.xboxlive.com/xsts/authorize",
              "/20ca2.playfabapi.com/Client/LoginWithXbox",
              "/authorization.franchise.minecraft-services.net/api/v1.0/session/start",
              "/authorization.franchise.minecraft-services.net/api/v1.0/multiplayer/session/start",
              "/multiplayer.minecraft.net/authentication"})
            CHECK(mock.seen.count(path) == 1);
    }

    {
        xbox::Account again(cache);
        CHECK(again.HasSavedLogin());
        bool prompted = false;
        CHECK(again.SignIn([&](const std::string&, const std::string&) { prompted = true; }, err));
        CHECK(!prompted && mock.refreshes == 1);
        xbox::Credentials creds;
        CHECK(again.LoginMinecraft(id.PublicKeyB64(), "1.26.52", creds, err));
        CHECK(creds.multiplayerToken == "oidc.multiplayer.token");
        CHECK(creds.gamertag == "MockGamer");
        CHECK(mock.deviceAuths == 1);
        std::string gamertag;
        CHECK(again.FetchGamertag(gamertag, err) && gamertag == "MockGamer");
    }

    {
        xbox::Account saved(cache);
        std::string gamertag;
        CHECK(saved.SignInSaved(err));
        CHECK(saved.FetchGamertag(gamertag, err) && gamertag == "MockGamer");
        xbox::Account none((dir / "missing.json").string());
        CHECK(!none.SignInSaved(err));
    }

    {
        {
            std::ofstream f(cache);
            f << R"({"refreshToken":"revoked"})";
        }
        xbox::Account stale(cache);
        bool prompted = false;
        CHECK(stale.SignIn([&](const std::string&, const std::string&) { prompted = true; }, err));
        CHECK(prompted);
    }

    xbox::Account::SignOut(cache);
    CHECK(!fs::exists(cache));
    xbox::detail::SetTestEndpoint("", 0);
    fs::remove_all(dir);
}

static std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

static void TestCipher() {
    uint8_t key[32];
    for (int i = 0; i < 32; i++)
        key[i] = uint8_t(i * 3);
    PacketCipher c;
    CHECK(c.InitWithKey(key));
    const auto c1 = c.Encrypt(Bytes(std::string(1, '\0') + "hello batch one"));
    const auto c2 =
        c.Encrypt(Bytes(std::string(1, '\xff') + "second, a bit longer batch payload 0123456789"));
    auto hex = [](const std::vector<uint8_t>& v) { return Hex(std::string(v.begin(), v.end())); };
    CHECK(hex(c1) == "25a37bff9dee666daffeaccc2c7e64266bbb1932258d960a");
    CHECK(hex(c2) == "1af35bde0ebdaf1b7a7ee4e3a170dd667f3d8ce6ff53da4f8b55e345df65c4ae2805f4af9394dddfab0d101"
                     "ab1fd445ecdadd4d09d94");
    PacketCipher d;
    CHECK(d.InitWithKey(key));
    std::vector<uint8_t> plain;
    CHECK(d.Decrypt(c1, plain) && plain.size() == 16 && plain[0] == 0);
    CHECK(d.Decrypt(c2, plain) && plain.size() == 46 && plain[0] == 0xff);
    auto tampered = c1;
    tampered[3] ^= 1;
    PacketCipher e;
    CHECK(e.InitWithKey(key));
    CHECK(!e.Decrypt(tampered, plain));

    ClientIdentity client, server;
    const std::string salt = "0123456789abcdef";
    const std::string token = Jwt("{\"alg\":\"ES384\",\"x5u\":\"" + server.PublicKeyB64() + "\"}",
                                  "{\"salt\":\"" + B64Standard(salt) + "\"}");
    PacketCipher mine;
    std::string err;
    CHECK(mine.InitFromHandshake(client, token, err));

    const std::string clientDer = StdB64Decode(client.PublicKeyB64());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(clientDer.data());
    EVP_PKEY* pk = d2i_PUBKEY(nullptr, &p, static_cast<long>(clientDer.size()));
    EC_KEY* clientPub = EVP_PKEY_get1_EC_KEY(pk);
    unsigned char secret[64];
    const int n =
        ECDH_compute_key(secret, sizeof(secret), EC_KEY_get0_public_key(clientPub), server.Key(), nullptr);
    CHECK(n == 48);
    std::string input = salt + std::string(reinterpret_cast<char*>(secret), size_t(n));
    uint8_t derived[32];
    EVP_Digest(input.data(), input.size(), derived, nullptr, EVP_sha256(), nullptr);
    PacketCipher theirs;
    CHECK(theirs.InitWithKey(derived));
    const auto wire = mine.Encrypt(Bytes("batch from the client"));
    CHECK(theirs.Decrypt(wire, plain) && std::string(plain.begin(), plain.end()) == "batch from the client");
    const auto back = theirs.Encrypt(Bytes("batch from the server"));
    CHECK(mine.Decrypt(back, plain) && std::string(plain.begin(), plain.end()) == "batch from the server");
    EC_KEY_free(clientPub);
    EVP_PKEY_free(pk);

    CHECK(!PacketCipher().InitFromHandshake(client, "not.a.token", err));
}

int main() {
    TestCipher();
    TestSignatures();
    TestTime();
    TestSkins();
    TestIdentity();
    TestFlow();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("auth_selftest: ok\n");
    return 0;
}
