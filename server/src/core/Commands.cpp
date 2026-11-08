#include "core/Commands.hpp"

#include "core/Server.hpp"
#include "world/BlockCatalog.hpp"
#include "world/vanilla/BiomeData.hpp"

#include "protocol/Packets.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <set>
#include <sstream>

namespace perlin::server {
    namespace {
        namespace proto = perlin::proto2225;

        std::string StripNamespace(const std::string& s) {
            return s.starts_with("minecraft:") ? s.substr(10) : s;
        }

        struct Token {
            std::string text;

            size_t end = 0;
            bool quoted = false;
        };

        std::vector<Token> Tokenize(const std::string& line) {
            std::vector<Token> out;
            size_t i = 0;
            const size_t n = line.size();
            while (i < n) {
                while (i < n && line[i] == ' ')
                    i++;
                if (i >= n)
                    break;
                Token t;
                if (line[i] == '"') {
                    i++;
                    t.quoted = true;
                    while (i < n && line[i] != '"') {
                        if (line[i] == '\\' && i + 1 < n)
                            i++;
                        t.text += line[i++];
                    }
                    if (i < n)
                        i++;
                } else {
                    int depth = 0;
                    bool inQuote = false;
                    while (i < n && (depth > 0 || inQuote || line[i] != ' ')) {
                        const char c = line[i];
                        if (c == '"')
                            inQuote = !inQuote;
                        else if (!inQuote && (c == '[' || c == '{'))
                            depth++;
                        else if (!inQuote && (c == ']' || c == '}') && depth > 0)
                            depth--;
                        t.text += c;
                        i++;
                    }
                }
                t.end = i;
                out.push_back(std::move(t));
            }
            return out;
        }

        bool ParseInteger(const std::string& s, int64_t& out) {
            if (s.empty())
                return false;
            char* end = nullptr;
            const long long v = std::strtoll(s.c_str(), &end, 10);
            if (end == s.c_str() || *end != '\0')
                return false;
            out = v;
            return true;
        }

        bool ParseDouble(const std::string& s, double& out) {
            if (s.empty())
                return false;
            char* end = nullptr;
            const double v = std::strtod(s.c_str(), &end);
            if (end == s.c_str() || *end != '\0' || !std::isfinite(v))
                return false;
            out = v;
            return true;
        }

        constexpr double kEye = 1.62;

        struct Vec {
            double x = 0, y = 0, z = 0;
        };

        Vec FeetOf(const Session& s) {
            return {s.X(), s.Y() - kEye, s.Z()};
        }

        thread_local const CommandOrigin* t_origin = nullptr;

        Vec Origin(Server& server, Session* source) {
            if (t_origin)
                return {t_origin->x, t_origin->y + (t_origin->eyes ? kEye : 0.0), t_origin->z};
            if (source)
                return FeetOf(*source);
            return {server.SpawnX() + 0.5, server.SpawnY(), server.SpawnZ() + 0.5};
        }

        bool ParseRange(const std::string& s, double& lo, double& hi) {
            lo = -1e300;
            hi = 1e300;
            const size_t dots = s.find("..");
            if (dots == std::string::npos) {
                double v;
                if (!ParseDouble(s, v))
                    return false;
                lo = hi = v;
                return true;
            }
            const std::string a = s.substr(0, dots), b = s.substr(dots + 2);
            if (!a.empty() && !ParseDouble(a, lo))
                return false;
            if (!b.empty() && !ParseDouble(b, hi))
                return false;
            return true;
        }

        const char* kSelectorOptions = "name, distance, x, y, z, limit, sort, gamemode, type";
    }

    const CommandOrigin* ActiveOrigin() { return t_origin; }

    OriginScope::OriginScope(const CommandOrigin& origin) : previous(t_origin), current(origin) { t_origin = &current; }

    OriginScope::~OriginScope() { t_origin = previous; }

    int CommandDimension(Session* source) {
        if (t_origin)
            return t_origin->dimension;
        return source ? source->DimensionId() : 0;
    }

