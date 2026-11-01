#include "core/Config.hpp"

#include "core/Json.hpp"
#include "core/Log.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace perlin::server {

    ServerConfig ServerConfig::Load(const std::string& path, std::string* error) {
        if (path.size() > 11 && path.compare(path.size() - 11, 11, ".properties") == 0)
            return LoadProperties(path, error);
        ServerConfig c;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return c;
        std::stringstream buf;
        buf << in.rdbuf();
        json::Value root;
        std::string err;
        if (!json::Parse(buf.str(), root, &err)) {
            if (error)
                *error = path + ": " + err;
            return c;
        }
        c.name = root["name"].String(c.name);
        c.motd = root["motd"].String(c.motd);
        c.levelName = root["level_name"].String(c.levelName);
        c.xboxAuth = root["xbox_auth"].Bool(c.xboxAuth);
        for (const json::Value& k : root["auth_root_keys"].items)
            if (k.IsString())
                c.authRootKeys.push_back(k.text);
        c.pluginDir = root["plugin_dir"].String(c.pluginDir);
        c.blockDataDir = root["block_data_dir"].String(c.blockDataDir);
        c.maxPlayers =
            static_cast<uint32_t>(std::clamp<int64_t>(root["max_players"].Int(c.maxPlayers), 1, 1000));
        c.viewDistance =
            static_cast<uint32_t>(std::clamp<int64_t>(root["view_distance"].Int(c.viewDistance), 2, 64));
        c.tickRate = static_cast<uint32_t>(std::clamp<int64_t>(root["tick_rate"].Int(c.tickRate), 1, 200));
        c.gameMode = static_cast<int>(root["gamemode"].Int(c.gameMode));
        if (c.gameMode != 0 && c.gameMode != 1 && c.gameMode != 2 && c.gameMode != 6)
            c.gameMode = 0;
        c.seed = static_cast<uint64_t>(root["seed"].Int(static_cast<int64_t>(c.seed)));
        c.whitelist = root["whitelist"].Bool(c.whitelist);
        c.lanDiscovery = root["lan_discovery"].Bool(c.lanDiscovery);
        c.maxThreads = static_cast<decltype(c.maxThreads)>(
            std::clamp<int64_t>(root["max_threads"].Int(static_cast<int64_t>(c.maxThreads)), 0, 256));
        c.nethernet = root["nethernet"].Bool(c.nethernet);
        c.port = static_cast<uint16_t>(std::clamp<int64_t>(root["port"].Int(c.port), 1, 65535));
        c.bindAddress = root["bind_address"].String(c.bindAddress);
        for (const json::Value& s : root["ice_servers"].items)
            if (s.IsString())
                c.iceServers.push_back(s.text);
        for (const json::Value& o : root["operators"].items)
            if (o.IsString())
                c.operators.push_back(o.text);
        c.iceUdpMux = root["ice_udp_mux"].Bool(c.iceUdpMux);
        c.iceBindAddress = root["ice_bind_address"].String(c.iceBindAddress);
        c.generator = root["generator"].String(c.generator);
        c.terrainBackend = root["terrain_backend"].String(c.terrainBackend);
        if (root["flat_layers"].IsArray()) {
            c.flatLayers.clear();
            for (const json::Value& l : root["flat_layers"].items) {
                FlatLayer layer;
                layer.block = l["block"].String();
                layer.count = static_cast<int>(std::clamp<int64_t>(l["count"].Int(1), 1, 384));
                if (!layer.block.empty())
                    c.flatLayers.push_back(std::move(layer));
            }
        }
        return c;
    }

    bool ServerConfig::Save(const std::string& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << "{\n"
            << "  \"name\": \"" << json::Escape(name) << "\",\n"
            << "  \"motd\": \"" << json::Escape(motd) << "\",\n"
            << "  \"level_name\": \"" << json::Escape(levelName) << "\",\n"
            << "  \"max_players\": " << maxPlayers << ",\n"
            << "  \"view_distance\": " << viewDistance << ",\n"
            << "  \"tick_rate\": " << tickRate << ",\n"
            << "  \"gamemode\": " << gameMode << ",\n"
            << "  \"seed\": " << static_cast<int64_t>(seed) << ",\n"
            << "  \"nethernet\": " << (nethernet ? "true" : "false") << ",\n"
            << "  \"port\": " << port << ",\n"
            << "  \"bind_address\": \"" << json::Escape(bindAddress) << "\",\n"
            << "  \"ice_udp_mux\": " << (iceUdpMux ? "true" : "false") << ",\n"
            << "  \"ice_bind_address\": \"" << json::Escape(iceBindAddress) << "\",\n"
            << "  \"generator\": \"" << json::Escape(generator) << "\",\n"
            << "  \"terrain_backend\": \"" << json::Escape(terrainBackend) << "\",\n"
            << "  \"operators\": [";
        for (size_t i = 0; i < operators.size(); i++)
            out << (i ? ", " : "") << "\"" << json::Escape(operators[i]) << "\"";
        out << "],\n"
            << "  \"xbox_auth\": " << (xboxAuth ? "true" : "false") << ",\n"
            << "  \"whitelist\": " << (whitelist ? "true" : "false") << ",\n"
            << "  \"lan_discovery\": " << (lanDiscovery ? "true" : "false") << ",\n"
            << "  \"plugin_dir\": \"" << json::Escape(pluginDir) << "\",\n"
            << "  \"block_data_dir\": \"" << json::Escape(blockDataDir) << "\",\n"
            << "  \"flat_layers\": [";
        for (size_t i = 0; i < flatLayers.size(); i++)
            out << (i ? ", " : "") << "{\"block\": \"" << json::Escape(flatLayers[i].block)
                << "\", \"count\": " << flatLayers[i].count << "}";
        out << "]\n}\n";
        return out.good();
    }

    namespace {

        std::string LowerText(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool ParseBool(const std::string& v, bool& out) {
            const std::string t = LowerText(v);
            if (t == "true" || t == "1") {
                out = true;
                return true;
            }
            if (t == "false" || t == "0") {
                out = false;
                return true;
            }
            return false;
        }

        bool ParseInt(const std::string& v, int64_t& out) {
            if (v.empty())
                return false;
            char* end = nullptr;
            const long long n = std::strtoll(v.c_str(), &end, 10);
            if (end == v.c_str() || *end != '\0')
                return false;
            out = n;
            return true;
        }

        std::vector<std::string> Split(const std::string& s, char sep) {
            std::vector<std::string> out;
            size_t start = 0;
            for (;;) {
                const size_t at = s.find(sep, start);
                std::string part =
                    str::Trim(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
                if (!part.empty())
                    out.push_back(std::move(part));
                if (at == std::string::npos)
                    break;
                start = at + 1;
            }
            return out;
        }

        // "19132" or "19400-19500"
        bool ParsePortRange(const std::string& text, uint16_t& first, uint16_t& last) {
            const size_t dash = text.find('-');
            int64_t a = 0, b = 0;
            if (dash == std::string::npos) {
                if (!ParseInt(str::Trim(text), a))
                    return false;
                b = a;
            } else if (!ParseInt(str::Trim(text.substr(0, dash)), a) ||
                       !ParseInt(str::Trim(text.substr(dash + 1)), b)) {
                return false;
            }
            if (a < 1 || b > 65535 || a > b)
                return false;
            first = static_cast<uint16_t>(a);
            last = static_cast<uint16_t>(b);
            return true;
        }

        // [ip:][public:]local, where public and local are a port or a first-last range (bds "UDP port configuration for NetherNet")
        bool ParseUdpPorts(const std::string& text, ServerConfig& c) {
            std::string t = str::Trim(text);
            c.udpPortsSet = true;
            c.udpPublicAddress.clear();
            c.udpPublicBegin = c.udpPublicEnd = 0;
            if (t.empty()) {
                // empty = an ephemeral port per connection
                c.udpPortBegin = c.udpPortEnd = 0;
                return true;
            }
            if (t[0] == '[') {
                const size_t close = t.find(']');
                if (close == std::string::npos)
                    return false;
                c.udpPublicAddress = t.substr(1, close - 1);
                t = t.substr(close + 1);
                if (t.empty() || t[0] != ':')
                    return false;
                t.erase(0, 1);
            } else if (std::count(t.begin(), t.end(), ':') == 2) {
                const size_t colon = t.find(':');
                c.udpPublicAddress = t.substr(0, colon);
                t.erase(0, colon + 1);
            }
            const std::vector<std::string> parts = Split(t, ':');
            if (parts.empty() || parts.size() > 2)
                return false;
            uint16_t localFirst = 0, localLast = 0, pubFirst = 0, pubLast = 0;
            if (!ParsePortRange(parts.back(), localFirst, localLast))
                return false;
            if (parts.size() == 2) {
                if (!ParsePortRange(parts[0], pubFirst, pubLast) ||
                    pubLast - pubFirst != localLast - localFirst)
                    return false;
                c.udpPublicBegin = pubFirst;
                c.udpPublicEnd = pubLast;
            } else if (!c.udpPublicAddress.empty()) {
                // an address needs both port values
                return false;
            }
            c.udpPortBegin = localFirst;
            c.udpPortEnd = localLast;
            return true;
        }

        std::string UdpPortsText(const ServerConfig& c) {
            if (!c.udpPortsSet)
                return std::to_string(c.port);
            const auto range = [](uint16_t a, uint16_t b) {
                return a == b ? std::to_string(a) : std::to_string(a) + "-" + std::to_string(b);
            };
            if (c.udpPortBegin == 0)
                return "";
            std::string out = range(c.udpPortBegin, c.udpPortEnd);
            if (c.udpPublicBegin != 0)
                out = range(c.udpPublicBegin, c.udpPublicEnd) + ":" + out;
            if (!c.udpPublicAddress.empty())
                out =
                    (c.udpPublicAddress.contains(':') ? "[" + c.udpPublicAddress + "]" : c.udpPublicAddress) +
                    ":" + out;
            return out;
        }

        const char* GameModeName(int mode) {
            return mode == 1 ? "creative" : mode == 2 ? "adventure" : mode == 6 ? "spectator" : "survival";
        }
        const char* DifficultyName(int d) {
            return d == 0 ? "peaceful" : d == 2 ? "normal" : d == 3 ? "hard" : "easy";
        }

        // the keys of bds's own server.properties, in its order, with its defaults; the ones this server acts on are read from the config when written
        struct BdsKey {
            const char* key;
            const char* fallback;
        };
        constexpr BdsKey kBdsKeys[] = {
            {"server-name", "Dedicated Server"},
            {"gamemode", "survival"},
            {"force-gamemode", "false"},
            {"difficulty", "easy"},
            {"allow-cheats", "false"},
            {"max-players", "10"},
            {"online-mode", "true"},
            {"allow-list", "true"},
            {"server-port", "19132"},
            {"server-portv6", "19133"},
            {"server-ip", ""},
            {"server-udp-ports", "19132"},
            {"transport", "nethernet"},
            {"enable-lan-visibility", "true"},
            {"view-distance", "32"},
            {"tick-distance", "4"},
            {"persistent-ticking-area-max-count", "10"},
            {"persistent-ticking-area-max-chunks", "100"},
            {"transient-ticking-area-max-chunks", "300"},
            {"player-idle-timeout", "30"},
            {"max-threads", "8"},
            {"level-name", "Bedrock level"},
            {"level-seed", ""},
            {"default-player-permission-level", "member"},
            {"texturepack-required", "false"},
            {"content-log-file-enabled", "false"},
            {"content-log-console-output-enabled", "false"},
            {"content-log-level", "info"},
            {"compression-threshold", "1"},
            {"compression-algorithm", "zlib"},
            {"server-authoritative-movement-strict", "false"},
            {"server-authoritative-dismount-strict", "false"},
            {"server-authoritative-entity-interactions-strict", "false"},
            {"server-authoritative-entity-attack-request-validation", "disabled"},
            {"server-authoritative-entity-attack-range-validation", "disabled"},
            {"server-authoritative-entity-attack-range-tolerance", "0.5"},
            {"player-position-acceptance-threshold", "0.5"},
            {"player-movement-action-direction-threshold", "0.85"},
            {"server-authoritative-block-breaking-pick-range-scalar", "1.5"},
            {"chat-restriction", "None"},
            {"disable-player-interaction", "false"},
            {"client-side-chunk-generation-enabled", "true"},
            {"block-network-ids-are-hashes", "true"},
            {"disable-persona", "false"},
            {"disable-custom-skins", "false"},
            {"server-build-radius-ratio", "Disabled"},
            {"enable-packet-rate-limiter", "false"},
            {"allow-outbound-script-debugging", "false"},
            {"allow-inbound-script-debugging", "false"},
            {"script-debugger-auto-attach", "disabled"},
        };

        std::string JoinList(const std::vector<std::string>& v) {
            std::string out;
            for (size_t i = 0; i < v.size(); i++)
                out += (i ? "," : "") + v[i];
            return out;
        }

        std::string FlatLayersText(const std::vector<FlatLayer>& layers) {
            std::string out;
            for (size_t i = 0; i < layers.size(); i++)
                out += (i ? "," : "") + layers[i].block + "*" + std::to_string(layers[i].count);
            return out;
        }

        const char* Bool(bool b) {
            return b ? "true" : "false";
        }
    }

    uint64_t ServerConfig::SeedFromText(const std::string& text) {
        const std::string t = str::Trim(text);
        int64_t n = 0;
        if (ParseInt(t, n))
            return static_cast<uint64_t>(n);
        // java's String.hashCode over utf-16 code units
        int32_t h = 0;
        const auto feed = [&h](uint32_t unit) {
            h = static_cast<int32_t>(static_cast<uint32_t>(h) * 31u + unit);
        };
        for (size_t i = 0; i < t.size();) {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            uint32_t cp = c;
            size_t len = 1;
            if (c >= 0xF0 && i + 3 < t.size()) {
                cp = c & 0x07;
                len = 4;
            } else if (c >= 0xE0 && i + 2 < t.size()) {
                cp = c & 0x0F;
                len = 3;
            } else if (c >= 0xC0 && i + 1 < t.size()) {
                cp = c & 0x1F;
                len = 2;
            }
            for (size_t k = 1; k < len; k++)
                cp = (cp << 6) | (static_cast<unsigned char>(t[i + k]) & 0x3F);
            i += len;
            if (cp >= 0x10000) {
                cp -= 0x10000;
                feed(0xD800 + (cp >> 10));
                feed(0xDC00 + (cp & 0x3FF));
            } else {
                feed(cp);
            }
        }
        return static_cast<uint64_t>(static_cast<int64_t>(h));
    }

    bool ServerConfig::SetProperty(const std::string& key, const std::string& rawValue,
                                   std::string* warning) {
        const std::string v = str::Trim(rawValue);
        const std::string lower = LowerText(v);
        bool b = false;
        int64_t n = 0;
        const auto bad = [&](const char* expected) {
            if (warning)
                *warning = key + "=" + v + ": expected " + expected + ", keeping the current value";
            return true;
        };
        const auto boolean = [&](bool& field) {
            if (ParseBool(v, b))
                field = b;
            else
                bad("true or false");
            return true;
        };
        const auto number = [&](auto& field, int64_t lo, int64_t hi) {
            if (ParseInt(v, n))
                field = static_cast<std::remove_reference_t<decltype(field)>>(std::clamp(n, lo, hi));
            else
                bad("a whole number");
            return true;
        };
        if (key == "server-name") {
            name = v;
            motd = v;
            return true;
        }
        if (key == "gamemode") {
            if (lower == "survival" || lower == "0")
                gameMode = 0;
            else if (lower == "creative" || lower == "1")
                gameMode = 1;
            else if (lower == "adventure" || lower == "2")
                gameMode = 2;
            else if (lower == "spectator" || lower == "6")
                gameMode = 6;
            else
                bad("survival, creative, adventure or spectator");
            return true;
        }
        if (key == "force-gamemode")
            return boolean(forceGamemode);
        if (key == "difficulty") {
            if (lower == "peaceful" || lower == "0")
                difficulty = 0;
            else if (lower == "easy" || lower == "1")
                difficulty = 1;
            else if (lower == "normal" || lower == "2")
                difficulty = 2;
            else if (lower == "hard" || lower == "3")
                difficulty = 3;
            else
                bad("peaceful, easy, normal or hard");
            return true;
        }
        if (key == "allow-cheats")
            return boolean(allowCheats);
        if (key == "max-players")
            return number(maxPlayers, 1, 1000);
        if (key == "online-mode")
            return boolean(xboxAuth);
        if (key == "allow-list")
            return boolean(whitelist);
        if (key == "server-port")
            return number(port, 1, 65535);
        if (key == "server-portv6")
            return number(portV6, 1, 65535);
        if (key == "server-ip") {
            bindAddress = v.empty() ? "0.0.0.0" : v;
            return true;
        }
        if (key == "server-udp-ports") {
            ServerConfig trial = *this;
            if (ParseUdpPorts(v, trial))
                *this = std::move(trial);
            else
                bad("a port, a first-last range, or public:local (see the bds how-to)");
            return true;
        }
        if (key == "transport") {
            if (lower == "nethernet" || lower == "raknet")
                transport = lower;
            else
                bad("nethernet or raknet");
            return true;
        }
        if (key == "enable-lan-visibility")
            return boolean(lanDiscovery);
        if (key == "view-distance")
            return number(viewDistance, 2, 64);
        if (key == "tick-distance")
            return number(tickDistance, 4, 12);
        if (key == "player-idle-timeout")
            return number(idleTimeoutMinutes, 0, 1000000);
        if (key == "max-threads")
            return number(maxThreads, 0, 256);
        if (key == "level-name") {
            levelName = v.empty() ? "Bedrock level" : v;
            return true;
        }
        if (key == "level-seed") {
            levelSeedText = v;
            randomSeed = v.empty();
            if (!randomSeed)
                seed = SeedFromText(v);
            return true;
        }
        if (key == "default-player-permission-level") {
            if (lower == "visitor" || lower == "member" || lower == "operator")
                defaultPermission = lower;
            else
                bad("visitor, member or operator");
            return true;
        }
        if (key == "compression-threshold")
            return number(compressionThreshold, 0, 65535);
        if (key == "compression-algorithm") {
            if (lower == "zlib" || lower == "snappy")
                compressionAlgorithm = lower;
            else
                bad("zlib or snappy");
            return true;
        }
        if (key == "client-side-chunk-generation-enabled")
            return boolean(clientSideChunkGeneration);
        if (key == "block-network-ids-are-hashes")
            return boolean(blockNetworkIdsAreHashes);
        if (key == "disable-persona")
            return boolean(disablePersona);
        if (key == "disable-custom-skins")
            return boolean(disableCustomSkins);
        if (key == "enable-packet-rate-limiter")
            return boolean(packetRateLimiter);
        if (key == "chat-restriction") {
            if (lower == "none")
                chatRestriction = "None";
            else if (lower == "dropped")
                chatRestriction = "Dropped";
            else if (lower == "disabled")
                chatRestriction = "Disabled";
            else
                bad("None, Dropped or Disabled");
            return true;
        }
        // this server's own keys
        if (key == "perlin-generator") {
            generator = lower;
            return true;
        }
        if (key == "perlin-terrain-backend") {
            terrainBackend = lower;
            return true;
        }
        if (key == "perlin-plugin-dir") {
            pluginDir = v;
            return true;
        }
        if (key == "perlin-block-data-dir") {
            blockDataDir = v;
            return true;
        }
        if (key == "perlin-ice-servers") {
            iceServers = Split(v, ',');
            return true;
        }
        if (key == "perlin-ice-bind-address") {
            iceBindAddress = v;
            return true;
        }
        if (key == "perlin-auth-root-keys") {
            authRootKeys = Split(v, ',');
            return true;
        }
        if (key == "perlin-flat-layers") {
            std::vector<FlatLayer> parsed;
            for (const std::string& item : Split(v, ',')) {
                FlatLayer layer;
                const size_t star = item.find('*');
                layer.block = str::Trim(item.substr(0, star));
                int64_t count = 1;
                if (star != std::string::npos && !ParseInt(str::Trim(item.substr(star + 1)), count))
                    count = 1;
                layer.count = static_cast<int>(std::clamp<int64_t>(count, 1, 384));
                if (!layer.block.empty())
                    parsed.push_back(std::move(layer));
            }
            if (!parsed.empty())
                flatLayers = std::move(parsed);
            return true;
        }
        // a bds key with no behaviour here, or something unknown: keep it as written
        for (auto& [k, val] : extraProperties)
            if (k == key) {
                val = v;
                return false;
            }
        extraProperties.emplace_back(key, v);
        return false;
    }

    ServerConfig ServerConfig::LoadProperties(const std::string& path, std::string* error) {
        ServerConfig c;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error)
                *error = path + ": cannot open";
            return c;
        }
        // defaults a file that omits a key falls back to
        c.randomSeed = true;
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
                line.erase(0, 3);
            first = false;
            line = str::Trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                LogWarn(path + ": ignored line without '=': " + line);
                continue;
            }
            std::string warning;
            c.SetProperty(str::Trim(line.substr(0, eq)), line.substr(eq + 1), &warning);
            if (!warning.empty())
                LogWarn(path + ": " + warning);
        }
        return c;
    }

    ServerConfig ServerConfig::LoadDirectory(const std::string& dir, std::string* error) {
        const std::filesystem::path props = std::filesystem::path(dir) / "server.properties";
        const std::filesystem::path legacy = std::filesystem::path(dir) / "server.json";
        std::error_code ec;
        if (std::filesystem::exists(props, ec))
            return LoadProperties(props.string(), error);
        if (std::filesystem::exists(legacy, ec))
            return Load(legacy.string(), error);
        // no config yet: a new world, so a new seed
        ServerConfig fresh;
        fresh.randomSeed = true;
        return fresh;
    }

    bool ServerConfig::SaveProperties(const std::string& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        const auto current = [&](const std::string& key, std::string& value) {
            if (key == "server-name")
                value = name;
            else if (key == "gamemode")
                value = GameModeName(gameMode);
            else if (key == "force-gamemode")
                value = Bool(forceGamemode);
            else if (key == "difficulty")
                value = DifficultyName(difficulty);
            else if (key == "allow-cheats")
                value = Bool(allowCheats);
            else if (key == "max-players")
                value = std::to_string(maxPlayers);
            else if (key == "online-mode")
                value = Bool(xboxAuth);
            else if (key == "allow-list")
                value = Bool(whitelist);
            else if (key == "server-port")
                value = std::to_string(port);
            else if (key == "server-portv6")
                value = std::to_string(portV6);
            else if (key == "server-ip")
                value = bindAddress == "0.0.0.0" ? "" : bindAddress;
            else if (key == "server-udp-ports")
                value = UdpPortsText(*this);
            else if (key == "transport")
                value = transport;
            else if (key == "enable-lan-visibility")
                value = Bool(lanDiscovery);
            else if (key == "view-distance")
                value = std::to_string(viewDistance);
            else if (key == "tick-distance")
                value = std::to_string(tickDistance);
            else if (key == "player-idle-timeout")
                value = std::to_string(idleTimeoutMinutes);
            else if (key == "max-threads")
                value = std::to_string(maxThreads);
            else if (key == "level-name")
                value = levelName;
            else if (key == "level-seed")
                value = randomSeed ? std::string()
                                   : (levelSeedText.empty() ? std::to_string(static_cast<int64_t>(seed))
                                                            : levelSeedText);
            else if (key == "default-player-permission-level")
                value = defaultPermission;
            else if (key == "compression-threshold")
                value = std::to_string(compressionThreshold);
            else if (key == "compression-algorithm")
                value = compressionAlgorithm;
            else if (key == "client-side-chunk-generation-enabled")
                value = Bool(clientSideChunkGeneration);
            else if (key == "block-network-ids-are-hashes")
                value = Bool(blockNetworkIdsAreHashes);
            else if (key == "disable-persona")
                value = Bool(disablePersona);
            else if (key == "disable-custom-skins")
                value = Bool(disableCustomSkins);
            else if (key == "enable-packet-rate-limiter")
                value = Bool(packetRateLimiter);
            else if (key == "chat-restriction")
                value = chatRestriction;
            else
                return false;
            return true;
        };
        std::vector<std::string> written;
        for (const BdsKey& k : kBdsKeys) {
            std::string value = k.fallback;
            if (!current(k.key, value))
                for (const auto& [ek, ev] : extraProperties)
                    if (ek == k.key)
                        value = ev;
            out << k.key << "=" << value << "\n\n";
            written.push_back(k.key);
        }
        // keys of ours and anything unknown that was read
        out << "# perlin additions: the terrain generator (flat or vanilla), gpu or cpu terrain, plugin and "
               "block data folders\n";
        out << "perlin-generator=" << generator << "\n";
        out << "perlin-terrain-backend=" << terrainBackend << "\n";
        out << "perlin-plugin-dir=" << pluginDir << "\n";
        out << "perlin-block-data-dir=" << blockDataDir << "\n";
        out << "perlin-flat-layers=" << FlatLayersText(flatLayers) << "\n";
        out << "perlin-ice-servers=" << JoinList(iceServers) << "\n";
        out << "perlin-ice-bind-address=" << iceBindAddress << "\n";
        out << "perlin-auth-root-keys=" << JoinList(authRootKeys) << "\n";
        for (const auto& [ek, ev] : extraProperties)
            if (std::find(written.begin(), written.end(), ek) == written.end())
                out << ek << "=" << ev << "\n";
        return out.good();
    }
}
