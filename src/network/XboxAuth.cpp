#include "network/XboxAuth.hpp"
#include "network/XboxRelyingParties.hpp"
#include <future>

#include "network/ClientIdentity.hpp"
#include "network/TokenStore.hpp"
#include "network/Http.hpp"
#include "util/Json.hpp"
#include "util/Strings.hpp"
#include "util/UtcTime.hpp"

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
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include "util/Log.hpp"
#include "util/FileIo.hpp"
#include "util/JsonText.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace perlin::xbox {
    namespace {
        constexpr const char* kMsaClientId = "0000000048183522";
        constexpr const char* kMsaScope = "service::user.auth.xboxlive.com::MBI_SSL";
        constexpr const char* kUserAgent = "XAL Android 2025.04.20250326.000";
        constexpr const char* kSandbox = "RETAIL";
        constexpr int64_t kTitleId = 1739947436;
        constexpr const char* kFallbackServiceUri = "https://authorization.franchise.minecraft-services.net";
        constexpr const char* kFallbackPlayFabTitle = "20CA2";
        constexpr const char* kXboxLiveRelyingParty = "http://xboxlive.com";
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

        using Policy = auth::SignaturePolicy;

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

        bool KeyFromPemValid(const std::string& pem) {
            EC_KEY* key = KeyFromPem(pem);
            if (!key)
                return false;
            EC_KEY_free(key);
            return true;
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

        using Tok = auth::Token;

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

        using Endpoint = auth::NsalEndpoint;

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
            return utc::UnixSeconds(y, mo, d, h, mi, s);
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
            return utc::UnixSeconds(y, m + 1, d, h, mi, s);
        }
    }

    struct Account::Impl {
        std::shared_ptr<auth::TokenStore> store;
        std::atomic<bool> cancelled{false};
        std::mutex keyMutex;
        std::shared_ptr<EC_KEY> keyHandle;
        std::string keyPem;

        explicit Impl(std::shared_ptr<auth::TokenStore> s) : store(std::move(s)) {}

        static std::shared_ptr<EC_KEY> Wrap(EC_KEY* k) {
            return std::shared_ptr<EC_KEY>(k, [](EC_KEY* p) {
                if (p)
                    EC_KEY_free(p);
            });
        }

        auth::TokenStore::EnsureOptions Cross(bool persist = true) const {
            return {auth::TokenStore::Scope::Account, persist, &cancelled};
        }
        auth::TokenStore::EnsureOptions Local(bool persist = true) const {
            return {persist ? auth::TokenStore::Scope::Key : auth::TokenStore::Scope::Process, persist, &cancelled};
        }

        int64_t ServerNow() const { return store->ServerNow(); }
        int64_t ServerNanos() const { return store->ServerNanos(); }
        void NoteDate(const http::Response& r) {
            const auto it = r.headers.find("date");
            if (it == r.headers.end())
                return;
            if (const int64_t t = detail::ParseHttpDate(it->second))
                store->SetSkew(t - store->LocalNow());
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
                return std::string("cannot reach ") + what + " (" +
                       (r.error.empty() ? std::string("no network, or TLS failed") : r.error) + ")";
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

        std::shared_ptr<const auth::Nsal> LoadNsal() {
            std::string error;
            store->Ensure(
                "nsal", Local(false),
                [this](const auth::State& s) {
                    const int64_t age = store->LocalNow() - s.nsalAt;
                    return s.nsal && age >= 0 && age < auth::policy::kNsalTtlSec;
                },
                [this](const auth::State&, auth::Mutator& commit, std::string&) {
                    auto fetched = std::make_shared<auth::Nsal>();
                    bool loaded = false;
                    Url u;
                    ParseUrl("https://title.mgt.xboxlive.com/titles/default/endpoints?type=1", u);
                    const http::Response r = Get(u, {{"x-xbl-contract-version", "1"}});
                    auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
                    if (!doc) {
                        Log("auth", "could not load Xbox endpoint policies ({}); using defaults", r.status);
                    } else {
                        loaded = true;
                        for (const json::View p : doc->root()["SignaturePolicies"].items()) {
                            Policy pol;
                            pol.version = static_cast<uint32_t>(p["Version"].integer(1));
                            pol.maxBody = static_cast<size_t>(p["MaxBodyBytes"].integer(0));
                            for (const json::View h : p["ExtraHeaders"].items())
                                pol.extraHeaders.push_back(h.str());
                            fetched->policies.push_back(std::move(pol));
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
                            fetched->endpoints.push_back(std::move(ep));
                        }
                    }
                    const int64_t now = store->LocalNow();
                    const int64_t stamp = loaded ? now : now - auth::policy::kNsalTtlSec + auth::policy::kNsalRetrySec;
                    commit = [fetched, stamp](auth::State& s) {
                        s.nsal = fetched;
                        s.nsalAt = stamp;
                    };
                    return true;
                },
                error);
            return store->Current()->nsal;
        }

        std::optional<Endpoint> MatchEndpoint(const Url& u) {
            const auto nsal = LoadNsal();
            if (!nsal)
                return std::nullopt;
            const Endpoint* found = nullptr;
            const std::string scheme = u.tls ? "https" : "http";
            const std::string path = u.path.substr(0, u.path.find('?'));
            for (const Endpoint& e : nsal->endpoints) {
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
            if (!found)
                return std::nullopt;
            return *found;
        }

        Policy PolicyFor(const Url& u) {
            if (const auto e = MatchEndpoint(u)) {
                const auto nsal = store->Current()->nsal;
                if (nsal && e->policy >= 0 && e->policy < static_cast<int>(nsal->policies.size()))
                    return nsal->policies[static_cast<size_t>(e->policy)];
            }
            return {};
        }

        std::shared_ptr<EC_KEY> Key() {
            const auto snap = store->Current();
            std::lock_guard<std::mutex> lock(keyMutex);
            if (!keyHandle || keyPem != snap->data.proofKeyPem) {
                keyHandle = Wrap(KeyFromPem(snap->data.proofKeyPem));
                keyPem = snap->data.proofKeyPem;
            }
            return keyHandle;
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
            const auto key = Key();
            headers["Signature"] = SignWith(key.get(), policy, "POST", u.path, values, body, ServerNanos());
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

        bool RefreshMsa(std::string& error) {
            return store->Ensure(
                "msa", Cross(),
                [this](const auth::State& s) { return auth::policy::MsaUsable(s, store->LocalNow()); },
                [this](const auth::State& snap, auth::Mutator& commit, std::string& why) {
                    if (snap.data.refreshToken.empty()) {
                        why = "no saved login";
                        return false;
                    }
                    Url u;
                    ParseUrl("https://login.live.com/oauth20_token.srf", u);
                    const http::Response r =
                        Post(u,
                             "grant_type=refresh_token&refresh_token=" + UrlEnc(snap.data.refreshToken) +
                                 "&scope=" + UrlEnc(kMsaScope) + "&client_id=" + kMsaClientId,
                             "application/x-www-form-urlencoded", {{"User-Agent", kUserAgent}});
                    MsaTokens t;
                    std::string code;
                    if (!ReadMsa(r, t, code, why)) {
                        if (r.status != 0 && (code == "invalid_grant" || r.status == 400 || r.status == 401))
                            store->Publish([](auth::State& s) {
                                s.data.refreshToken.clear();
                                s.msaAccess.clear();
                                s.msaExpiry = 0;
                            });
                        return false;
                    }
                    const int64_t expiry = store->LocalNow() + t.expiresIn;
                    commit = [t, expiry](auth::State& s) {
                        s.msaAccess = t.access;
                        s.msaExpiry = expiry;
                        if (!t.refresh.empty())
                            s.data.refreshToken = t.refresh;
                    };
                    return true;
                },
                error);
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
            const int64_t deadline = store->LocalNow() + static_cast<int64_t>(root["expires_in"].number(900));
            prompt(verify, userCode);

            ParseUrl("https://login.live.com/oauth20_token.srf", u);
            while (store->LocalNow() < deadline) {
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
                    const int64_t expiry = store->LocalNow() + t.expiresIn;
                    store->Publish([t, expiry](auth::State& s) {
                        s.msaAccess = t.access;
                        s.msaExpiry = expiry;
                        s.data.refreshToken = t.refresh;
                        s.data.sisu = {};
                        s.data.xsts.clear();
                    });
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

        bool EnsureProofKey(std::string& error) {
            const bool ok = store->Ensure(
                "proofkey", Cross(),
                [](const auth::State& s) { return !s.data.proofKeyPem.empty() && KeyFromPemValid(s.data.proofKeyPem); },
                [](const auth::State&, auth::Mutator& commit, std::string& why) {
                    EC_KEY* fresh = NewProofKey();
                    if (!fresh) {
                        why = "could not create the Xbox device key";
                        return false;
                    }
                    const std::string pem = KeyToPem(fresh);
                    EC_KEY_free(fresh);
                    commit = [pem](auth::State& s) {
                        s.data.proofKeyPem = pem;
                        s.data.deviceId.clear();
                        s.data.deviceToken.clear();
                        s.data.deviceNotAfter = 0;
                        s.data.sisu = {};
                        s.data.xsts.clear();
                    };
                    return true;
                },
                error);
            return ok;
        }

        bool EnsureDevice(std::string& error) {
            if (!EnsureProofKey(error))
                return false;
            return store->Ensure(
                "device", Cross(),
                [this](const auth::State& s) { return auth::policy::DeviceUsable(s.data, ServerNow()); },
                [this](const auth::State& snap, auth::Mutator& commit, std::string& why) {
                    const std::string deviceId =
                        snap.data.deviceId.empty() ? "{" + RandomUuid() + "}" : snap.data.deviceId;
                    const auto key = Key();
                    if (!key) {
                        why = "could not load the Xbox device key";
                        return false;
                    }
                    const std::string body =
                        "{\"RelyingParty\":\"http://"
                        "auth.xboxlive.com\",\"TokenType\":\"JWT\",\"Properties\":{\"AuthMethod\":"
                        "\"ProofOfPossession\",\"Id\":" +
                        json::Quote(deviceId) +
                        ",\"DeviceType\":\"Android\",\"Version\":\"13\","
                        "\"ProofKey\":" +
                        JwkJson(key.get()) + "}}";
                    Url u;
                    ParseUrl("https://device.auth.xboxlive.com/device/authenticate", u);
                    const http::Response r = PostSigned(u, body, {{"x-xbl-contract-version", "1"}}, Policy{});
                    auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
                    Tok device;
                    if (!doc || !ReadTok(doc->root(), device)) {
                        why = NetworkError(r, "the Xbox device service");
                        return false;
                    }
                    commit = [device, deviceId](auth::State& s) {
                        s.data.deviceId = deviceId;
                        s.data.deviceToken = device.token;
                        s.data.deviceNotAfter = device.notAfter;
                        s.data.sisu = {};
                    };
                    return true;
                },
                error);
        }

        bool SisuUsable() const {
            const auto snap = store->Current();
            return auth::policy::SisuUsable(snap->data, store->LocalNow(), ServerNow());
        }

        bool EnsureSisu(std::string& error) {
            return store->Ensure(
                "sisu", Cross(),
                [this](const auth::State& s) {
                    return auth::policy::SisuUsable(s.data, store->LocalNow(), ServerNow());
                },
                [this](const auth::State&, auth::Mutator& commit, std::string& why) {
                    if (!EnsureDevice(why))
                        return false;
                    if (!RefreshMsaIfNeeded(why))
                        return false;
                    const auto snap = store->Current();
                    const auto key = Key();
                    if (!key) {
                        why = "could not load the Xbox device key";
                        return false;
                    }
                    Url u;
                    ParseUrl("https://sisu.xboxlive.com/authorize", u);
                    const std::string body = "{\"AccessToken\":" + json::Quote("t=" + snap->msaAccess) +
                                             ",\"AppId\":" + json::Quote(kMsaClientId) +
                                             ",\"DeviceToken\":" + json::Quote(snap->data.deviceToken) +
                                             ",\"Sandbox\":\"RETAIL\",\"UseModernGamertag\":true,"
                                             "\"SiteName\":\"user.auth.xboxlive.com\",\"RelyingParty\":\"http://"
                                             "xboxlive.com\",\"ProofKey\":" +
                                             JwkJson(key.get()) + "}";
                    const http::Response r = PostSigned(u, body, {}, PolicyFor(u));
                    if (r.status != 200) {
                        const auto xerr = r.headers.find("x-err");
                        if (xerr != r.headers.end())
                            why = XboxErrorText(std::strtoull(xerr->second.c_str(), nullptr, 10));
                        else
                            why = NetworkError(r, "the Xbox sign-in service");
                        return false;
                    }
                    auto doc = json::Document::Parse(r.body);
                    Tok title, user;
                    if (!doc || !ReadTok(doc->root()["TitleToken"], title) ||
                        !ReadTok(doc->root()["UserToken"], user)) {
                        why = "the Xbox sign-in service returned an unexpected answer";
                        return false;
                    }
                    const int64_t acquired = store->LocalNow();
                    const std::string binding = auth::policy::Fingerprint(snap->data.deviceToken);
                    commit = [title, user, acquired, binding](auth::State& s) {
                        s.data.sisu.title = title;
                        s.data.sisu.user = user;
                        s.data.sisu.acquiredAt = acquired;
                        s.data.sisu.deviceBinding = binding;
                    };
                    return true;
                },
                error);
        }

        bool RefreshMsaIfNeeded(std::string& error) { return RefreshMsa(error); }

        void InvalidateSisu(const std::string& rejectedTitle) {
            store->Publish([rejectedTitle](auth::State& s) {
                if (s.data.sisu.title.token != rejectedTitle)
                    return;
                s.data.sisu = {};
                s.data.xsts.clear();
            });
        }

        void InvalidateXsts(const std::string& relyingParty) {
            store->Publish([relyingParty](auth::State& s) { s.data.xsts.erase(relyingParty); });
        }

        bool Xsts(const std::string& relyingParty, Tok& out, std::string& error) {
            for (int attempt = 0; attempt < 3; attempt++) {
                bool sisuRejected = false;
                std::string rejectedTitle;
                const bool ok = store->Ensure(
                    "xsts:" + relyingParty, Local(),
                    [this, &relyingParty](const auth::State& s) {
                        return auth::policy::XstsUsable(s.data, relyingParty, ServerNow());
                    },
                    [this, &relyingParty, &sisuRejected, &rejectedTitle](const auth::State&, auth::Mutator& commit,
                                                         std::string& why) {
                        if (!EnsureDevice(why) || !EnsureSisu(why))
                            return false;
                        const auto snap = store->Current();
                        rejectedTitle = snap->data.sisu.title.token;
                        const std::string body =
                            "{\"RelyingParty\":" + json::Quote(relyingParty) +
                            ",\"TokenType\":\"JWT\",\"Properties\":{\"SandboxId\":\"RETAIL\","
                            "\"DeviceToken\":" +
                            json::Quote(snap->data.deviceToken) + ",\"TitleToken\":" + json::Quote(snap->data.sisu.title.token) +
                            ",\"UserTokens\":[" + json::Quote(snap->data.sisu.user.token) + "]}}";
                        Url u;
                        ParseUrl("https://xsts.auth.xboxlive.com/xsts/authorize", u);
                        const http::Response r =
                            PostSigned(u, body, {{"x-xbl-contract-version", "1"}}, Policy{});
                        auto doc = json::Document::Parse(r.body);
                        Tok issued;
                        if (r.status != 200 || !doc || !ReadTok(doc->root(), issued)) {
                            const bool hasXErr = doc && doc->root()["XErr"].isNumber();
                            if (hasXErr)
                                why = XboxErrorText(static_cast<uint64_t>(doc->root()["XErr"].number()));
                            else
                                why = NetworkError(r, "the Xbox token service");
                            sisuRejected = r.status == 401 && !hasXErr;
                            return false;
                        }
                        commit = [relyingParty, issued](auth::State& s) { s.data.xsts[relyingParty] = issued; };
                        return true;
                    },
                    error);
                if (ok) {
                    const auto snap = store->Current();
                    const auto it = snap->data.xsts.find(relyingParty);
                    if (it != snap->data.xsts.end() && auth::policy::XstsUsable(snap->data, relyingParty, ServerNow())) {
                        out = it->second;
                        return true;
                    }
                    continue;
                }
                if (sisuRejected && attempt == 0) {
                    InvalidateSisu(rejectedTitle);
                    continue;
                }
                return false;
            }
            if (error.empty())
                error = "the Xbox token expired while it was being issued";
            return false;
        }

        std::string MinecraftDeviceId() {
            std::string error;
            store->Ensure(
                "mcdevice", Cross(), [](const auth::State& s) { return !s.data.minecraftDeviceId.empty(); },
                [](const auth::State&, auth::Mutator& commit, std::string&) {
                    const std::string id = RandomUuid();
                    commit = [id](auth::State& s) { s.data.minecraftDeviceId = id; };
                    return true;
                },
                error);
            return store->Current()->data.minecraftDeviceId;
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
            const auto advertised = MatchEndpoint(u);
            const std::string party = rp::ForPlayFab(advertised ? advertised->relyingParty : std::string());
            Tok xsts;
            if (!Xsts(party, xsts, error))
                return false;
            const std::string body = "{\"CreateAccount\":true,\"TitleId\":" + json::Quote(env.playFabTitle) +
                                     ",\"XboxToken\":" + json::Quote(XblHeader(xsts)) + "}";
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
            const std::string minecraftDeviceId = MinecraftDeviceId();
            Url u;
            ParseUrl(env.serviceUri + "/api/v1.0/session/start", u);
            const std::string sessionBody =
                "{\"device\":{\"applicationType\":\"MinecraftPE\",\"capabilities\":[],\"gameVersion\":" +
                json::Quote(gameVersion) + ",\"id\":" + json::Quote(minecraftDeviceId) +
                ",\"memory\":\"17179869184\",\"platform\":\"Windows10\","
                "\"playFabTitleId\":" +
                json::Quote(env.playFabTitle) +
                ",\"storePlatform\":\"uwp.store\",\"type\":\"Windows10\"},"
                "\"user\":{\"language\":\"en\",\"languageCode\":\"en-US\",\"regionCode\":\"US\",\"token\":" +
                json::Quote(ticket) + ",\"tokenType\":\"PlayFab\"}}";
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
            r = Post(u, "{\"publicKey\":" + json::Quote(publicKeyB64) + "}", "application/json", headers);
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
            const std::string minecraftDeviceId = MinecraftDeviceId();
            Url u;
            ParseUrl(env.serviceUri + "/api/v1.0/session/start", u);
            const std::string sessionBody =
                "{\"device\":{\"applicationType\":\"MinecraftPE\",\"capabilities\":[],\"gameVersion\":" +
                json::Quote(gameVersion) + ",\"id\":" + json::Quote(minecraftDeviceId) +
                ",\"memory\":\"17179869184\",\"platform\":\"Windows10\","
                "\"playFabTitleId\":" +
                json::Quote(env.playFabTitle) +
                ",\"storePlatform\":\"uwp.store\",\"type\":\"Windows10\"},"
                "\"user\":{\"language\":\"en\",\"languageCode\":\"en-US\",\"regionCode\":\"US\",\"token\":" +
                json::Quote(ticket) + ",\"tokenType\":\"PlayFab\"}}";
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
                if (Xsts(kXboxLiveRelyingParty, live, why)) {
                    gamertag = live.gtg;
                    if (xuid.empty())
                        xuid = live.xid;
                }
            }
            Url u;
            ParseUrl("https://multiplayer.minecraft.net/authentication", u);
            const http::Response r =
                Post(u, "{\"identityPublicKey\":" + json::Quote(publicKeyB64) + "}", "application/json",
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

    Account::Account(std::string cachePath) : impl(std::make_unique<Impl>(auth::TokenStore::Open(cachePath))) {}
    Account::Account(std::shared_ptr<auth::TokenStore> store) : impl(std::make_unique<Impl>(std::move(store))) {}
    Account::~Account() = default;

    bool Account::HasSavedLogin() const {
        return !impl->store->Current()->data.refreshToken.empty();
    }

    void Account::Cancel() {
        impl->cancelled = true;
    }

    void Account::SignOut(const std::string& cachePath) {
        auth::TokenStore::Forget(cachePath);
    }

    void Account::Invalidate(const std::string& relyingParty) {
        impl->InvalidateXsts(relyingParty);
    }

    bool Account::SignIn(const PromptFn& prompt, std::string& error) {
        if (HasSavedLogin()) {
            std::string why;
            if (impl->SisuUsable() || impl->RefreshMsa(why))
                return true;
            Log("auth", "saved login no longer works ({}); signing in again", why);
        }
        return impl->DeviceCode(prompt, error);
    }

    bool Account::SignInSaved(std::string& error) {
        if (!HasSavedLogin()) {
            error = "no saved login";
            return false;
        }
        return impl->SisuUsable() || impl->RefreshMsa(error);
    }

    bool Account::FetchGamertag(std::string& gamertag, std::string& error) {
        Impl& s = *impl;
        Tok xsts;
        if (!s.Xsts(kXboxLiveRelyingParty, xsts, error))
            return false;
        gamertag = xsts.gtg;
        if (gamertag.empty())
            error = "the Xbox token had no gamertag";
        return !gamertag.empty();
    }

    bool Account::AuthorizationHeader(const std::string& relyingParty, std::string& header, std::string& xuid,
                                      std::string& error) {
        Impl& s = *impl;
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
        if (!s.EnsureSisu(error))
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
        const bool tokensReady = s.EnsureSisu(ensureError);
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