    bool ResolveSelector(Server& server, Session* source, const std::string& text, std::vector<Session*>& out,
                         std::string& error) {
        out.clear();
        if (text.size() < 2 || text[0] != '@') {
            error = "Invalid selector";
            return false;
        }
        const char kind = static_cast<char>(std::tolower(static_cast<unsigned char>(text[1])));
        if (std::string("spare").find(kind) == std::string::npos || (text.size() > 2 && text[2] != '[')) {
            error =
                "Unknown selector type '" + text.substr(0, text.size() > 2 && text[2] != '[' ? 3 : 2) + "'";
            return false;
        }

        std::vector<std::pair<std::string, std::string>> options;
        if (text.size() > 2) {
            if (text.back() != ']') {
                error = "Expected end of options";
                return false;
            }
            const std::string inner = text.substr(3, text.size() - 4);
            size_t i = 0;
            while (i < inner.size()) {
                while (i < inner.size() && (inner[i] == ' ' || inner[i] == ','))
                    i++;
                if (i >= inner.size())
                    break;
                const size_t eq = inner.find('=', i);
                if (eq == std::string::npos) {
                    error = "Expected value for option '" + inner.substr(i) + "'";
                    return false;
                }
                std::string key = str::Lower(inner.substr(i, eq - i));
                while (!key.empty() && key.back() == ' ')
                    key.pop_back();
                size_t j = eq + 1;
                while (j < inner.size() && inner[j] == ' ')
                    j++;
                std::string value;
                if (j < inner.size() && inner[j] == '"') {
                    j++;
                    while (j < inner.size() && inner[j] != '"')
                        value += inner[j++];
                    if (j < inner.size())
                        j++;
                } else {
                    while (j < inner.size() && inner[j] != ',')
                        value += inner[j++];
                    while (!value.empty() && value.back() == ' ')
                        value.pop_back();
                }
                options.emplace_back(key, value);
                i = j;
            }
        }

        Vec origin = Origin(server, source);
        bool nameNegate = false;
        std::string nameFilter;
        bool haveName = false;
        double distLo = -1e300, distHi = 1e300;
        bool haveDistance = false;
        int limit = (kind == 'p' || kind == 'r' || kind == 's') ? 1 : 1 << 30;
        std::string sort = kind == 'p' ? "nearest" : kind == 'r' ? "random" : "arbitrary";
        std::vector<std::pair<bool, int>> modes;
        bool typeOk = true;
        bool positional = kind == 'p';
        for (const auto& [key, value] : options) {
            if (key == "distance" || key == "x" || key == "y" || key == "z")
                positional = true;
            if (key == "name") {
                haveName = true;
                nameNegate = !value.empty() && value[0] == '!';
                nameFilter = nameNegate ? value.substr(1) : value;
            } else if (key == "distance") {
                if (!ParseRange(value, distLo, distHi)) {
                    error = "Expected a distance range";
                    return false;
                }
                haveDistance = true;
            } else if (key == "x" || key == "y" || key == "z") {
                double v;
                if (!ParseDouble(value, v)) {
                    error = "Expected a coordinate for option '" + key + "'";
                    return false;
                }
                (key == "x" ? origin.x : key == "y" ? origin.y : origin.z) = v;
            } else if (key == "limit" || key == "c") {
                int64_t v;
                if (!ParseInteger(value, v) || v < 1) {
                    error = "Limit must be at least 1";
                    return false;
                }
                limit = static_cast<int>(std::min<int64_t>(v, 1 << 30));
            } else if (key == "sort") {
                const std::string v = str::Lower(value);
                if (v != "nearest" && v != "furthest" && v != "random" && v != "arbitrary") {
                    error = "Invalid or unknown sort type '" + value + "'";
                    return false;
                }
                sort = v;
            } else if (key == "gamemode" || key == "m") {
                bool neg = !value.empty() && value[0] == '!';
                const std::string v = str::Lower(neg ? value.substr(1) : value);
                int mode = -1;
                if (v == "survival" || v == "s" || v == "0")
                    mode = 0;
                else if (v == "creative" || v == "c" || v == "1")
                    mode = 1;
                else if (v == "adventure" || v == "a" || v == "2")
                    mode = 2;
                else if (v == "spectator" || v == "sp" || v == "6")
                    mode = 6;
                if (mode < 0) {
                    error = "Invalid or unknown game mode '" + value + "'";
                    return false;
                }
                modes.emplace_back(neg, mode);
            } else if (key == "type") {
                const std::string v = StripNamespace(str::Lower(value));
                const bool neg = !v.empty() && v[0] == '!';
                const std::string t = neg ? v.substr(1) : v;

                typeOk = typeOk && ((t == "player") != neg);
            } else {
                error = "Unknown option '" + key + "' (supported: " + kSelectorOptions + ")";
                return false;
            }
        }

        std::vector<Session*> candidates;
        if (kind == 's') {
            if (source && source->InGame())
                candidates.push_back(source);
        } else {
            candidates = server.InGamePlayers();
        }
        if (!typeOk)
            candidates.clear();
        if (positional && kind != 's') {
            const int dim = CommandDimension(source);
            std::erase_if(candidates, [dim](Session* s) { return s->DimensionId() != dim; });
        }
        std::vector<std::pair<double, Session*>> kept;
        for (Session* s : candidates) {
            if (haveName && ((s->Name() == nameFilter) == nameNegate))
                continue;
            bool modeOk = true;
            for (const auto& [neg, mode] : modes)
                if ((s->GameMode() == mode) == neg)
                    modeOk = false;
            if (!modeOk)
                continue;
            const Vec f = FeetOf(*s);
            const double d =
                std::sqrt((f.x - origin.x) * (f.x - origin.x) + (f.y - origin.y) * (f.y - origin.y) +
                          (f.z - origin.z) * (f.z - origin.z));
            if (haveDistance && (d < distLo || d > distHi))
                continue;
            kept.emplace_back(d, s);
        }
        if (sort == "nearest")
            std::stable_sort(kept.begin(), kept.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
        else if (sort == "furthest")
            std::stable_sort(kept.begin(), kept.end(),
                             [](const auto& a, const auto& b) { return a.first > b.first; });
        else if (sort == "random") {
            static std::mt19937 rng{std::random_device{}()};
            std::shuffle(kept.begin(), kept.end(), rng);
        }
        for (size_t i = 0; i < kept.size() && static_cast<int>(out.size()) < limit; i++)
            out.push_back(kept[i].second);
        return true;
    }

    bool ParseOverload(const std::string& notation, OverloadSpec& out, std::string* error) {
        out.clear();
        std::istringstream in(notation);
        std::string word;
        bool optionalSeen = false;
        while (in >> word) {
            ParamSpec p;
            const bool required = word.front() == '<';
            const bool optional = word.front() == '[';
            if (!required && !optional) {
                p.name = word;
                p.kind = ArgKind::Literal;
                p.values = {str::Lower(word)};
                if (optionalSeen) {
                    if (error)
                        *error = "a keyword after an optional argument: " + word;
                    return false;
                }
                out.push_back(std::move(p));
                continue;
            }
            if (word.size() < 5 || word.back() != (required ? '>' : ']')) {
                if (error)
                    *error = "malformed argument " + word;
                return false;
            }
            const std::string inner = word.substr(1, word.size() - 2);
            const size_t colon = inner.find(':');
            if (colon == std::string::npos) {
                if (error)
                    *error = "argument without a type: " + word;
                return false;
            }
            p.name = inner.substr(0, colon);
            const std::string type = inner.substr(colon + 1);
            p.optional = optional;
            if (required && optionalSeen) {
                if (error)
                    *error = "a required argument after an optional one: " + word;
                return false;
            }
            optionalSeen = optionalSeen || optional;
            if (type == "int")
                p.kind = ArgKind::Int;
            else if (type == "float")
                p.kind = ArgKind::Float;
            else if (type == "string")
                p.kind = ArgKind::String;
            else if (type == "text")
                p.kind = ArgKind::Text;
            else if (type == "targets")
                p.kind = ArgKind::Targets;
            else if (type == "target")
                p.kind = ArgKind::Target;
            else if (type == "profiles")
                p.kind = ArgKind::Profiles;
            else if (type == "pos")
                p.kind = ArgKind::Pos;
            else if (type == "blockpos")
                p.kind = ArgKind::BlockPos;
            else if (type == "item")
                p.kind = ArgKind::Item;
            else if (type == "block")
                p.kind = ArgKind::Block;
            else if (type == "biome")
                p.kind = ArgKind::Biome;
            else if (type.starts_with("enum(") && type.back() == ')') {
                p.kind = ArgKind::Enum;
                std::string list = type.substr(5, type.size() - 6);
                size_t from = 0;
                while (from <= list.size()) {
                    size_t bar = list.find('|', from);
                    if (bar == std::string::npos)
                        bar = list.size();
                    if (bar > from)
                        p.values.push_back(str::Lower(list.substr(from, bar - from)));
                    from = bar + 1;
                }
                if (p.values.empty()) {
                    if (error)
                        *error = "empty enum in " + word;
                    return false;
                }
            } else {
                if (error)
                    *error = "unknown type '" + type + "' in " + word;
                return false;
            }
            out.push_back(std::move(p));
        }
        return true;
    }

    std::string UsageLine(const std::string& name, const OverloadSpec& overload) {
        std::string line = "/" + name;
        for (const ParamSpec& p : overload) {
            line += " ";
            if (p.kind == ArgKind::Literal) {
                line += p.name;
                continue;
            }
            std::string shown;
            if (p.kind == ArgKind::Enum) {
                for (size_t i = 0; i < p.values.size(); i++)
                    shown += (i ? "|" : "") + p.values[i];
            } else {
                shown = p.name;
            }
            line += p.optional ? "[<" + shown + ">]" : "<" + shown + ">";
        }
        return line;
    }

    namespace {
        struct Step {

            int status = 0;
            std::string error;

            size_t reached = 0;
        };

        struct Coord {
            bool relative = false, local = false;
            double value = 0;
            bool hasNumber = false;
        };

        bool ParseCoord(const std::string& t, Coord& c) {
            if (t.empty())
                return false;
            size_t i = 0;
            if (t[0] == '~') {
                c.relative = true;
                i = 1;
            } else if (t[0] == '^') {
                c.local = true;
                i = 1;
            }
            if (i == t.size()) {
                c.hasNumber = false;
                return c.relative || c.local;
            }
            c.hasNumber = true;
            return ParseDouble(t.substr(i), c.value);
        }

        bool Resolve(Server& server, Session* source, const std::vector<Token>& tokens, size_t at, bool block,
                     std::array<double, 3>& out, std::string& error) {
            Coord c[3];
            for (int k = 0; k < 3; k++) {
                if (!ParseCoord(tokens[at + static_cast<size_t>(k)].text, c[k])) {
                    error = "Expected a coordinate";
                    return false;
                }
            }
            const bool anyLocal = c[0].local || c[1].local || c[2].local;
            if (anyLocal && !(c[0].local && c[1].local && c[2].local)) {
                error = "Cannot mix world & local coordinates (everything must either use ^ or not)";
                return false;
            }
            const Vec base = Origin(server, source);
            if (anyLocal) {
                constexpr double kPi = 3.14159265358979323846;
                const double yaw = (t_origin ? t_origin->yaw : source ? source->Yaw() : 0.0) * kPi / 180.0,
                             pitch = (t_origin ? t_origin->pitch : source ? source->Pitch() : 0.0) * kPi / 180.0;

                const double fx = -std::sin(yaw) * std::cos(pitch), fy = -std::sin(pitch),
                             fz = std::cos(yaw) * std::cos(pitch);
                const double lx = std::cos(yaw), lz = std::sin(yaw);
                const double ux = fy * lz, uy = fz * lx - fx * lz, uz = -fy * lx;
                const double left = c[0].value, up = c[1].value, forward = c[2].value;
                out = {base.x + left * lx + up * ux + forward * fx, base.y + up * uy + forward * fy,
                       base.z + left * lz + up * uz + forward * fz};
                return true;
            }
            const double origin[3] = {base.x, base.y, base.z};
            for (int k = 0; k < 3; k++) {
                if (c[k].relative) {
                    out[static_cast<size_t>(k)] = origin[k] + (c[k].hasNumber ? c[k].value : 0.0);
                } else {
                    double v = c[k].value;

                    const std::string& text = tokens[at + static_cast<size_t>(k)].text;
                    if (!block && k != 1 && !text.contains('.'))
                        v += 0.5;
                    out[static_cast<size_t>(k)] = v;
                }
                if (block)
                    out[static_cast<size_t>(k)] = std::floor(out[static_cast<size_t>(k)]);
            }
            return true;
        }

        Step ParseArg(Server& server, Session* source, const ParamSpec& p, const std::vector<Token>& tokens,
                      size_t& at, ArgValue& v) {
            Step st;
            if (p.kind == ArgKind::Text) {
                if (at >= tokens.size()) {
                    st.status = 2;
                    st.reached = at;
                    return st;
                }

                std::string text;
                for (size_t i = at; i < tokens.size(); i++)
                    text += (i > at ? " " : "") + tokens[i].text;
                v.text = text;
                at = tokens.size();
                v.present = true;
                return st;
            }
            if (at >= tokens.size()) {
                st.status = 2;
                st.reached = at;
                return st;
            }
            const Token& t = tokens[at];
            st.reached = at + 1;
            switch (p.kind) {
            case ArgKind::Literal:
            case ArgKind::Enum: {
                const std::string word = str::Lower(t.text);
                if (std::find(p.values.begin(), p.values.end(), word) == p.values.end()) {
                    st.status = 1;
                    st.error = "Incorrect argument for command";
                    return st;
                }
                v.text = word;
                break;
            }
            case ArgKind::Int: {
                if (!ParseInteger(t.text, v.integer)) {
                    st.status = 1;
                    st.error = "Expected integer";
                    return st;
                }
                v.number = static_cast<double>(v.integer);
                break;
            }
            case ArgKind::Float: {
                if (!ParseDouble(t.text, v.number)) {
                    st.status = 1;
                    st.error = "Expected float";
                    return st;
                }
                v.integer = static_cast<int64_t>(v.number);
                break;
            }
            case ArgKind::String:
                v.text = t.text;
                break;
            case ArgKind::Targets:
            case ArgKind::Target: {
                std::string error;
                if (!t.text.empty() && t.text[0] == '@') {
                    if (!ResolveSelector(server, source, t.text, v.players, error)) {
                        st.status = 1;
                        st.error = error;
                        return st;
                    }
                } else {
                    v.players.clear();
                    if (Session* s = server.FindByName(t.text); s && s->InGame())
                        v.players.push_back(s);
                }

                if (p.kind == ArgKind::Target && v.players.size() > 1) {
                    st.status = 1;
                    st.error = "Only one entity is allowed, but the provided selector allows more than one";
                    return st;
                }
                v.text = t.text;
                break;
            }
            case ArgKind::Profiles: {
                v.names.clear();
                if (!t.text.empty() && t.text[0] == '@') {
                    std::string error;
                    std::vector<Session*> list;
                    if (!ResolveSelector(server, source, t.text, list, error)) {
                        st.status = 1;
                        st.error = error;
                        return st;
                    }
                    for (Session* s : list)
                        v.names.push_back(s->Name());
                    if (v.names.empty()) {
                        st.status = 1;
                        st.error = "No player was found";
                        return st;
                    }
                } else {
                    v.names.push_back(t.text);
                }
                v.text = t.text;
                break;
            }
            case ArgKind::Pos:
            case ArgKind::BlockPos: {
                if (at + 3 > tokens.size()) {
                    st.status = 2;
                    st.reached = tokens.size();
                    return st;
                }
                std::string error;
                if (!Resolve(server, source, tokens, at, p.kind == ArgKind::BlockPos, v.pos, error)) {
                    st.status = 1;
                    st.error = error;
                    st.reached = at + 3;
                    return st;
                }
                at += 2;
                st.reached = at + 1;
                break;
            }
            case ArgKind::Item:
            case ArgKind::Block: {
                std::string name = t.text;
                const size_t cut = name.find_first_of("[{");
                if (cut != std::string::npos)
                    name.resize(cut);
                const std::string full = "minecraft:" + StripNamespace(str::Lower(name));
                if (p.kind == ArgKind::Block && (full == "minecraft:air" || full == "minecraft:cave_air" ||
                                                 full == "minecraft:void_air")) {
                    v.text = "minecraft:air";
                    v.index = 0;
                    v.block = 0;
                    break;
                }
                const CatalogItem* item = BlockCatalog::Instance().ItemByName(full);
                if (p.kind == ArgKind::Block && (!item || !item->block) && BlockCatalog::Instance().Knows(full)) {
                    v.text = full;
                    v.index = -1;
                    std::string states = cut != std::string::npos && t.text[cut] == '[' ? t.text.substr(cut) : std::string();
                    std::erase(states, '"');
                    v.block = BlockCatalog::Instance().Resolve(full + states);
                    break;
                }
                if (!item || (p.kind == ArgKind::Block && !item->block)) {
                    st.status = 1;
                    st.error =
                        std::string(p.kind == ArgKind::Block ? "Unknown block type '" : "Unknown item '") +
                        full + "'";
                    return st;
                }
                v.text = full;
                v.index = item->networkId;
                if (p.kind == ArgKind::Block) {
                    std::string states = cut != std::string::npos && t.text[cut] == '[' ? t.text.substr(cut) : std::string();
                    std::erase(states, '"');
                    v.block = states.empty() ? BlockCatalog::Instance().RegistryId(item->blockHash)
                                             : BlockCatalog::Instance().Resolve(full + states);
                }
                break;
            }
            case ArgKind::Biome: {
                const std::string name = StripNamespace(str::Lower(t.text));
                int found = -1;
                for (int i = 0; i < vanilla::kBiomeCount && found < 0; i++)
                    if (name == vanilla::kBiomes[i].bedrock || name == vanilla::kBiomes[i].java)
                        found = i;
                if (found < 0) {
                    st.status = 1;
                    st.error = "Unknown biome '" + t.text + "'";
                    return st;
                }
                v.index = found;
                v.text = std::string("minecraft:") + vanilla::kBiomes[found].bedrock;
                break;
            }
            case ArgKind::Text:
                break;
            }
            v.present = true;
            at++;
            return st;
        }
    }

    MatchResult MatchCommand(Server& server, Session* source, const std::string& name,
                             const std::vector<OverloadSpec>& overloads, const std::string& args) {
        MatchResult result;
        const std::vector<Token> tokens = Tokenize(args);

        const auto contextUpTo = [&](size_t upTo) {
            std::string c = "/" + name;
            if (upTo > 0 && !tokens.empty())
                c += " " + args.substr(0, tokens[std::min(upTo, tokens.size()) - 1].end);
            return c + "<--[HERE]";
        };

        size_t bestReach = 0;
        bool haveFailure = false;
        std::string bestError;
        size_t bestContext = 0;
        for (size_t o = 0; o < overloads.size(); o++) {
            std::map<std::string, ArgValue> values;
            size_t at = 0;
            bool failed = false;
            size_t reach = 0;
            std::string error;
            size_t contextAt = 0;
            for (const ParamSpec& p : overloads[o]) {
                ArgValue v;
                const Step st = ParseArg(server, source, p, tokens, at, v);
                if (st.status == 2) {
                    if (p.optional)
                        break;
                    failed = true;
                    reach = at;
                    error = "Unknown or incomplete command, see below for error";
                    contextAt = at;
                    break;
                }
                if (st.status == 1) {
                    if (p.optional) {

                        failed = true;
                        reach = st.reached;
                        error = st.error;
                        contextAt = st.reached;
                        break;
                    }
                    failed = true;
                    reach = st.reached;
                    error = st.error;
                    contextAt = st.reached;
                    break;
                }
                values[p.name] = std::move(v);
            }
            if (!failed && at < tokens.size()) {
                failed = true;
                reach = at + 1;
                error = "Incorrect argument for command";
                contextAt = at + 1;
            }
            if (!failed) {

                for (const ParamSpec& p : overloads[o]) {
                    const auto found = values.find(p.name);
                    if ((p.kind == ArgKind::Targets || p.kind == ArgKind::Target) && found != values.end() &&
                        found->second.present && found->second.players.empty()) {
                        result.ok = false;
                        result.error = found->second.text.starts_with("@e") ? "No entity was found"
                                                                            : "No player was found";
                        return result;
                    }
                }
                result.ok = true;
                result.overload = static_cast<int>(o);
                result.values = std::move(values);
                return result;
            }
            if (!haveFailure || reach > bestReach) {
                haveFailure = true;
                bestReach = reach;
                bestError = error;
                bestContext = contextAt;
            }
        }
        result.ok = false;
        result.error = bestError.empty() ? "Unknown or incomplete command, see below for error" : bestError;
        result.context = contextUpTo(bestContext);
        return result;
    }

    void FillAvailableCommands(proto::AvailableCommandsPacket& out,
                               const std::map<std::string, CommandDef>& commands, bool isOperator) {
        using VT = proto::AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryValueType;
        using ET = proto::AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryEnumType;
        std::map<std::string, uint32_t> valueIndex;
        std::map<std::string, uint32_t> enumIndex;
        const auto valueOf = [&](const std::string& v) {
            const auto it = valueIndex.find(v);
            if (it != valueIndex.end())
                return it->second;
            const uint32_t at = static_cast<uint32_t>(out.enumValues.size());
            out.enumValues.push_back(v);
            valueIndex[v] = at;
            return at;
        };
        const auto enumOf = [&](const std::string& label, const std::vector<std::string>& values) {
            const auto it = enumIndex.find(label);
            if (it != enumIndex.end())
                return it->second;
            proto::AvailableCommandsPacketEnumsEntry e;
            e.name = label;
            for (const std::string& v : values)
                e.values.push_back(valueOf(v));
            const uint32_t at = static_cast<uint32_t>(out.enums.size());
            out.enums.push_back(std::move(e));
            enumIndex[label] = at;
            return at;
        };

        int biomeSoft = -1;
        const auto biomes = [&]() {
            if (biomeSoft < 0) {
                proto::AvailableCommandsPacketDynamicEnumsEntry d;
                d.name = "Biome";
                for (int i = 0; i < vanilla::kBiomeCount; i++)
                    d.values.push_back(std::string("minecraft:") + vanilla::kBiomes[i].bedrock);
                biomeSoft = static_cast<int>(out.dynamicEnums.size());
                out.dynamicEnums.push_back(std::move(d));
            }
            return biomeSoft;
        };

        for (const auto& [cmdName, def] : commands) {
            if (def.operatorOnly && !isOperator)
                continue;
            proto::AvailableCommandsPacketCommandDataEntry e;
            e.name = cmdName;
            e.description = def.description;
            e.permissionLevel = "any";
            e.alias = -1;
            if (def.overloads.empty()) {

                proto::AvailableCommandsPacketCommandDataEntryOverloadsEntry overload;
                proto::AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntry arg;
                arg.parameterName = "args";
                arg.valueType = VT::RawText;
                arg.enumType = ET::Valid;
                arg.optional = true;
                overload.parameters.push_back(arg);
                e.overloads.push_back(std::move(overload));
            }
            for (size_t o = 0; o < def.overloads.size(); o++) {
                proto::AvailableCommandsPacketCommandDataEntryOverloadsEntry overload;
                for (const ParamSpec& p : def.overloads[o]) {
                    proto::AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntry arg;
                    arg.parameterName = p.name;
                    arg.optional = p.optional;
                    arg.enumType = ET::Valid;
                    switch (p.kind) {
                    case ArgKind::Literal:
                    case ArgKind::Enum:
                        arg.enumType = ET::Enum;
                        arg.valueType = static_cast<VT>(
                            enumOf(cmdName + "_" + p.name + "_" + std::to_string(o), p.values));
                        break;
                    case ArgKind::Int:
                        arg.valueType = VT::Int;
                        break;
                    case ArgKind::Float:
                        arg.valueType = VT::Float;
                        break;
                    case ArgKind::String:
                    case ArgKind::Item:
                    case ArgKind::Block:
                        arg.valueType = VT::String;
                        break;
                    case ArgKind::Text:
                        arg.valueType = VT::RawText;
                        break;
                    case ArgKind::Targets:
                    case ArgKind::Target:
                    case ArgKind::Profiles:
                        arg.valueType = VT::Target;
                        break;
                    case ArgKind::Pos:
                        arg.valueType = VT::Position;
                        break;
                    case ArgKind::BlockPos:
                        arg.valueType = VT::BlockPosition;
                        break;
                    case ArgKind::Biome:
                        arg.enumType = ET::SoftEnum;
                        arg.valueType = static_cast<VT>(biomes());
                        break;
                    }
                    overload.parameters.push_back(std::move(arg));
                }
                e.overloads.push_back(std::move(overload));
            }
            out.commandData.push_back(std::move(e));
        }
    }
}
