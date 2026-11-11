#include "network/XboxAuth.hpp"
#include <future>

#include "network/ClientIdentity.hpp"
#include "network/Http.hpp"
#include "util/Json.hpp"
#include "util/Strings.hpp"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>
#include "util/Log.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace perlin::xbox {
    namespace persist {
        struct Cache {
            std::string refreshToken;
            std::string proofKeyPem;
            std::string deviceId;
            std::string deviceToken;
            int64_t deviceNotAfter = 0;
            std::string minecraftDeviceId;
        };

    }
    using persist::Cache;

    namespace {
        constexpr const char* kMsaClientId = "0000000048183522";
        constexpr const char* kMsaScope = "service::user.auth.xboxlive.com::MBI_SSL";
        constexpr const char* kUserAgent = "XAL Android 2025.04.20250326.000";
        constexpr const char* kSandbox = "RETAIL";
        constexpr int64_t kTitleId = 1739947436;
        constexpr const char* kFallbackServiceUri = "https://authorization.franchise.minecraft-services.net";
        constexpr const char* kFallbackPlayFabTitle = "20CA2";
        constexpr const char* kPlayFabRelyingParty = "rp://playfabapi.com/";
        constexpr const char* kMultiplayerRelyingParty = "https://multiplayer.minecraft.net/";

        struct TestEndpoint {
            std::string host;
            uint16_t port = 0;
        } g_testEndpoint;

        int64_t NowSec() {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        std::string Esc(const std::string& s) {
            std::string out;
            out.reserve(s.size() + 2);
            for (char c : s) {
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
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += c;
                    }
                }
            }
            return out;
        }
        std::string Q(const std::string& s) {
            return "\"" + Esc(s) + "\"";
        }

        std::string UrlEnc(const std::string& s) {
            static const char* hex = "0123456789ABCDEF";
            std::string out;
            for (unsigned char c : s) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                    out += static_cast<char>(c);
                else {
                    out += '%';
                    out += hex[c >> 4];
                    out += hex[c & 15];
                }
            }
            return out;
        }

        std::string B64Decode(const std::string& s) {
            return B64UrlDecode(s);
        }

        struct Url {
            std::string host;
            uint16_t port = 443;
            bool tls = true;
            std::string path = "/";
            std::string Full() const { return std::string(tls ? "https://" : "http://") + host + path; }
        };

        bool ParseUrl(std::string s, Url& out) {
            out = {};
            if (s.starts_with("https://")) {
                s = s.substr(8);
            } else if (s.starts_with("http://")) {
                s = s.substr(7);
                out.tls = false;
                out.port = 80;
            } else {
                return false;
            }
            const size_t slash = s.find('/');
            std::string hostPort = slash == std::string::npos ? s : s.substr(0, slash);
            out.path = slash == std::string::npos ? "/" : s.substr(slash);
            const size_t colon = hostPort.rfind(':');
            if (colon != std::string::npos) {
                out.port = static_cast<uint16_t>(std::atoi(hostPort.c_str() + colon + 1));
                hostPort.resize(colon);
            }
            out.host = hostPort;
            return !out.host.empty();
        }

        bool GlobMatch(const std::string& pattern, const std::string& text) {
            size_t p = 0, t = 0, star = std::string::npos, mark = 0;
            while (t < text.size()) {
                if (p < pattern.size() && pattern[p] == '*') {
                    star = p++;
                    mark = t;
                } else if (p < pattern.size() && pattern[p] == text[t]) {
                    p++;
                    t++;
                } else if (star != std::string::npos) {
                    p = star + 1;
                    t = ++mark;
                } else {
                    return false;
                }
            }
            while (p < pattern.size() && pattern[p] == '*')
                p++;
            return p == pattern.size();
        }

        void PutBe32(std::string& out, uint32_t v) {
            for (int i = 3; i >= 0; i--)
                out += static_cast<char>((v >> (i * 8)) & 0xFF);
        }
        void PutBe64(std::string& out, uint64_t v) {
            for (int i = 7; i >= 0; i--)
                out += static_cast<char>((v >> (i * 8)) & 0xFF);
        }

        uint64_t WindowsTime(int64_t unixNanos) {
            return static_cast<uint64_t>(unixNanos / 100 + 116444736000000000LL);
        }

        struct Policy {
            uint32_t version = 1;
            size_t maxBody = 0;
            std::vector<std::string> extraHeaders;
        };

        std::string Digest(const Policy& policy, const std::string& method, const std::string& path,
                           const std::vector<std::string>& headerValues, const std::string& body,
                           int64_t unixNanos) {
            std::string data;
            PutBe32(data, policy.version);
            data += '\0';
            PutBe64(data, WindowsTime(unixNanos));
            data += '\0';
            data += method;
            data += '\0';
            data += path;
            data += '\0';
            for (const std::string& h : headerValues) {
                data += h;
                data += '\0';
            }
            data.append(body, 0, policy.maxBody == 0 ? body.size() : std::min(policy.maxBody, body.size()));
            data += '\0';
            unsigned char digest[32];
            unsigned int len = 0;
            EVP_Digest(data.data(), data.size(), digest, &len, EVP_sha256(), nullptr);
            return std::string(reinterpret_cast<char*>(digest), 32);
        }

        std::string SignWith(EC_KEY* key, const Policy& policy, const std::string& method,
                             const std::string& path, const std::vector<std::string>& headerValues,
                             const std::string& body, int64_t unixNanos) {
            const std::string digest = Digest(policy, method, path, headerValues, body, unixNanos);
            ECDSA_SIG* sig = ECDSA_do_sign(reinterpret_cast<const unsigned char*>(digest.data()), 32, key);
            if (!sig)
                return {};
            const BIGNUM* r = nullptr;
            const BIGNUM* s = nullptr;
            ECDSA_SIG_get0(sig, &r, &s);
            unsigned char raw[64];
            BN_bn2binpad(r, raw, 32);
            BN_bn2binpad(s, raw + 32, 32);
            ECDSA_SIG_free(sig);
            std::string out;
            PutBe32(out, policy.version);
            PutBe64(out, WindowsTime(unixNanos));
            out.append(reinterpret_cast<char*>(raw), 64);
            return B64Standard(out);
        }

        EC_KEY* NewProofKey() {
            EC_KEY* key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
            if (key && !EC_KEY_generate_key(key)) {
                EC_KEY_free(key);
                return nullptr;
            }
            return key;
        }

        std::string KeyToPem(EC_KEY* key) {
            BIO* bio = BIO_new(BIO_s_mem());
            std::string out;
            if (PEM_write_bio_ECPrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr) == 1) {
                char* data = nullptr;
                const long len = BIO_get_mem_data(bio, &data);
                out.assign(data, static_cast<size_t>(len));
            }
            BIO_free(bio);
            return out;
        }

        EC_KEY* KeyFromPem(const std::string& pem) {
            BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
            EC_KEY* key = PEM_read_bio_ECPrivateKey(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (key && EC_GROUP_get_curve_name(EC_KEY_get0_group(key)) != NID_X9_62_prime256v1) {
                EC_KEY_free(key);
                return nullptr;
            }
            return key;
        }

        std::string JwkJson(EC_KEY* key) {
            BIGNUM* x = BN_new();
            BIGNUM* y = BN_new();
            EC_POINT_get_affine_coordinates(EC_KEY_get0_group(key), EC_KEY_get0_public_key(key), x, y,
                                            nullptr);
            unsigned char xb[32], yb[32];
            BN_bn2binpad(x, xb, 32);
            BN_bn2binpad(y, yb, 32);
            BN_free(x);
            BN_free(y);
            return "{\"use\":\"sig\",\"kty\":\"EC\",\"crv\":\"P-256\",\"alg\":\"ES256\",\"x\":\"" +
                   B64Url(xb, 32) + "\",\"y\":\"" + B64Url(yb, 32) + "\"}";
        }

        struct Tok {
            std::string token;
            int64_t notAfter = 0;
            std::string uhs, gtg, xid;
            bool Valid() const { return !token.empty() && NowSec() < notAfter - 60; }
        };

        bool ReadTok(const json::View& v, Tok& out) {
            out = {};
            out.token = v["Token"].str();
            out.notAfter = detail::ParseIsoTime(v["NotAfter"].str());
            const json::View user = v["DisplayClaims"]["xui"][0];
            out.uhs = user["uhs"].str();
            out.gtg = user["gtg"].str();
            out.xid = user["xid"].str();
            return !out.token.empty();
        }

        struct Endpoint {
            std::string protocol, host, hostType, path, relyingParty;
            int port = 0;
            int policy = -1;
        };

        std::string XboxErrorText(uint64_t code) {
            switch (code) {
            case 2148916227:
                return "this Xbox account has been banned";
            case 2148916229:
                return "an adult must approve this account's online play (Microsoft family settings)";
            case 2148916233:
                return "this Microsoft account has no Xbox profile yet; sign in once at https://www.xbox.com "
                       "or in "
                       "the Minecraft launcher to create one";
            case 2148916235:
                return "Xbox Live is not available in your country";
            case 2148916236:
            case 2148916237:
                return "this account needs adult verification before it can use Xbox Live";
            case 2148916238:
                return "this account belongs to a child and must be added to a Microsoft family group";
            case 2148916258:
                return "this account's Xbox profile needs attention (visit https://account.xbox.com)";
            default:
                return "Xbox error " + std::to_string(code);
            }
        }
    }

    namespace detail {
        std::string SignRequest(void* ecKeyP256, const std::string& method, const std::string& pathAndQuery,
                                const std::string& authorization, const std::string& body, int64_t unixNanos,
                                size_t maxBodyBytes) {
            Policy policy;
            policy.maxBody = maxBodyBytes;
            return SignWith(static_cast<EC_KEY*>(ecKeyP256), policy, method, pathAndQuery, {authorization},
                            body, unixNanos);
        }

        std::string RequestDigest(const std::string& method, const std::string& pathAndQuery,
                                  const std::string& authorization, const std::string& body,
                                  int64_t unixNanos, size_t maxBodyBytes) {
            Policy policy;
            policy.maxBody = maxBodyBytes;
            return Digest(policy, method, pathAndQuery, {authorization}, body, unixNanos);
        }

        void SetTestEndpoint(const std::string& host, uint16_t port) {
            g_testEndpoint = {host, port};
        }

        int64_t ParseIsoTime(const std::string& iso) {
            int y, mo, d, h, mi, s;
            if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6)
                return 0;
            std::tm tm{};
            tm.tm_year = y - 1900;
            tm.tm_mon = mo - 1;
            tm.tm_mday = d;
            tm.tm_hour = h;
            tm.tm_min = mi;
            tm.tm_sec = s;
#ifdef _WIN32
            return static_cast<int64_t>(_mkgmtime(&tm));
#else
            return static_cast<int64_t>(timegm(&tm));
#endif
        }

        int64_t ParseHttpDate(const std::string& date) {
            static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            char mon[4] = {};
            int d, y, h, mi, s;
            const size_t comma = date.find(',');
            if (comma == std::string::npos ||
                std::sscanf(date.c_str() + comma + 1, " %d %3s %d %d:%d:%d", &d, mon, &y, &h, &mi, &s) != 6)
                return 0;
            int m = -1;
            for (int i = 0; i < 12; i++)
                if (std::strcmp(mon, months[i]) == 0)
                    m = i;
            if (m < 0)
                return 0;
            std::tm tm{};
            tm.tm_year = y - 1900;
            tm.tm_mon = m;
            tm.tm_mday = d;
            tm.tm_hour = h;
            tm.tm_min = mi;
            tm.tm_sec = s;
#ifdef _WIN32
            return static_cast<int64_t>(_mkgmtime(&tm));
#else
            return static_cast<int64_t>(timegm(&tm));
#endif
        }
    }

    struct Account::Impl {
        std::string cachePath;
        Cache cache;
        EC_KEY* proofKey = nullptr;
        std::string msaAccess;
        int64_t msaExpiry = 0;
        Tok device, title, user;
        int64_t skewSeconds = 0;
        std::vector<Endpoint> endpoints;
        std::vector<Policy> policies;
        bool nsalLoaded = false;
        std::atomic<bool> cancelled{false};

        ~Impl() {
            if (proofKey)
                EC_KEY_free(proofKey);
        }

        void LoadCache() {
            std::ifstream file(cachePath, std::ios::binary);
            if (!file.good())
                return;
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (auto parsed = json::Read<Cache>(text))
                cache = std::move(*parsed);
        }
        void SaveCache() {
            const std::string text = json::Write(cache);
            if (text.empty())
                return;
            {
                std::ofstream file(cachePath, std::ios::binary | std::ios::trunc);
                file << text;
            }
#ifndef _WIN32
            chmod(cachePath.c_str(), 0600);
#endif
        }

        int64_t ServerNow() const { return NowSec() + skewSeconds; }
        int64_t ServerNanos() const {
            return (std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count()) +
                   skewSeconds * 1000000000LL;
        }
        void NoteDate(const http::Response& r) {
            const auto it = r.headers.find("date");
            if (it == r.headers.end())
                return;
            if (const int64_t t = detail::ParseHttpDate(it->second))
                skewSeconds = t - NowSec();
        }

        static http::Response Send(const char* method, const Url& u, const std::string& body,
                                   const std::string& contentType,
                                   const std::map<std::string, std::string>& headers) {
            if (!g_testEndpoint.host.empty())
                return http::Request(g_testEndpoint.host, g_testEndpoint.port, false, method,
                                     "/" + u.host + u.path, body, contentType, headers, 20);
            return http::Request(u.host, u.port, u.tls, method, u.path, body, contentType, headers, 20);
        }

        http::Response Post(const Url& u, const std::string& body, const std::string& contentType,
                            std::map<std::string, std::string> headers = {}) {
            http::Response r = Send("POST", u, body, contentType, headers);
            NoteDate(r);
            return r;
        }
        http::Response Get(const Url& u, std::map<std::string, std::string> headers = {}) {
            http::Response r = Send("GET", u, {}, {}, headers);
            NoteDate(r);
            return r;
        }

        static std::string NetworkError(const http::Response& r, const char* what) {
            if (r.status == 0)
                return std::string("cannot reach ") + what + " (no network, or TLS failed)";
            return std::string(what) + " answered HTTP " + std::to_string(r.status);
        }

        bool Sleep(int ms) {
            for (int waited = 0; waited < ms; waited += 100) {
                if (cancelled)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return !cancelled;
        }

        void LoadNsal() {
            if (nsalLoaded)
                return;
            Url u;
            ParseUrl("https://title.mgt.xboxlive.com/titles/default/endpoints?type=1", u);
            const http::Response r = Get(u, {{"x-xbl-contract-version", "1"}});
            auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
            if (!doc) {
                Log("auth", "could not load Xbox endpoint policies ({}); using defaults", r.status);
                nsalLoaded = true;
                return;
            }
            for (const json::View p : doc->root()["SignaturePolicies"].items()) {
                Policy pol;
                pol.version = static_cast<uint32_t>(p["Version"].integer(1));
                pol.maxBody = static_cast<size_t>(p["MaxBodyBytes"].integer(0));
                for (const json::View h : p["ExtraHeaders"].items())
                    pol.extraHeaders.push_back(h.str());
                policies.push_back(std::move(pol));
            }
            for (const json::View e : doc->root()["EndPoints"].items()) {
                Endpoint ep;
                ep.protocol = e["Protocol"].str();
                ep.host = e["Host"].str();
                ep.hostType = e["HostType"].str();
                ep.path = e["Path"].str();
                ep.relyingParty = e["RelyingParty"].str();
                ep.port = e["Port"].integer(0);
                ep.policy = e.contains("SignaturePolicyIndex") ? e["SignaturePolicyIndex"].integer(-1) : -1;
                endpoints.push_back(std::move(ep));
            }
            nsalLoaded = true;
        }

        const Endpoint* MatchEndpoint(const Url& u) {
            LoadNsal();
            const Endpoint* found = nullptr;
            const std::string scheme = u.tls ? "https" : "http";
            const std::string path = u.path.substr(0, u.path.find('?'));
            for (const Endpoint& e : endpoints) {
                if (e.relyingParty.empty() || e.protocol != scheme)
                    continue;
                bool host = false;
                if (e.hostType == "fqdn")
                    host = e.host == u.host;
                else if (e.hostType == "wildcard")
                    host = !e.host.empty() && e.host[0] == '*' && GlobMatch(e.host, u.host);
                if (!host || (e.port != 0 && e.port != u.port) || (!e.path.empty() && e.path != path))
                    continue;
                found = &e;
                if (e.hostType == "fqdn")
                    break;
            }
            return found;
        }

        Policy PolicyFor(const Url& u) {
            if (const Endpoint* e = MatchEndpoint(u))
                if (e->policy >= 0 && e->policy < static_cast<int>(policies.size()))
                    return policies[static_cast<size_t>(e->policy)];
            return {};
        }

        http::Response PostSigned(const Url& u, const std::string& body,
                                  std::map<std::string, std::string> headers, const Policy& policy) {
            headers["User-Agent"] = kUserAgent;
            auto find = [&](const std::string& name) -> std::string {
                for (auto& [k, v] : headers)
                    if (str::Lower(k) == str::Lower(name))
                        return v;
                return {};
            };
            std::vector<std::string> values{find("Authorization")};
            for (const std::string& h : policy.extraHeaders)
                values.push_back(find(h));
            const std::string path = u.path;
            headers["Signature"] = SignWith(proofKey, policy, "POST", path, values, body, ServerNanos());
            return Post(u, body, "application/json", headers);
        }

        struct MsaTokens {
            std::string access, refresh;
            int64_t expiresIn = 0;
        };

        bool ReadMsa(const http::Response& r, MsaTokens& out, std::string& errorCode,
                     std::string& errorText) {
            auto doc = json::Document::Parse(r.body);
            if (!doc) {
                errorText = NetworkError(r, "login.live.com");
                return false;
            }
            const json::View root = doc->root();
            if (r.status == 200 && root["access_token"].isString()) {
                out.access = root["access_token"].str();
                out.refresh = root["refresh_token"].str();
                out.expiresIn = static_cast<int64_t>(root["expires_in"].number(3600));
                return true;
            }
            errorCode = root["error"].str();
            errorText = root["error_description"].str(errorCode.empty() ? NetworkError(r, "login.live.com")
                                                                        : errorCode);
            return false;
        }

        bool Refresh(std::string& error) {
            if (cache.refreshToken.empty())
                return false;
            Url u;
            ParseUrl("https://login.live.com/oauth20_token.srf", u);
            const http::Response r =
                Post(u,
                     "grant_type=refresh_token&refresh_token=" + UrlEnc(cache.refreshToken) +
                         "&scope=" + UrlEnc(kMsaScope) + "&client_id=" + kMsaClientId,
                     "application/x-www-form-urlencoded", {{"User-Agent", kUserAgent}});
            MsaTokens t;
            std::string code;
            if (!ReadMsa(r, t, code, error)) {
                if (r.status != 0 && (code == "invalid_grant" || r.status == 400 || r.status == 401)) {
                    cache.refreshToken.clear();
                    SaveCache();
                }
                return false;
            }
            msaAccess = t.access;
            msaExpiry = NowSec() + t.expiresIn;
            if (!t.refresh.empty() && t.refresh != cache.refreshToken) {
                cache.refreshToken = t.refresh;
                SaveCache();
            }
            return true;
        }

        bool DeviceCode(const PromptFn& prompt, std::string& error) {
            Url u;
            ParseUrl("https://login.live.com/oauth20_connect.srf", u);
            http::Response r = Post(u,
                                    std::string("client_id=") + kMsaClientId + "&scope=" + UrlEnc(kMsaScope) +
                                        "&response_type=device_code",
                                    "application/x-www-form-urlencoded", {{"User-Agent", kUserAgent}});
            auto doc = json::Document::Parse(r.body);
            if (r.status != 200 || !doc || !doc->root()["device_code"].isString()) {
                error = NetworkError(r, "Microsoft sign-in");
                return false;
            }
            const json::View root = doc->root();
            const std::string deviceCode = root["device_code"].str();
            const std::string userCode = root["user_code"].str();
            std::string verify = root["verification_uri"].str("https://www.microsoft.com/link");
            int interval = std::max(1, root["interval"].integer(5));
            const int64_t deadline = NowSec() + static_cast<int64_t>(root["expires_in"].number(900));
            prompt(verify, userCode);

            ParseUrl("https://login.live.com/oauth20_token.srf", u);
            while (NowSec() < deadline) {
                if (!Sleep(interval * 1000)) {
                    error = "sign-in cancelled";
                    return false;
                }
                r = Post(u,
                         "grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=" +
                             UrlEnc(deviceCode) + "&client_id=" + kMsaClientId,
                         "application/x-www-form-urlencoded", {{"User-Agent", kUserAgent}});
                MsaTokens t;
                std::string code, text;
                if (ReadMsa(r, t, code, text)) {
                    msaAccess = t.access;
                    msaExpiry = NowSec() + t.expiresIn;
                    cache.refreshToken = t.refresh;
                    SaveCache();
                    return true;
                }
                if (code == "authorization_pending" || (r.status == 0))
                    continue;
                if (code == "slow_down") {
                    interval += 5;
                    continue;
                }
                if (code == "expired_token" || code == "authorization_declined" || code == "access_denied" ||
                    code == "bad_verification_code") {
                    error = code == "expired_token" ? "the sign-in code expired before it was entered"
                                                    : "the sign-in was declined";
                    return false;
                }
                error = text;
                return false;
            }
            error = "the sign-in code expired before it was entered";
            return false;
        }

        bool EnsureProofKey() {
            if (proofKey)
                return true;
            if (!cache.proofKeyPem.empty())
                proofKey = KeyFromPem(cache.proofKeyPem);
            if (!proofKey) {
                proofKey = NewProofKey();
                if (!proofKey)
                    return false;
                cache.proofKeyPem = KeyToPem(proofKey);
                cache.deviceId.clear();
                cache.deviceToken.clear();
                cache.deviceNotAfter = 0;
                SaveCache();
            }
            return true;
        }

        bool EnsureDeviceToken(std::string& error) {
            if (!EnsureProofKey()) {
                error = "could not create the Xbox device key";
                return false;
            }
            if (!cache.deviceToken.empty() && ServerNow() < cache.deviceNotAfter - 60) {
                device.token = cache.deviceToken;
                device.notAfter = cache.deviceNotAfter;
                return true;
            }
            if (cache.deviceId.empty())
                cache.deviceId = "{" + RandomUuid() + "}";
            const std::string body =
                "{\"RelyingParty\":\"http://"
                "auth.xboxlive.com\",\"TokenType\":\"JWT\",\"Properties\":{\"AuthMethod\":"
                "\"ProofOfPossession\",\"Id\":" +
                Q(cache.deviceId) +
                ",\"DeviceType\":\"Android\",\"Version\":\"13\","
                "\"ProofKey\":" +
                JwkJson(proofKey) + "}}";
            Url u;
            ParseUrl("https://device.auth.xboxlive.com/device/authenticate", u);
            const http::Response r = PostSigned(u, body, {{"x-xbl-contract-version", "1"}}, Policy{});
            auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
            if (!doc || !ReadTok(doc->root(), device)) {
                error = NetworkError(r, "the Xbox device service");
                return false;
            }
            cache.deviceToken = device.token;
            cache.deviceNotAfter = device.notAfter;
            SaveCache();
            return true;
        }

        Tok sisuXsts;

        bool EnsureUserAndTitle(std::string& error) {
            if (title.Valid() && user.Valid())
                return true;
            if (!EnsureDeviceToken(error))
                return false;
            if (msaAccess.empty() || NowSec() >= msaExpiry - 60) {
                if (!Refresh(error))
                    return false;
            }
            Url u;
            ParseUrl("https://sisu.xboxlive.com/authorize", u);
            const std::string body = "{\"AccessToken\":" + Q("t=" + msaAccess) +
                                     ",\"AppId\":" + Q(kMsaClientId) + ",\"DeviceToken\":" + Q(device.token) +
                                     ",\"Sandbox\":\"RETAIL\",\"UseModernGamertag\":true,"
                                     "\"SiteName\":\"user.auth.xboxlive.com\",\"RelyingParty\":\"http://"
                                     "xboxlive.com\",\"ProofKey\":" +
                                     JwkJson(proofKey) + "}";
            const http::Response r = PostSigned(u, body, {}, PolicyFor(u));
            if (r.status != 200) {
                const auto xerr = r.headers.find("x-err");
                if (xerr != r.headers.end())
                    error = XboxErrorText(std::strtoull(xerr->second.c_str(), nullptr, 10));
                else
                    error = NetworkError(r, "the Xbox sign-in service");
                return false;
            }
            auto doc = json::Document::Parse(r.body);
            if (!doc || !ReadTok(doc->root()["TitleToken"], title) ||
                !ReadTok(doc->root()["UserToken"], user)) {
                error = "the Xbox sign-in service returned an unexpected answer";
                return false;
            }
            ReadTok(doc->root()["AuthorizationToken"], sisuXsts);
            return true;
        }

        bool Xsts(const std::string& relyingParty, Tok& out, std::string& error) {
            if (!EnsureUserAndTitle(error))
                return false;
            const std::string body = "{\"RelyingParty\":" + Q(relyingParty) +
                                     ",\"TokenType\":\"JWT\",\"Properties\":{\"SandboxId\":\"RETAIL\","
                                     "\"DeviceToken\":" +
                                     Q(device.token) + ",\"TitleToken\":" + Q(title.token) +
                                     ",\"UserTokens\":[" + Q(user.token) + "]}}";
            Url u;
            ParseUrl("https://xsts.auth.xboxlive.com/xsts/authorize", u);
            const http::Response r = PostSigned(u, body, {{"x-xbl-contract-version", "1"}}, Policy{});
            auto doc = json::Document::Parse(r.body);
            if (r.status != 200 || !doc || !ReadTok(doc->root(), out)) {
                if (doc && doc->root()["XErr"].isNumber())
                    error = XboxErrorText(static_cast<uint64_t>(doc->root()["XErr"].number()));
                else
                    error = NetworkError(r, "the Xbox token service");
                if (r.status == 401 && !(doc && doc->root()["XErr"].isNumber())) {
                    title = {};
                    user = {};
                }
                return false;
            }
            return true;
        }

        static std::string XblHeader(const Tok& t) { return "XBL3.0 x=" + t.uhs + ";" + t.token; }

        struct ServiceEnv {
            std::string serviceUri = kFallbackServiceUri;
            std::string playFabTitle = kFallbackPlayFabTitle;
            std::string issuer, signalingUri, signalingAfdUri;
        };

        ServiceEnv Discover(const std::string& gameVersion) {
            ServiceEnv env;
            Url u;
            ParseUrl(
                "https://client.discovery.minecraft-services.net/api/v1.0/discovery/MinecraftPE/builds/" +
                    gameVersion,
                u);
            const http::Response r =
                Get(u, {{"Content-Type", "application/json"}, {"User-Agent", "libhttpclient/1.0.0.0"}});
            if (auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt) {
                const json::View prod = doc->root()["result"]["serviceEnvironments"]["auth"]["prod"];
                if (prod["serviceUri"].isString())
                    env.serviceUri = prod["serviceUri"].str();
                if (prod["playFabTitleId"].isString())
                    env.playFabTitle = prod["playFabTitleId"].str();
                if (prod["issuer"].isString())
                    env.issuer = prod["issuer"].str();
                const json::View environments = doc->root()["result"]["serviceEnvironments"];
                if (environments["signaling"]["prod"]["serviceUri"].isString())
                    env.signalingUri = environments["signaling"]["prod"]["serviceUri"].str();
                if (environments["signaling-afd"]["prod"]["serviceUri"].isString())
                    env.signalingAfdUri = environments["signaling-afd"]["prod"]["serviceUri"].str();
            } else {
                Log("auth", "service discovery failed ({}), using built-in endpoints", r.status);
            }
            while (!env.serviceUri.empty() && env.serviceUri.back() == '/')
                env.serviceUri.pop_back();
            return env;
        }

        bool PlayFabTicket(const ServiceEnv& env, std::string& ticket, std::string& playFabId,
                           std::string& error) {
            Url u;
            ParseUrl("https://" + str::Lower(env.playFabTitle) + ".playfabapi.com/Client/LoginWithXbox", u);
            std::string rp = kPlayFabRelyingParty;
            if (const Endpoint* e = MatchEndpoint(u))
                rp = e->relyingParty;
            Tok xsts;
            if (!Xsts(rp, xsts, error))
                return false;
            const std::string body = "{\"CreateAccount\":true,\"TitleId\":" + Q(env.playFabTitle) +
                                     ",\"XboxToken\":" + Q(XblHeader(xsts)) + "}";
            const http::Response r = Post(u, body, "application/json");
            auto doc = json::Document::Parse(r.body);
            const json::View data = doc ? doc->root()["data"] : json::View();
            if (r.status != 200 || !data["SessionTicket"].isString()) {
                error = doc && doc->root()["errorMessage"].isString()
                            ? "PlayFab: " + doc->root()["errorMessage"].str()
                            : NetworkError(r, "PlayFab");
                return false;
            }
            ticket = data["SessionTicket"].str();
            playFabId = data["PlayFabId"].isString() ? data["PlayFabId"].str() : std::string();
            return true;
        }

        bool MinecraftToken(const ServiceEnv& env, const std::string& ticket, const std::string& gameVersion,
                            const std::string& publicKeyB64, std::string& token, std::string& error) {
            if (cache.minecraftDeviceId.empty()) {
                cache.minecraftDeviceId = RandomUuid();
                SaveCache();
            }
            Url u;
            ParseUrl(env.serviceUri + "/api/v1.0/session/start", u);
            const std::string sessionBody =
                "{\"device\":{\"applicationType\":\"MinecraftPE\",\"capabilities\":[],\"gameVersion\":" +
                Q(gameVersion) + ",\"id\":" + Q(cache.minecraftDeviceId) +
                ",\"memory\":\"17179869184\",\"platform\":\"Windows10\","
                "\"playFabTitleId\":" +
                Q(env.playFabTitle) +
                ",\"storePlatform\":\"uwp.store\",\"type\":\"Windows10\"},"
                "\"user\":{\"language\":\"en\",\"languageCode\":\"en-US\",\"regionCode\":\"US\",\"token\":" +
                Q(ticket) + ",\"tokenType\":\"PlayFab\"}}";
            const std::map<std::string, std::string> common{{"Accept", "application/json"},
                                                            {"User-Agent", "libhttpclient/1.0.0.0"}};
            http::Response r = Post(u, sessionBody, "application/json", common);
            auto doc = json::Document::Parse(r.body);
            if (r.status != 200 || !doc || !doc->root()["result"]["authorizationHeader"].isString()) {
                error = doc && doc->root()["message"].isString()
                            ? "Minecraft services: " + doc->root()["message"].str()
                            : NetworkError(r, "the Minecraft authorization service");
                return false;
            }
            const std::string authorization = doc->root()["result"]["authorizationHeader"].str();

            ParseUrl(env.serviceUri + "/api/v1.0/multiplayer/session/start", u);
            auto headers = common;
            headers["Authorization"] = authorization;
            r = Post(u, "{\"publicKey\":" + Q(publicKeyB64) + "}", "application/json", headers);
            doc = json::Document::Parse(r.body);
            if (r.status != 200 || !doc || !doc->root()["result"]["signedToken"].isString()) {
                error = doc && doc->root()["message"].isString()
                            ? "Minecraft services: " + doc->root()["message"].str()
                            : NetworkError(r, "the Minecraft multiplayer service");
                return false;
            }
            token = doc->root()["result"]["signedToken"].str();
            return true;
        }

        bool ServiceAuthorization(const ServiceEnv& env, const std::string& ticket,
                                  const std::string& gameVersion, std::string& authorization,
                                  std::string& error) {
            if (cache.minecraftDeviceId.empty()) {
                cache.minecraftDeviceId = RandomUuid();
                SaveCache();
            }
            Url u;
            ParseUrl(env.serviceUri + "/api/v1.0/session/start", u);
            const std::string sessionBody =
                "{\"device\":{\"applicationType\":\"MinecraftPE\",\"capabilities\":[],\"gameVersion\":" +
                Q(gameVersion) + ",\"id\":" + Q(cache.minecraftDeviceId) +
                ",\"memory\":\"17179869184\",\"platform\":\"Windows10\","
                "\"playFabTitleId\":" +
                Q(env.playFabTitle) +
                ",\"storePlatform\":\"uwp.store\",\"type\":\"Windows10\"},"
                "\"user\":{\"language\":\"en\",\"languageCode\":\"en-US\",\"regionCode\":\"US\",\"token\":" +
                Q(ticket) + ",\"tokenType\":\"PlayFab\"}}";
            const http::Response r =
                Post(u, sessionBody, "application/json",
                     {{"Accept", "application/json"}, {"User-Agent", "libhttpclient/1.0.0.0"}});
            auto doc = json::Document::Parse(r.body);
            if (r.status != 200 || !doc || !doc->root()["result"]["authorizationHeader"].isString()) {
                error = doc && doc->root()["message"].isString()
                            ? "Minecraft services: " + doc->root()["message"].str()
                            : NetworkError(r, "the Minecraft authorization service");
                return false;
            }
            authorization = doc->root()["result"]["authorizationHeader"].str();
            return true;
        }

        bool LegacyChain(const std::string& publicKeyB64, const std::string& gameVersion,
                         std::string& chainJson, std::string& gamertag, std::string& xuid,
                         std::string& error) {
            Tok xsts;
            if (!Xsts(kMultiplayerRelyingParty, xsts, error))
                return false;
            gamertag = xsts.gtg;
            xuid = xsts.xid;
            if (gamertag.empty()) {
                Tok live;
                std::string why;
                if (Xsts("http://xboxlive.com", live, why)) {
                    gamertag = live.gtg;
                    if (xuid.empty())
                        xuid = live.xid;
                }
            }
            Url u;
            ParseUrl("https://multiplayer.minecraft.net/authentication", u);
            const http::Response r =
                Post(u, "{\"identityPublicKey\":" + Q(publicKeyB64) + "}", "application/json",
                     {{"User-Agent", "MCPE/Android"},
                      {"Client-Version", gameVersion},
                      {"Authorization", XblHeader(xsts)}});
            auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
            if (!doc || !doc->root()["chain"].isArray()) {
                error = NetworkError(r, "multiplayer.minecraft.net");
                return false;
            }
            chainJson = r.body;
            for (const json::View jwt : doc->root()["chain"].items()) {
                const std::string s = jwt.str();
                const size_t a = s.find('.');
                const size_t b = a == std::string::npos ? a : s.find('.', a + 1);
                if (b == std::string::npos)
                    continue;
                auto payload = json::Document::Parse(B64Decode(s.substr(a + 1, b - a - 1)));
                if (!payload)
                    continue;
                const json::View extra = payload->root()["extraData"];
                if (extra["displayName"].isString()) {
                    gamertag = extra["displayName"].str();
                    if (extra["XUID"].isString())
                        xuid = extra["XUID"].str();
                }
            }
            return true;
        }
    };

    Account::Account(std::string cachePath) : impl(std::make_unique<Impl>()) {
        impl->cachePath = std::move(cachePath);
        impl->LoadCache();
    }
    Account::~Account() = default;

    bool Account::HasSavedLogin() const {
        return !impl->cache.refreshToken.empty();
    }

    void Account::Cancel() {
        impl->cancelled = true;
    }

    void Account::SignOut(const std::string& cachePath) {
        std::remove(cachePath.c_str());
    }

    bool Account::SignIn(const PromptFn& prompt, std::string& error) {
        if (impl->cache.refreshToken.empty() == false) {
            std::string why;
            if (impl->Refresh(why))
                return true;
            Log("auth", "saved login no longer works ({}); signing in again", why);
        }
        return impl->DeviceCode(prompt, error);
    }

    bool Account::SignInSaved(std::string& error) {
        if (impl->cache.refreshToken.empty()) {
            error = "no saved login";
            return false;
        }
        return impl->Refresh(error);
    }

    bool Account::FetchGamertag(std::string& gamertag, std::string& error) {
        Impl& s = *impl;
        if (!s.EnsureUserAndTitle(error))
            return false;
        Tok xsts;
        if (!s.Xsts("http://xboxlive.com", xsts, error))
            return false;
        gamertag = xsts.gtg;
        if (gamertag.empty())
            error = "the Xbox token had no gamertag";
        return !gamertag.empty();
    }

    bool Account::AuthorizationHeader(const std::string& relyingParty, std::string& header, std::string& xuid,
                                      std::string& error) {
        Impl& s = *impl;
        if (!s.EnsureUserAndTitle(error))
            return false;
        Tok xsts;
        if (!s.Xsts(relyingParty, xsts, error))
            return false;
        header = Impl::XblHeader(xsts);
        xuid = xsts.xid;
        return true;
    }

    bool Account::ServiceSession(const std::string& gameVersion, ServiceInfo& out, std::string& error) {
        out = {};
        Impl& s = *impl;
        if (!s.EnsureUserAndTitle(error))
            return false;
        const Impl::ServiceEnv env = s.Discover(gameVersion);
        std::string ticket, playFabId;
        if (!s.PlayFabTicket(env, ticket, playFabId, error))
            return false;
        if (!s.ServiceAuthorization(env, ticket, gameVersion, out.authorization, error))
            return false;
        const std::string jwt =
            out.authorization.starts_with("MCToken ") ? out.authorization.substr(8) : out.authorization;
        const size_t a = jwt.find('.');
        const size_t b = a == std::string::npos ? a : jwt.find('.', a + 1);
        if (b != std::string::npos)
            if (auto payload = json::Document::Parse(B64Decode(jwt.substr(a + 1, b - a - 1))))
                if (payload->root()["pmid"].isString())
                    out.playerMessagingId = payload->root()["pmid"].str();
        out.issuer = env.issuer;
        out.signalingUri = env.signalingUri;
        out.signalingAfdUri = env.signalingAfdUri;
        if (out.playerMessagingId.empty()) {
            error = "the Minecraft services token has no player messaging id";
            return false;
        }
        return true;
    }

    bool Account::LoginMinecraft(const std::string& publicKeyB64, const std::string& gameVersion,
                                 Credentials& out, std::string& error) {
        out = {};
        Impl& s = *impl;
        Log("auth", "contacting Xbox Live...");
        auto discovery =
            std::async(std::launch::async, [&s, &gameVersion] { return s.Discover(gameVersion); });
        std::string ensureError;
        const bool tokensReady = s.EnsureUserAndTitle(ensureError);
        struct Chain {
            std::string json, gamertag, xuid, error;
            bool ok = false;
        };
        std::future<Chain> chainTask;
        if (tokensReady)
            chainTask = std::async(std::launch::async, [&s, &publicKeyB64, &gameVersion] {
                Chain c;
                c.ok = s.LegacyChain(publicKeyB64, gameVersion, c.json, c.gamertag, c.xuid, c.error);
                return c;
            });
        const Impl::ServiceEnv env = discovery.get();
        std::string ticket;
        const bool playFabOk = s.PlayFabTicket(env, ticket, out.playFabId, error);
        if (playFabOk)
            Log("auth", "PlayFab login ok");
        const bool tokenOk = playFabOk && s.MinecraftToken(env, ticket, gameVersion, publicKeyB64,
                                                           out.multiplayerToken, error);
        Chain chain;
        if (chainTask.valid())
            chain = chainTask.get();
        if (!playFabOk || !tokenOk) {
            if (!playFabOk && error.empty())
                error = ensureError;
            return false;
        }
        Log("auth", "multiplayer token ok");
        out.gamertag = chain.gamertag;
        out.xuid = chain.xuid;
        if (chain.ok) {
            out.chainJson = chain.json;
        } else {
            Log("auth", "legacy certificate chain unavailable: {}",
                (tokensReady ? chain.error : ensureError));
            out.chainJson = "{\"chain\":[]}";
        }
        Log("auth", "signed in as {}", out.gamertag);
        return true;
    }
}
