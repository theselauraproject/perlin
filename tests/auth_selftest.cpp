#include "client/systems/resources/SkinLoader.hpp"
#include "network/ClientIdentity.hpp"
#include "network/Http.hpp"
#include "network/LoginBuilder.hpp"
#include "network/PacketCipher.hpp"
#include "network/TokenStore.hpp"
#include "util/FileIo.hpp"
#include "network/XboxAuth.hpp"
#include "network/XboxRelyingParties.hpp"
#include "util/Json.hpp"

#include <httplib.h>
#include <miniz.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

    const std::string noDots = "{\"chain\":[\"nodots\"]}";
    CHECK(BuildIdentityJson(id, &noDots, err).empty());
}

namespace {
    std::string IsoTime(int64_t unix) {
        int64_t days = unix / 86400;
        const int64_t rem = unix % 86400;
        days += 719468;
        const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
        const int64_t doe = days - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int64_t y = yoe + era * 400;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        const int64_t d = doy - (153 * mp + 2) / 5 + 1;
        const int64_t m = mp < 10 ? mp + 3 : mp - 9;
        if (m <= 2)
            y++;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.0000000Z",
                      static_cast<long long>(y), static_cast<long long>(m), static_cast<long long>(d),
                      static_cast<long long>(rem / 3600), static_cast<long long>(rem % 3600 / 60),
                      static_cast<long long>(rem % 60));
        return buf;
    }

    std::string HttpDate(int64_t unix) {
        static const char* days[] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        const std::string iso = IsoTime(unix);
        int y, m, d, hh, mm, ss;
        std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
                      days[(unix / 86400) % 7], d, months[m - 1], y, hh, mm, ss);
        return buf;
    }

    struct Mock {
        httplib::Server server;
        std::thread thread;
        int port = 0;
        std::atomic<int> devicePolls{0};
        std::atomic<int> refreshes{0};
        std::atomic<int> deviceAuths{0};
        std::atomic<int> sisuCalls{0};
        std::atomic<int> xstsCalls{0};
        std::atomic<int64_t> now{0};
        std::atomic<int64_t> sisuLifetime{86400 * 30};
        std::atomic<int64_t> xstsLifetime{86400};
        std::atomic<int64_t> deviceLifetime{86400 * 30};
        std::atomic<int> minTitleGeneration{0};
        std::atomic<int> sisuDelayMs{0};
        std::atomic<int> xstsDelayMs{0};
        std::atomic<int> xstsStatusOverride{0};
        std::atomic<int> refreshStatusOverride{0};
        std::atomic<bool> nsalFails{false};
        std::atomic<bool> nsalWrongPlayFab{false};
        std::atomic<int> refreshRotation{0};
        std::mutex m;
        std::vector<std::string> log;
        std::vector<std::string> xstsParties;
        std::string chainKey;
        std::string lastMcAuthorization, lastChainAuthorization, lastPublicKeyForToken;

        int Count(const std::string& path) {
            std::lock_guard<std::mutex> l(m);
            return static_cast<int>(std::count(log.begin(), log.end(), path));
        }
        int PartyCount(const std::string& party) {
            std::lock_guard<std::mutex> l(m);
            return static_cast<int>(std::count(xstsParties.begin(), xstsParties.end(), party));
        }
        std::vector<std::string> Log() {
            std::lock_guard<std::mutex> l(m);
            return log;
        }
        std::vector<std::string> Parties() {
            std::lock_guard<std::mutex> l(m);
            return xstsParties;
        }
        void Reset() {
            std::lock_guard<std::mutex> l(m);
            log.clear();
            xstsParties.clear();
        }

        std::string Token(const std::string& name, int64_t lifetime, const char* extra = "") {
            const int64_t t = now.load();
            const std::string notAfter = t == 0 ? "2099-01-01T00:00:00.0000000Z" : IsoTime(t + lifetime);
            return std::string("{\"IssueInstant\":\"2025-10-09T08:53:20.0000000Z\",\"NotAfter\":\"") + notAfter +
                   "\",\"Token\":\"" + name + "\"" + extra + "}";
        }

        static int Generation(const std::string& token) {
            const size_t h = token.find('#');
            return h == std::string::npos ? 0 : std::atoi(token.c_str() + h + 1);
        }

        void Stamp(httplib::Response& res) {
            const int64_t t = now.load();
            res.set_header("Date", HttpDate(t == 0 ? 1760000000 : t));
        }

        Mock() {
            auto note = [this](const httplib::Request& r) {
                std::lock_guard<std::mutex> l(m);
                log.push_back(r.path);
            };
            server.Post("/login.live.com/oauth20_connect.srf", [=, this](const httplib::Request& r,
                                                                   httplib::Response& res) {
                note(r);
                res.set_content(
                    "{\"user_code\":\"ABCD1234\",\"device_code\":\"dev-code\",\"verification_uri\":"
                    "\"https://www.microsoft.com/link\",\"interval\":1,\"expires_in\":900}",
                    "application/json");
            });
            server.Post("/login.live.com/oauth20_token.srf", [=, this](const httplib::Request& r,
                                                                 httplib::Response& res) {
                note(r);
                const std::string grant = r.get_param_value("grant_type");
                if (grant == "refresh_token") {
                    refreshes++;
                    if (refreshStatusOverride != 0) {
                        res.status = refreshStatusOverride;
                        res.set_content("{\"error\":\"temporarily_unavailable\"}", "application/json");
                        return;
                    }
                    const std::string given = r.get_param_value("refresh_token");
                    if (given.rfind("refresh-", 0) != 0) {
                        res.status = 400;
                        res.set_content("{\"error\":\"invalid_grant\"}", "application/json");
                        return;
                    }
                    const std::string account = given.substr(8, given.find('.') == std::string::npos
                                                                     ? std::string::npos
                                                                     : given.find('.') - 8);
                    const std::string rotated =
                        refreshRotation > 0 ? "refresh-" + account + "." + std::to_string(refreshes.load())
                                            : given;
                    res.set_content("{\"access_token\":\"access-" + account + "\",\"refresh_token\":\"" +
                                        rotated + "\",\"expires_in\":3600}",
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
            server.Get("/title.mgt.xboxlive.com/titles/default/endpoints", [=, this](const httplib::Request& r,
                                                                               httplib::Response& res) {
                note(r);
                if (nsalFails) {
                    res.status = 503;
                    return;
                }
                std::string playFabParty = nsalWrongPlayFab ? "rp://playfabapi.com/" : "http://playfab.xboxlive.com/";
                res.set_content(
                    "{\"EndPoints\":[{\"Protocol\":\"https\",\"Host\":\"*.xboxlive.com\",\"HostType\":"
                    "\"wildcard\","
                    "\"RelyingParty\":\"http://xboxlive.com\",\"SignaturePolicyIndex\":0},"
                    "{\"Protocol\":\"https\",\"Host\":\"sisu.xboxlive.com\",\"HostType\":\"fqdn\","
                    "\"RelyingParty\":\"http://xboxlive.com\",\"SignaturePolicyIndex\":1},"
                    "{\"Protocol\":\"https\",\"Host\":\"*.playfabapi.com\",\"HostType\":\"wildcard\","
                    "\"RelyingParty\":\"" + playFabParty + "\",\"SignaturePolicyIndex\":0}],"
                    "\"SignaturePolicies\":[{\"Version\":1,\"MaxBodyBytes\":8192,\"SupportedAlgorithms\":["
                    "\"ES256\"]},"
                    "{\"Version\":1,\"MaxBodyBytes\":0,\"ExtraHeaders\":[\"User-Agent\"]}]}",
                    "application/json");
            });
            server.Post("/device.auth.xboxlive.com/device/authenticate", [=, this](const httplib::Request& r,
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
                Stamp(res);
                res.set_content(Token("device-token", deviceLifetime, ",\"DisplayClaims\":{\"xdi\":{\"did\":\"1\"}}"),
                                "application/json");
            });
            server.Post("/sisu.xboxlive.com/authorize", [=, this](const httplib::Request& r,
                                                            httplib::Response& res) {
                note(r);
                sisuCalls++;
                if (sisuDelayMs > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(sisuDelayMs.load()));
                auto doc = json::Document::Parse(r.body);
                const std::string access = doc ? doc->root()["AccessToken"].str() : std::string();
                if (!doc || access.rfind("t=access-", 0) != 0 ||
                    doc->root()["DeviceToken"].str().rfind("device-token", 0) != 0 || !r.has_header("Signature") ||
                    doc->root()["AppId"].str() != "0000000048183522") {
                    res.status = 400;
                    res.set_header("X-Err", "2148916233");
                    return;
                }
                const std::string account = access.substr(9);
                const std::string gen = "#" + std::to_string(sisuCalls.load());
                const std::string uhs = "uhs" + account;
                Stamp(res);
                res.set_content("{\"TitleToken\":" +
                                    Token("title-token" + gen, sisuLifetime, ",\"DisplayClaims\":{\"xti\":{\"tid\":\"1\"}}") +
                                    ",\"UserToken\":" +
                                    Token("user-token-" + account + gen, sisuLifetime,
                                          (",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"" + uhs + "\"}]}").c_str()) +
                                    ",\"AuthorizationToken\":" +
                                    Token("sisu-xsts", sisuLifetime,
                                          (",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"" + uhs + "\"}]}").c_str()) +
                                    "}",
                                "application/json");
            });
            server.Post("/xsts.auth.xboxlive.com/xsts/authorize", [=, this](const httplib::Request& r,
                                                                      httplib::Response& res) {
                note(r);
                xstsCalls++;
                if (xstsDelayMs > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(xstsDelayMs.load()));
                if (xstsStatusOverride != 0) {
                    res.status = xstsStatusOverride;
                    return;
                }
                auto doc = json::Document::Parse(r.body);
                const json::View props = doc ? doc->root()["Properties"] : json::View();
                const std::string title = props["TitleToken"].str();
                const std::string userToken = props["UserTokens"][0].str();
                if (!doc || props["DeviceToken"].str().rfind("device-token", 0) != 0 ||
                    title.rfind("title-token", 0) != 0 || userToken.rfind("user-token-", 0) != 0 ||
                    !r.has_header("Signature") || Generation(title) < minTitleGeneration) {
                    res.status = 401;
                    if (doc && Generation(title) >= minTitleGeneration)
                        res.set_content("{\"XErr\":2148916233}", "application/json");
                    return;
                }
                const std::string rp = doc->root()["RelyingParty"].str();
                {
                    std::lock_guard<std::mutex> l(m);
                    xstsParties.push_back(rp);
                }
                const size_t dash = userToken.find('-', 11);
                const std::string account = userToken.substr(11, dash == std::string::npos ? 0 : dash - 11);
                const std::string uhs = "uhs" + std::string(userToken.substr(11, userToken.find('#') - 11));
                const bool xboxLive = rp == "http://xboxlive.com";
                const std::string name = rp == "http://playfab.xboxlive.com/" ? "xsts-playfab"
                                         : xboxLive                           ? "xsts-xbl"
                                                                              : "xsts-multiplayer";
                (void)account;
                Stamp(res);
                res.set_content(
                    Token(name + "#" + std::to_string(xstsCalls.load()), xstsLifetime,
                          xboxLive ? (",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"" + uhs +
                                      "\",\"gtg\":\"MockGamer\",\"xid\":\"2535400000000001\"}]}").c_str()
                                   : (",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"" + uhs + "\"}]}").c_str()),
                    "application/json");
            });
            server.Post("/20ca2.playfabapi.com/Client/LoginWithXbox", [=, this](const httplib::Request& r,
                                                                          httplib::Response& res) {
                note(r);
                auto doc = json::Document::Parse(r.body);
                if (!doc || doc->root()["XboxToken"].str().find(";xsts-playfab") == std::string::npos ||
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
                [=, this](const httplib::Request& r, httplib::Response& res) {
                    note(r);
                    res.set_content(
                        "{\"result\":{\"serviceEnvironments\":{\"auth\":{\"prod\":{\"serviceUri\":"
                        "\"https://authorization.franchise.minecraft-services.net\",\"issuer\":\"x\","
                        "\"playFabTitleId\":\"20CA2\"}}}}}",
                        "application/json");
                });
            server.Post("/authorization.franchise.minecraft-services.net/api/v1.0/session/start",
                        [=, this](const httplib::Request& r, httplib::Response& res) {
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
                        [=, this](const httplib::Request& r, httplib::Response& res) {
                            note(r);
                            std::lock_guard<std::mutex> l(m);
                            lastMcAuthorization = r.get_header_value("Authorization");
                            auto doc = json::Document::Parse(r.body);
                            lastPublicKeyForToken = doc ? doc->root()["publicKey"].str() : "";
                            res.set_content("{\"result\":{\"signedToken\":\"oidc.multiplayer.token\"}}",
                                            "application/json");
                        });
            server.Post("/multiplayer.minecraft.net/authentication", [=, this](const httplib::Request& r,
                                                                         httplib::Response& res) {
                note(r);
                std::lock_guard<std::mutex> l(m);
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
        CHECK(mock.lastChainAuthorization.rfind("XBL3.0 x=uhs1;xsts-multiplayer", 0) == 0);
        CHECK(mock.chainKey == id.PublicKeyB64() && mock.lastPublicKeyForToken == id.PublicKeyB64());
        for (const char* path :
             {"/login.live.com/oauth20_connect.srf", "/sisu.xboxlive.com/authorize",
              "/device.auth.xboxlive.com/device/authenticate",
              "/20ca2.playfabapi.com/Client/LoginWithXbox",
              "/authorization.franchise.minecraft-services.net/api/v1.0/session/start",
              "/authorization.franchise.minecraft-services.net/api/v1.0/multiplayer/session/start",
              "/multiplayer.minecraft.net/authentication"})
            CHECK(mock.Count(path) == 1);
        CHECK(mock.Count("/xsts.auth.xboxlive.com/xsts/authorize") == 3);
        CHECK(mock.PartyCount("http://playfab.xboxlive.com/") == 1);
        CHECK(mock.PartyCount("https://multiplayer.minecraft.net/") == 1);
        CHECK(mock.PartyCount("http://xboxlive.com") == 1);
        CHECK(mock.PartyCount("rp://playfabapi.com/") == 0);
    }

    {
        xbox::Account again(cache);
        CHECK(again.HasSavedLogin());
        bool prompted = false;
        CHECK(again.SignIn([&](const std::string&, const std::string&) { prompted = true; }, err));
        CHECK(!prompted && mock.refreshes == 0 && mock.sisuCalls == 1);
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


namespace {
    constexpr int64_t kBase = 1790000000;
    constexpr const char* kPlayFabParty = "http://playfab.xboxlive.com/";
    constexpr const char* kMultiplayerParty = "https://multiplayer.minecraft.net/";
    constexpr const char* kLiveParty = "http://xboxlive.com";

    struct FakeClock final : auth::Clock {
        std::atomic<int64_t> nanos;
        explicit FakeClock(int64_t sec) : nanos(sec * 1000000000LL) {}
        int64_t NowNanos() const override { return nanos.load(); }
        void Advance(int64_t sec) { nanos += sec * 1000000000LL; }
    };

    struct Rig {
        Mock mock;
        std::shared_ptr<FakeClock> clock = std::make_shared<FakeClock>(kBase);
        std::filesystem::path dir;
        std::string path;
        ClientIdentity id;

        explicit Rig(const char* name, const std::string& seed = R"({"refreshToken":"refresh-A"})") {
            dir = std::filesystem::temp_directory_path() / (std::string("perlin_auth_") + name);
            std::filesystem::remove_all(dir);
            std::filesystem::create_directories(dir);
            path = (dir / "auth.json").string();
            if (!seed.empty()) {
                std::ofstream f(path);
                f << seed;
            }
            mock.now = kBase;
            xbox::detail::SetTestEndpoint("127.0.0.1", static_cast<uint16_t>(mock.port));
        }
        ~Rig() {
            xbox::detail::SetTestEndpoint("", 0);
            std::filesystem::remove_all(dir);
        }
        void Advance(int64_t sec) {
            clock->Advance(sec);
            mock.now = clock->NowSec();
        }
        std::shared_ptr<auth::TokenStore> Launch(const std::string& p = {}, int lockMs = 3000) {
            auth::TokenStore::Options o;
            o.clock = clock;
            o.lockTimeoutMs = lockMs;
            return std::make_shared<auth::TokenStore>(p.empty() ? path : p, o);
        }
        bool Startup(const std::shared_ptr<auth::TokenStore>& store, std::string& err) {
            xbox::Account account(store);
            if (!account.SignInSaved(err))
                return false;
            xbox::Credentials creds;
            return account.LoginMinecraft(id.PublicKeyB64(), "1.26.52", creds, err);
        }
        std::optional<json::Document> Disk(const std::string& p = {}) {
            return json::Document::Parse(file::Read(p.empty() ? path : p));
        }
    };
}

static void TestSisuReuseAcrossStartups() {
    Rig rig("reuse");
    std::string err;
    {
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
    }
    CHECK(rig.mock.refreshes == 1 && rig.mock.deviceAuths == 1 && rig.mock.sisuCalls == 1);
    CHECK(rig.mock.xstsCalls == 3);
    auto disk = rig.Disk();
    CHECK(disk && disk->root()["version"].integer(0) == 2);
    if (disk) {
        const json::View root = disk->root();
        CHECK(root["sisu"]["title"]["token"].str().rfind("title-token#", 0) == 0);
        CHECK(root["sisu"]["user"]["token"].str().rfind("user-token-A#", 0) == 0);
        CHECK(root["sisu"]["acquiredAt"].integer(0) == kBase);
        CHECK(root["sisu"]["deviceBinding"].str().size() == 64);
        CHECK(root["xsts"][kPlayFabParty]["token"].str().rfind("xsts-playfab#", 0) == 0);
        CHECK(root["xsts"][kMultiplayerParty]["uhs"].str() == "uhsA");
        CHECK(root["xsts"][kLiveParty]["gtg"].str() == "MockGamer");
        CHECK(!root["xsts"].contains("rp://playfabapi.com/"));
        CHECK(root["refreshToken"].str() == "refresh-A");
        CHECK(!root.contains("msaAccess"));
    }
    for (int launch = 0; launch < 3; launch++) {
        rig.Advance(600);
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
    }
    CHECK(rig.mock.refreshes == 1);
    CHECK(rig.mock.deviceAuths == 1);
    CHECK(rig.mock.sisuCalls == 1);
    CHECK(rig.mock.xstsCalls == 3);
    CHECK(rig.mock.Count("/20ca2.playfabapi.com/Client/LoginWithXbox") == 4);
}

static void TestExpiryPolicy() {
    Rig rig("policy");
    std::string err;
    {
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
    }
    rig.Advance(13 * 86400);
    {
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
        CHECK(rig.mock.sisuCalls == 1 && rig.mock.refreshes == 1);
        CHECK(rig.mock.xstsCalls == 6);
        auto snap = store->Current();
        CHECK(snap->data.sisu.acquiredAt == kBase);
        CHECK(snap->data.xsts.at(kPlayFabParty).token.rfind("xsts-playfab#", 0) == 0);
        CHECK(snap->data.xsts.at(kPlayFabParty).notAfter == kBase + 13 * 86400 + 86400);
    }
    rig.Advance(86400 + 1);
    {
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
        CHECK(rig.mock.sisuCalls == 2 && rig.mock.refreshes == 2);
        CHECK(rig.mock.deviceAuths == 1);
        auto snap = store->Current();
        CHECK(snap->data.sisu.acquiredAt == rig.clock->NowSec());
        CHECK(snap->data.sisu.title.token.ends_with("#2"));
    }

    Rig early("earlyexpiry");
    early.mock.sisuLifetime = 2 * 86400;
    {
        auto store = early.Launch();
        CHECK(early.Startup(store, err));
        CHECK(store->Current()->data.sisu.title.notAfter == kBase + 2 * 86400);
    }
    early.Advance(2 * 86400 - 30);
    {
        auto store = early.Launch();
        CHECK(early.Startup(store, err));
        CHECK(early.mock.sisuCalls == 2);
        CHECK(store->Current()->data.sisu.acquiredAt == early.clock->NowSec());
    }

    Rig device("deviceexpiry");
    device.mock.deviceLifetime = 3 * 86400;
    {
        auto store = device.Launch();
        CHECK(device.Startup(store, err));
    }
    device.Advance(3 * 86400);
    {
        auto store = device.Launch();
        CHECK(device.Startup(store, err));
        CHECK(device.mock.deviceAuths == 2);
        CHECK(device.mock.sisuCalls == 2);
    }

    auth::Persisted p;
    p.deviceToken = "d";
    p.deviceNotAfter = kBase + 1000;
    CHECK(auth::policy::DeviceUsable(p, kBase + 939));
    CHECK(!auth::policy::DeviceUsable(p, kBase + 940));
    p.sisu.title = {"t", "", "", "", kBase + 40 * 86400};
    p.sisu.user = {"u", "", "", "", kBase + 40 * 86400};
    p.sisu.acquiredAt = kBase;
    p.sisu.deviceBinding = auth::policy::Fingerprint("d");
    CHECK(auth::policy::SisuUsable(p, kBase + 14 * 86400 - 1, kBase));
    CHECK(!auth::policy::SisuUsable(p, kBase + 14 * 86400, kBase));
    CHECK(!auth::policy::SisuUsable(p, kBase - 3600, kBase));
    p.sisu.deviceBinding = "other";
    CHECK(!auth::policy::SisuUsable(p, kBase, kBase));
}

static void TestRejectionRecovery() {
    Rig rig("reject");
    std::string err;
    {
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
    }
    rig.Advance(86400 + 5);
    rig.mock.minTitleGeneration = 2;
    {
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
        CHECK(rig.mock.sisuCalls == 2);
        CHECK(store->Current()->data.sisu.title.token.ends_with("#2"));
        CHECK(store->Current()->data.xsts.size() == 3);
    }
    auto disk = rig.Disk();
    CHECK(disk && disk->root()["sisu"]["title"]["token"].str().ends_with("#2"));
    {
        auto store = rig.Launch();
        rig.Advance(1);
        xbox::Account account(store);
        std::string header, xuid;
        CHECK(account.AuthorizationHeader(kMultiplayerParty, header, xuid, err));
        const int before = rig.mock.xstsCalls;
        account.Invalidate(kMultiplayerParty);
        CHECK(store->Current()->data.xsts.count(kMultiplayerParty) == 0);
        CHECK(store->Current()->data.xsts.count(kPlayFabParty) == 1);
        std::string again;
        CHECK(account.AuthorizationHeader(kMultiplayerParty, again, xuid, err));
        CHECK(rig.mock.xstsCalls == before + 1 && again != header);
    }
    Rig refused("refused");
    {
        auto store = refused.Launch();
        CHECK(refused.Startup(store, err));
    }
    refused.mock.xstsLifetime = 86400;
    refused.Advance(86400 + 5);
    refused.mock.minTitleGeneration = 99;
    {
        auto store = refused.Launch();
        CHECK(!refused.Startup(store, err));
        CHECK(refused.mock.sisuCalls >= 2 && refused.mock.sisuCalls <= 4);
        CHECK(!err.empty());
    }
}

static void TestRelyingPartiesAndOrder() {
    Rig rig("order");
    std::string err;
    {
        auto store = rig.Launch();
        xbox::Account account(store);
        xbox::Credentials creds;
        CHECK(account.SignInSaved(err));
        CHECK(account.LoginMinecraft(rig.id.PublicKeyB64(), "1.26.52", creds, err));
        CHECK(account.LoginMinecraft(rig.id.PublicKeyB64(), "1.26.52", creds, err));
        std::string gamertag;
        CHECK(account.FetchGamertag(gamertag, err) && gamertag == "MockGamer");
    }
    const auto log = rig.mock.Log();
    auto first = [&](const std::string& path) {
        return static_cast<int>(std::find(log.begin(), log.end(), path) - log.begin());
    };
    CHECK(first("/login.live.com/oauth20_token.srf") < first("/device.auth.xboxlive.com/device/authenticate") ||
          first("/device.auth.xboxlive.com/device/authenticate") < first("/sisu.xboxlive.com/authorize"));
    CHECK(first("/device.auth.xboxlive.com/device/authenticate") < first("/sisu.xboxlive.com/authorize"));
    CHECK(first("/login.live.com/oauth20_token.srf") < first("/sisu.xboxlive.com/authorize"));
    CHECK(first("/sisu.xboxlive.com/authorize") < first("/xsts.auth.xboxlive.com/xsts/authorize"));
    CHECK(first("/xsts.auth.xboxlive.com/xsts/authorize") < first("/20ca2.playfabapi.com/Client/LoginWithXbox"));
    CHECK(first("/20ca2.playfabapi.com/Client/LoginWithXbox") <
          first("/authorization.franchise.minecraft-services.net/api/v1.0/session/start"));
    CHECK(rig.mock.Count("/sisu.xboxlive.com/authorize") == 1);
    CHECK(rig.mock.Count("/title.mgt.xboxlive.com/titles/default/endpoints") == 1);
    CHECK(rig.mock.Count("/xsts.auth.xboxlive.com/xsts/authorize") == 3);
    auto parties = rig.mock.Parties();
    std::sort(parties.begin(), parties.end());
    std::vector<std::string> expected{kLiveParty, kPlayFabParty, kMultiplayerParty};
    std::sort(expected.begin(), expected.end());
    CHECK(parties == expected);
    CHECK(rig.mock.PartyCount("rp://playfabapi.com/") == 0);

    Rig fallback("nsalfail");
    fallback.mock.nsalFails = true;
    {
        auto store = fallback.Launch();
        CHECK(fallback.Startup(store, err));
    }
    CHECK(fallback.mock.PartyCount(kPlayFabParty) == 1);
    CHECK(fallback.mock.PartyCount("rp://playfabapi.com/") == 0);
    CHECK(fallback.mock.Count("/title.mgt.xboxlive.com/titles/default/endpoints") == 1);
}

static void TestConcurrentCallers() {
    Rig rig("concurrent", R"({"refreshToken":"refresh-A"})");
    rig.mock.sisuDelayMs = 120;
    rig.mock.xstsDelayMs = 80;
    auto store = rig.Launch();
    constexpr int kThreads = 12;
    std::vector<std::thread> threads;
    std::vector<std::string> headers(kThreads);
    std::atomic<int> okCount{0};
    std::atomic<bool> go{false};
    for (int i = 0; i < kThreads; i++)
        threads.emplace_back([&, i] {
            xbox::Account account(store);
            while (!go)
                std::this_thread::yield();
            std::string header, xuid, err;
            const char* party = i % 3 == 0 ? kPlayFabParty : i % 3 == 1 ? kMultiplayerParty : kLiveParty;
            if (account.AuthorizationHeader(party, header, xuid, err)) {
                headers[static_cast<size_t>(i)] = header;
                okCount++;
            }
        });
    go = true;
    for (auto& t : threads)
        t.join();
    CHECK(okCount == kThreads);
    CHECK(rig.mock.refreshes == 1 && rig.mock.deviceAuths == 1 && rig.mock.sisuCalls == 1);
    CHECK(rig.mock.xstsCalls == 3);
    for (int i = 3; i < kThreads; i++)
        CHECK(headers[static_cast<size_t>(i)] == headers[static_cast<size_t>(i % 3)]);
    CHECK(headers[0].find("xsts-playfab") != std::string::npos);
    CHECK(headers[1].find("xsts-multiplayer") != std::string::npos);
    CHECK(headers[2].find("xsts-xbl") != std::string::npos);
}

static void TestMultipleAccounts() {
    Rig rig("multi", "");
    const std::string a = (rig.dir / "a.json").string();
    const std::string b = (rig.dir / "b.json").string();
    {
        std::ofstream(a) << R"({"refreshToken":"refresh-A"})";
        std::ofstream(b) << R"({"refreshToken":"refresh-B"})";
    }
    auto storeA = auth::TokenStore::Open(a);
    auto storeB = auth::TokenStore::Open(b);
    CHECK(storeA != storeB);
    CHECK(auth::TokenStore::Open(a) == storeA);
    std::string headerA, headerB, headerA2, xuid, errA, errB;
    bool okA = false, okB = false;
    std::thread ta([&] {
        xbox::Account account(storeA);
        okA = account.AuthorizationHeader(kMultiplayerParty, headerA, xuid, errA);
    });
    std::thread tb([&] {
        xbox::Account account(storeB);
        std::string x;
        okB = account.AuthorizationHeader(kMultiplayerParty, headerB, x, errB);
    });
    ta.join();
    tb.join();
    CHECK(okA && okB);
    CHECK(headerA.find("x=uhsA;") != std::string::npos);
    CHECK(headerB.find("x=uhsB;") != std::string::npos);
    CHECK(storeA->Current()->data.refreshToken == "refresh-A");
    CHECK(storeB->Current()->data.refreshToken == "refresh-B");
    CHECK(storeA->Current()->data.deviceId != storeB->Current()->data.deviceId);
    CHECK(storeA->Current()->data.proofKeyPem != storeB->Current()->data.proofKeyPem);
    CHECK(storeA->Current()->data.sisu.user.token.find("user-token-A") == 0);
    CHECK(storeB->Current()->data.sisu.user.token.find("user-token-B") == 0);
    const bool plainStore = storeB->VaultName() == "private file";
    const auto diskB = rig.Disk(b);
    CHECK(std::filesystem::exists(b));
    CHECK(!plainStore || diskB.has_value());
    xbox::Account::SignOut(a);
    CHECK(!std::filesystem::exists(a));
    CHECK(storeA->Current()->data.refreshToken.empty());
    CHECK(storeB->Current()->data.sisu.user.token.find("user-token-B") == 0);
    CHECK(std::filesystem::exists(b));
    CHECK(!plainStore || (rig.Disk(b) && rig.Disk(b)->root()["refreshToken"].str() == "refresh-B"));
}

static void TestFailedRefreshKeepsState() {
    Rig rig("failed");
    std::string err;
    {
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
    }
    const std::string before = file::Read(rig.path);
    const int64_t revision = rig.Disk()->root()["revision"].integer(0);
    rig.Advance(86400 + 10);
    rig.mock.xstsStatusOverride = 500;
    {
        auto store = rig.Launch();
        xbox::Account account(store);
        std::string header, xuid;
        CHECK(!account.AuthorizationHeader(kMultiplayerParty, header, xuid, err));
        CHECK(!err.empty());
        CHECK(store->Current()->data.sisu.title.token.ends_with("#1"));
        CHECK(store->Current()->data.xsts.count(kMultiplayerParty) == 1);
    }
    CHECK(file::Read(rig.path) == before);
    CHECK(rig.Disk()->root()["revision"].integer(0) == revision);
    rig.mock.xstsStatusOverride = 0;
    {
        auto store = rig.Launch();
        xbox::Account account(store);
        std::string header, xuid;
        CHECK(account.AuthorizationHeader(kMultiplayerParty, header, xuid, err));
    }

    Rig msa("failedmsa");
    msa.mock.refreshStatusOverride = 503;
    {
        auto store = msa.Launch();
        xbox::Account account(store);
        CHECK(!account.SignInSaved(err));
        CHECK(account.HasSavedLogin());
        CHECK(store->Current()->data.refreshToken == "refresh-A");
    }
    CHECK(msa.Disk()->root()["refreshToken"].str() == "refresh-A");

    Rig rotate("rotate");
    rotate.mock.refreshRotation = 1;
    {
        auto store = rotate.Launch();
        CHECK(rotate.Startup(store, err));
        CHECK(store->Current()->data.refreshToken == "refresh-A.1");
    }
    CHECK(rotate.Disk()->root()["refreshToken"].str() == "refresh-A.1");
}

static void TestCorruptCaches() {
    namespace fs = std::filesystem;
    const std::string good = R"({"version":2,"revision":3,"refreshToken":"refresh-A","deviceId":"{id}"})";
    struct Case {
        const char* name;
        std::string content;
        bool corrupt;
        bool login;
    };
    const std::vector<Case> cases = {
        {"garbage", "{not json at all", true, false},
        {"truncated", good.substr(0, good.size() / 2), true, false},
        {"empty", "", true, false},
        {"binary", std::string("\x00\x01\x02\xff\xfe", 5), true, false},
        {"wrongtypes", R"({"version":"two","refreshToken":5})", true, false},
        {"newer", R"({"version":99,"refreshToken":"refresh-A"})", false, false},
        {"legacy", R"({"refreshToken":"refresh-A","deviceId":"{x}","minecraftDeviceId":"m"})", false, true},
        {"current", good, false, true},
    };
    for (const Case& c : cases) {
        Rig rig((std::string("corrupt_") + c.name).c_str(), c.content);
        if (c.content.empty())
            std::ofstream(rig.path).close();
        {
            std::ofstream leftover(rig.path + ".tmp.1.1");
            leftover << "partial";
        }
        auto store = rig.Launch();
        xbox::Account account(store);
        CHECK(account.HasSavedLogin() == c.login);
        if (fs::exists(rig.path + ".corrupt") != c.corrupt)
            std::printf("  case %s\n", c.name);
        CHECK(fs::exists(rig.path + ".corrupt") == c.corrupt);
        CHECK(fs::exists(rig.path) == !c.corrupt);
        store->Publish([](auth::State& s) { s.data.minecraftDeviceId = "published"; });
        auto disk = rig.Disk();
        CHECK(disk && disk->root()["version"].integer(0) == 2);
        CHECK(disk && disk->root()["minecraftDeviceId"].str() == "published");
        CHECK(disk && (disk->root()["refreshToken"].str() == "refresh-A") == c.login);
    }
    Rig missing("corrupt_missing", "");
    auto store = missing.Launch();
    CHECK(!xbox::Account(store).HasSavedLogin());
    CHECK(!fs::exists(missing.path));
}

static void TestAtomicPublication() {
    Rig rig("atomic", "");
    auto store = rig.Launch();
    store->Publish([](auth::State& s) {
        s.data.refreshToken = std::string(60000, 'a') + "seed";
        for (int k = 0; k < 4; k++)
            s.data.xsts["rp" + std::to_string(k)] = {"seed", "u", "", "", 1};
    });
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0}, bad{0};
    std::thread reader([&] {
        while (!stop) {
            std::string text;
            if (!file::Read(rig.path, text))
                continue;
            reads++;
            auto parsed = json::Read<auth::Persisted>(text);
            if (!parsed || parsed->version != 2 || parsed->refreshToken.empty() ||
                parsed->xsts.size() != 4 || parsed->refreshToken.size() < 60000)
                bad++;
        }
    });
    for (int i = 0; i < 150; i++)
        store->Publish([i](auth::State& s) {
            s.data.refreshToken = std::string(60000, 'a') + std::to_string(i);
            for (int k = 0; k < 4; k++)
                s.data.xsts["rp" + std::to_string(k)] = {"token" + std::to_string(i), "u", "", "", 1};
        });
    stop = true;
    reader.join();
    CHECK(reads > 0);
    CHECK(bad == 0);
    CHECK(store->Current()->data.revision == 151);
    CHECK(rig.Disk()->root()["revision"].integer(0) == 151);
    int tmpFiles = 0;
    for (const auto& e : std::filesystem::directory_iterator(rig.dir))
        if (e.path().filename().string().find(".tmp.") != std::string::npos)
            tmpFiles++;
    CHECK(tmpFiles == 0);
#ifndef _WIN32
    struct stat st{};
    CHECK(stat(rig.path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif

    const std::string nested = (rig.dir / "fresh" / "dir" / "auth.json").string();
    auto other = rig.Launch(nested);
    other->Publish([](auth::State& s) { s.data.refreshToken = "x"; });
    CHECK(std::filesystem::exists(nested));
#ifndef _WIN32
    CHECK(stat((rig.dir / "fresh").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
#endif

    auto a = rig.Launch(nested);
    auto b = rig.Launch(nested);
    std::thread ta([&] {
        for (int i = 0; i < 40; i++)
            a->Publish([](auth::State& s) { s.data.xsts["a"] = {"a", "u", "", "", 1}; });
    });
    std::thread tb([&] {
        for (int i = 0; i < 40; i++)
            b->Publish([](auth::State& s) { s.data.xsts["b"] = {"b", "u", "", "", 1}; });
    });
    ta.join();
    tb.join();
    auto merged = rig.Disk(nested);
    CHECK(merged && merged->root()["xsts"].contains("a") && merged->root()["xsts"].contains("b"));
    CHECK(merged && merged->root()["revision"].integer(0) == 81);
}

static void TestSimultaneousInstances() {
    Rig rig("processes");
    rig.mock.sisuDelayMs = 200;
    auto one = rig.Launch();
    auto two = rig.Launch();
    std::string e1, e2, header1, header2, xuid;
    bool ok1 = false, ok2 = false;
    std::thread t1([&] {
        xbox::Account account(one);
        ok1 = account.AuthorizationHeader(kMultiplayerParty, header1, xuid, e1);
    });
    std::thread t2([&] {
        xbox::Account account(two);
        std::string x;
        ok2 = account.AuthorizationHeader(kMultiplayerParty, header2, x, e2);
    });
    t1.join();
    t2.join();
    CHECK(ok1 && ok2);
    CHECK(rig.mock.sisuCalls == 1);
    CHECK(rig.mock.deviceAuths == 1);
    CHECK(rig.mock.refreshes == 1);
    CHECK(rig.mock.xstsCalls >= 1 && rig.mock.xstsCalls <= 2);
    CHECK(one->LockTimeouts() == 0 && two->LockTimeouts() == 0);
    auto third = rig.Launch();
    xbox::Account account(third);
    std::string header;
    CHECK(account.AuthorizationHeader(kMultiplayerParty, header, xuid, e1));
    CHECK(rig.mock.sisuCalls == 1 && rig.mock.deviceAuths == 1);

#ifndef _WIN32
    Rig held("heldlock");
    const int fd = open((held.path + ".lock").c_str(), O_RDWR | O_CREAT, 0600);
    CHECK(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    {
        auto store = held.Launch({}, 300);
        xbox::Account stuck(store);
        const auto begin = std::chrono::steady_clock::now();
        std::string err, h, x;
        CHECK(!stuck.AuthorizationHeader(kMultiplayerParty, h, x, err));
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        CHECK(elapsed >= std::chrono::milliseconds(300));
        CHECK(elapsed < std::chrono::seconds(10));
        CHECK(store->LockTimeouts() >= 1);
        CHECK(!err.empty());
        CHECK(held.mock.refreshes == 0 && held.mock.sisuCalls == 0 && held.mock.deviceAuths == 0 && held.mock.xstsCalls == 0);
        flock(fd, LOCK_UN);
        CHECK(stuck.AuthorizationHeader(kMultiplayerParty, h, x, err));
        CHECK(held.mock.sisuCalls == 1);
    }
    close(fd);
    Rig stale("stalelock");
    {
        std::ofstream(stale.path + ".lock") << "left behind by a crashed process";
        auto store = stale.Launch({}, 300);
        xbox::Account account2(store);
        std::string err, h, x;
        CHECK(account2.AuthorizationHeader(kMultiplayerParty, h, x, err));
        CHECK(store->LockTimeouts() == 0);
    }
#endif
}

static void TestCancellationAndShutdown() {
    Rig rig("cancel");
    rig.mock.sisuDelayMs = 1200;
    auto store = rig.Launch();
    xbox::Account leader(store);
    xbox::Account waiter(store);
    xbox::Account blocked(store);
    std::string leaderHeader, leaderErr, xuid;
    bool leaderOk = false;
    std::thread lt([&] { leaderOk = leader.AuthorizationHeader(kMultiplayerParty, leaderHeader, xuid, leaderErr); });
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    std::string waitErr, blockedErr;
    bool waitOk = true, blockedOk = true;
    std::chrono::steady_clock::duration waitTime{}, blockedTime{};
    std::thread wt([&] {
        const auto begin = std::chrono::steady_clock::now();
        std::string h, x;
        waitOk = waiter.AuthorizationHeader(kMultiplayerParty, h, x, waitErr);
        waitTime = std::chrono::steady_clock::now() - begin;
    });
    std::thread bt([&] {
        const auto begin = std::chrono::steady_clock::now();
        std::string h, x;
        blockedOk = blocked.AuthorizationHeader(kPlayFabParty, h, x, blockedErr);
        blockedTime = std::chrono::steady_clock::now() - begin;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    waiter.Cancel();
    wt.join();
    CHECK(!waitOk && waitErr == "cancelled");
    CHECK(waitTime < std::chrono::milliseconds(900));
    store->Shutdown();
    bt.join();
    CHECK(!blockedOk && blockedErr == "cancelled");
    CHECK(blockedTime < std::chrono::milliseconds(1100));
    lt.join();
    CHECK(leaderOk);
    std::string err, h, x;
    xbox::Account after(store);
    CHECK(!after.AuthorizationHeader(kLiveParty, h, x, err) || true);
    CHECK(rig.mock.sisuCalls == 1);
}

static std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

static void TestPlayFabRelyingPartyIsPinned() {
    CHECK(std::string(xbox::rp::kPlayFab) == "http://playfab.xboxlive.com/");
    CHECK(xbox::rp::IsRejected("rp://playfabapi.com/"));
    CHECK(!xbox::rp::IsRejected(xbox::rp::kPlayFab));
    CHECK(xbox::rp::ForPlayFab("") == xbox::rp::kPlayFab);
    CHECK(xbox::rp::ForPlayFab("rp://playfabapi.com/") == xbox::rp::kPlayFab);
    CHECK(xbox::rp::ForPlayFab("http://playfab.xboxlive.com/") == xbox::rp::kPlayFab);

    Rig rig("rpwrong");
    rig.mock.nsalWrongPlayFab = true;
    std::string err;
    {
        auto store = rig.Launch();
        CHECK(rig.Startup(store, err));
    }
    CHECK(rig.mock.PartyCount("rp://playfabapi.com/") == 0);
    CHECK(rig.mock.PartyCount(kPlayFabParty) == 1);
    const auto disk = rig.Disk();
    CHECK(disk && !disk->root()["xsts"].contains("rp://playfabapi.com/"));
}

static void TestXstsIssuedOncePerRelyingPartyAcrossProcesses() {
    Rig rig("xstsonce");
    std::string err, header, xuid;
    {
        auto warm = rig.Launch();
        xbox::Account account(warm);
        CHECK(account.AuthorizationHeader(kLiveParty, header, xuid, err));
    }
    const int sisuBefore = rig.mock.sisuCalls;
    const int xstsBefore = rig.mock.xstsCalls;
    rig.mock.xstsDelayMs = 250;
    auto one = rig.Launch();
    auto two = rig.Launch();
    auto three = rig.Launch();
    std::vector<std::thread> threads;
    std::atomic<int> ok{0};
    for (auto& store : {one, two, three})
        threads.emplace_back([&, store] {
            xbox::Account account(store);
            std::string h, x, e;
            if (account.AuthorizationHeader(kMultiplayerParty, h, x, e) && h.find("x=uhsA;") != std::string::npos)
                ok++;
        });
    for (auto& t : threads)
        t.join();
    CHECK(ok == 3);
    CHECK(rig.mock.xstsCalls - xstsBefore == 1);
    CHECK(rig.mock.sisuCalls == sisuBefore);
    CHECK(rig.mock.PartyCount(kMultiplayerParty) == 1);
    auto late = rig.Launch();
    xbox::Account account(late);
    CHECK(account.AuthorizationHeader(kMultiplayerParty, header, xuid, err));
    CHECK(rig.mock.xstsCalls - xstsBefore == 1);
}

static void TestDifferentRelyingPartiesRefreshIndependently() {
    Rig rig("xstsparallel");
    std::string err, header, xuid;
    {
        auto warm = rig.Launch();
        xbox::Account account(warm);
        CHECK(account.AuthorizationHeader(kLiveParty, header, xuid, err));
    }
    const int before = rig.mock.xstsCalls;
    rig.mock.xstsDelayMs = 400;
    auto one = rig.Launch();
    auto two = rig.Launch();
    const auto begin = std::chrono::steady_clock::now();
    bool ok1 = false, ok2 = false;
    std::thread t1([&] {
        xbox::Account account(one);
        std::string h, x, e;
        ok1 = account.AuthorizationHeader(kMultiplayerParty, h, x, e);
    });
    std::thread t2([&] {
        xbox::Account account(two);
        std::string h, x, e;
        ok2 = account.AuthorizationHeader(kPlayFabParty, h, x, e);
    });
    t1.join();
    t2.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
    CHECK(ok1 && ok2);
    CHECK(rig.mock.xstsCalls - before == 2);
    CHECK(elapsed < std::chrono::milliseconds(750));
    CHECK(one->LockTimeouts() == 0 && two->LockTimeouts() == 0);
    const auto disk = rig.Disk();
    CHECK(disk && disk->root()["xsts"].contains(kMultiplayerParty) && disk->root()["xsts"].contains(kPlayFabParty) &&
          disk->root()["xsts"].contains(kLiveParty));
}

static void TestXstsCoordinationFailuresDoNotBypass() {
#ifndef _WIN32
    Rig rig("xstsblocked");
    std::string err, header, xuid;
    {
        auto warm = rig.Launch();
        xbox::Account account(warm);
        CHECK(account.AuthorizationHeader(kLiveParty, header, xuid, err));
    }
    const int before = rig.mock.xstsCalls;
    const std::string keyLock =
        rig.path + ".lock.x" + auth::policy::Fingerprint(std::string("xsts:") + kMultiplayerParty).substr(0, 16);
    const int fd = open(keyLock.c_str(), O_RDWR | O_CREAT, 0600);
    CHECK(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    auto store = rig.Launch({}, 300);
    xbox::Account account(store);
    CHECK(!account.AuthorizationHeader(kMultiplayerParty, header, xuid, err));
    CHECK(!err.empty());
    CHECK(rig.mock.xstsCalls == before);
    CHECK(store->LockTimeouts() >= 1);
    CHECK(account.AuthorizationHeader(kPlayFabParty, header, xuid, err));
    CHECK(rig.mock.xstsCalls == before + 1);
    flock(fd, LOCK_UN);
    CHECK(account.AuthorizationHeader(kMultiplayerParty, header, xuid, err));
    CHECK(rig.mock.xstsCalls == before + 2);

    Rig cancelled("xstscancel");
    {
        auto warm = cancelled.Launch();
        xbox::Account a(warm);
        CHECK(a.AuthorizationHeader(kLiveParty, header, xuid, err));
    }
    const int base = cancelled.mock.xstsCalls;
    const std::string lockPath =
        cancelled.path + ".lock.x" + auth::policy::Fingerprint(std::string("xsts:") + kMultiplayerParty).substr(0, 16);
    const int fd2 = open(lockPath.c_str(), O_RDWR | O_CREAT, 0600);
    CHECK(fd2 >= 0 && flock(fd2, LOCK_EX | LOCK_NB) == 0);
    auto waiting = cancelled.Launch({}, 60000);
    xbox::Account waiter(waiting);
    bool ok = true;
    std::string werr, wh, wx;
    const auto begin = std::chrono::steady_clock::now();
    std::thread t([&] { ok = waiter.AuthorizationHeader(kMultiplayerParty, wh, wx, werr); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    waiter.Cancel();
    t.join();
    CHECK(!ok);
    CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(5));
    CHECK(cancelled.mock.xstsCalls == base);
    flock(fd2, LOCK_UN);
    close(fd2);
    close(fd);
#endif
}

static void TestIndependentAccountsAcrossInstances() {
    Rig rig("accounts2", "");
    rig.mock.sisuDelayMs = 400;
    const std::string a = (rig.dir / "a.json").string();
    const std::string b = (rig.dir / "b.json").string();
    {
        std::ofstream(a) << R"({"refreshToken":"refresh-A"})";
        std::ofstream(b) << R"({"refreshToken":"refresh-B"})";
    }
    auto a1 = rig.Launch(a), a2 = rig.Launch(a), b1 = rig.Launch(b), b2 = rig.Launch(b);
    std::vector<std::string> headers(4), errors(4);
    std::vector<int> ok(4, 0);
    std::vector<std::thread> threads;
    const std::vector<std::shared_ptr<auth::TokenStore>> stores{a1, a2, b1, b2};
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 4; i++)
        threads.emplace_back([&, i] {
            xbox::Account account(stores[i]);
            std::string x;
            ok[i] = account.AuthorizationHeader(kMultiplayerParty, headers[i], x, errors[i]);
        });
    for (auto& t : threads)
        t.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
    CHECK(ok[0] && ok[1] && ok[2] && ok[3]);
    CHECK(headers[0].find("x=uhsA;") != std::string::npos && headers[1].find("x=uhsA;") != std::string::npos);
    CHECK(headers[2].find("x=uhsB;") != std::string::npos && headers[3].find("x=uhsB;") != std::string::npos);
    CHECK(rig.mock.sisuCalls == 2);
    CHECK(rig.mock.deviceAuths == 2);
    CHECK(rig.mock.refreshes == 2);
    CHECK(elapsed < std::chrono::milliseconds(2800));
    const auto diskA = rig.Disk(a);
    const auto diskB = rig.Disk(b);
    CHECK(diskA && diskB);
    CHECK(diskA->root()["refreshToken"].str() == "refresh-1" || diskA->root()["refreshToken"].str() == "refresh-A");
    CHECK(diskA->root()["deviceId"].str() != diskB->root()["deviceId"].str());

    xbox::Account::SignOut(a);
    CHECK(!std::filesystem::exists(a));
    CHECK(std::filesystem::exists(b));
    auto b3 = rig.Launch(b);
    xbox::Account again(b3);
    std::string h, x, e;
    const int sisuBefore = rig.mock.sisuCalls;
    CHECK(again.AuthorizationHeader(kMultiplayerParty, h, x, e));
    CHECK(rig.mock.sisuCalls == sisuBefore);
    CHECK(h.find("x=uhsB;") != std::string::npos);
}

static void TestLateRefreshAfterSignOutIsDropped() {
    Rig rig("late", "");
    rig.mock.sisuDelayMs = 400;
    const std::string a = (rig.dir / "a.json").string();
    const std::string b = (rig.dir / "b.json").string();
    {
        std::ofstream(a) << R"({"refreshToken":"refresh-A"})";
        std::ofstream(b) << R"({"refreshToken":"refresh-B"})";
    }
    auto storeA = rig.Launch(a);
    auto storeB = rig.Launch(b);
    bool okA = true, okB = false;
    std::string errA, errB, h, x;
    std::thread ta([&] {
        xbox::Account account(storeA);
        okA = account.AuthorizationHeader(kMultiplayerParty, h, x, errA);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::thread out([&] { storeA->Clear(); });
    std::thread tb([&] {
        xbox::Account account(storeB);
        std::string hb, xb;
        okB = account.AuthorizationHeader(kMultiplayerParty, hb, xb, errB);
    });
    ta.join();
    out.join();
    tb.join();
    CHECK(!okA);
    CHECK(!std::filesystem::exists(a));
    CHECK(storeA->Current()->data.sisu.user.token.empty());
    CHECK(okB);
    CHECK(rig.Disk(b) && rig.Disk(b)->root()["sisu"]["user"]["token"].str().find("user-token-B") == 0);
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
    TestSisuReuseAcrossStartups();
    TestExpiryPolicy();
    TestRejectionRecovery();
    TestRelyingPartiesAndOrder();
    TestConcurrentCallers();
    TestMultipleAccounts();
    TestFailedRefreshKeepsState();
    TestCorruptCaches();
    TestAtomicPublication();
    TestSimultaneousInstances();
    TestCancellationAndShutdown();
    TestPlayFabRelyingPartyIsPinned();
    TestXstsIssuedOncePerRelyingPartyAcrossProcesses();
    TestDifferentRelyingPartiesRefreshIndependently();
    TestXstsCoordinationFailuresDoNotBypass();
    TestIndependentAccountsAcrossInstances();
    TestLateRefreshAfterSignOutIsDropped();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("auth_selftest: ok\n");
    return 0;
}
