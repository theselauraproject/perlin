#include "core/Commands.hpp"
#include "core/Log.hpp"
#include "core/Server.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include "util/Strings.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace perlin::server {
    namespace {
        constexpr double kEye = 1.62;
        constexpr double kPi = 3.14159265358979323846;

        struct ExecContext {
            Session* executor = nullptr;
            CommandOrigin origin;
        };

        std::vector<std::string> Tokenize(const std::string& text, std::vector<size_t>& starts) {
            std::vector<std::string> out;
            size_t i = 0;
            while (i < text.size()) {
                while (i < text.size() && text[i] == ' ')
                    i++;
                if (i >= text.size())
                    break;
                const size_t begin = i;
                int depth = 0;
                bool quoted = false;
                while (i < text.size()) {
                    const char c = text[i];
                    if (c == '"')
                        quoted = !quoted;
                    else if (!quoted && (c == '[' || c == '{'))
                        depth++;
                    else if (!quoted && (c == ']' || c == '}'))
                        depth--;
                    else if (c == ' ' && depth <= 0 && !quoted)
                        break;
                    i++;
                }
                starts.push_back(begin);
                out.push_back(text.substr(begin, i - begin));
            }
            return out;
        }

        bool Number(const std::string& s, double& out) {
            if (s.empty())
                return false;
            char* end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && *end == '\0' && std::isfinite(out);
        }

        bool Coordinates(const std::string* t, const CommandOrigin& o, bool center, double out[3], std::string& error) {
            const bool local = !t[0].empty() && t[0][0] == '^';
            double v[3] = {0, 0, 0};
            bool relative[3] = {false, false, false};
            for (int k = 0; k < 3; k++) {
                const std::string& s = t[k];
                if (s.empty()) {
                    error = "Expected a coordinate";
                    return false;
                }
                if ((s[0] == '^') != local) {
                    error = "Cannot mix world & local coordinates (everything must either use ^ or not)";
                    return false;
                }
                std::string body = s;
                if (s[0] == '~' || s[0] == '^') {
                    relative[k] = true;
                    body = s.substr(1);
                }
                if (body.empty())
                    v[k] = 0;
                else if (!Number(body, v[k])) {
                    error = "Expected a coordinate, found '" + s + "'";
                    return false;
                }
                if (!relative[k] && center && k != 1 && body.find('.') == std::string::npos)
                    v[k] += 0.5;
            }
            const double baseY = o.y + (o.eyes ? kEye : 0.0);
            if (local) {
                const double yaw = o.yaw * kPi / 180.0, pitch = o.pitch * kPi / 180.0;
                const double fx = -std::sin(yaw) * std::cos(pitch), fy = -std::sin(pitch), fz = std::cos(yaw) * std::cos(pitch);
                const double lx = std::cos(yaw), lz = std::sin(yaw);
                const double ux = fy * lz, uy = fz * lx - fx * lz, uz = -fy * lx;
                out[0] = o.x + v[0] * lx + v[1] * ux + v[2] * fx;
                out[1] = baseY + v[1] * uy + v[2] * fy;
                out[2] = o.z + v[0] * lz + v[1] * uz + v[2] * fz;
                return true;
            }
            const double base[3] = {o.x, baseY, o.z};
            for (int k = 0; k < 3; k++)
                out[k] = relative[k] ? base[k] + v[k] : v[k];
            return true;
        }

        int DimensionNamed(std::string name) {
            name = str::Lower(name);
            if (name.starts_with("minecraft:"))
                name = name.substr(10);
            if (name == "overworld")
                return kOverworld;
            if (name == "the_nether" || name == "nether")
                return kNether;
            if (name == "the_end" || name == "end")
                return kEnd;
            return -1;
        }

        std::string BareBlock(std::string name) {
            name = str::Lower(name);
            if (const size_t open = name.find('['); open != std::string::npos)
                name = name.substr(0, open);
            return name.contains(':') ? name : "minecraft:" + name;
        }
    }

    int Server::RunExecute(Session* from, const std::string& args) {
        const auto fail = [from](const std::string& m) {
            if (from)
                from->SendRaw("§c" + m);
            else
                LogInfo(m);
        };
        const auto say = [from](const std::string& m) {
            if (from)
                from->SendRaw(m);
            else
                LogInfo(m);
        };
        std::vector<size_t> starts;
        const std::vector<std::string> t = Tokenize(args, starts);
        ExecContext initial;
        initial.executor = from;
        if (const CommandOrigin* active = ActiveOrigin()) {
            initial.origin = *active;
        } else if (from) {
            initial.origin = {from->X(), from->Y() - kEye, from->Z(), from->Yaw(), from->Pitch(), from->DimensionId(), false};
        } else {
            initial.origin = {SpawnX() + 0.5, SpawnY(), SpawnZ() + 0.5, 0.0f, 0.0f, kOverworld, false};
        }
        std::vector<ExecContext> contexts{initial};
        const auto select = [this](const ExecContext& c, const std::string& selector, std::vector<Session*>& out,
                                   std::string& error) {
            OriginScope scope(c.origin);
            if (!selector.empty() && selector[0] == '@')
                return ResolveSelector(*this, c.executor, selector, out, error);
            out.clear();
            if (Session* s = FindByName(selector); s && s->InGame())
                out.push_back(s);
            if (out.empty())
                error = "No player was found";
            return !out.empty();
        };
        const auto need = [&](size_t i, size_t count, const char* what) {
            if (i + count <= t.size())
                return true;
            fail(std::string("Expected ") + what + " after '" + (i > 0 ? t[i - 1] : std::string("execute")) + "'");
            return false;
        };
        size_t i = 0;
        bool conditionOnly = false;
        while (i < t.size()) {
            const std::string sub = str::Lower(t[i]);
            i++;
            if (sub == "run") {
                if (i >= t.size()) {
                    fail("Expected a command after 'run'");
                    return 0;
                }
                const std::string command = args.substr(starts[i]);
                int ran = 0;
                const bool saved = executeElevated;
                executeElevated = saved || !from || from->Operator();
                for (const ExecContext& c : contexts) {
                    OriginScope scope(c.origin);
                    RunCommand(c.executor, command);
                    ran++;
                }
                executeElevated = saved;
                return ran;
            }
            std::vector<ExecContext> next;
            std::string error;
            if (sub == "as" || sub == "at") {
                if (!need(i, 1, "a target"))
                    return 0;
                for (const ExecContext& c : contexts) {
                    std::vector<Session*> found;
                    if (!select(c, t[i], found, error))
                        continue;
                    for (Session* s : found) {
                        ExecContext n = c;
                        if (sub == "as") {
                            n.executor = s;
                        } else {
                            n.origin.x = s->X();
                            n.origin.y = s->Y() - kEye;
                            n.origin.z = s->Z();
                            n.origin.yaw = s->Yaw();
                            n.origin.pitch = s->Pitch();
                            n.origin.dimension = s->DimensionId();
                        }
                        next.push_back(n);
                    }
                }
                i++;
            } else if (sub == "positioned") {
                if (i < t.size() && str::Lower(t[i]) == "as") {
                    if (!need(i + 1, 1, "a target"))
                        return 0;
                    for (const ExecContext& c : contexts) {
                        std::vector<Session*> found;
                        if (!select(c, t[i + 1], found, error))
                            continue;
                        for (Session* s : found) {
                            ExecContext n = c;
                            n.origin.x = s->X();
                            n.origin.y = s->Y() - kEye;
                            n.origin.z = s->Z();
                            next.push_back(n);
                        }
                    }
                    i += 2;
                } else {
                    if (!need(i, 3, "a position"))
                        return 0;
                    for (const ExecContext& c : contexts) {
                        double p[3];
                        if (!Coordinates(&t[i], c.origin, true, p, error)) {
                            fail(error);
                            return 0;
                        }
                        ExecContext n = c;
                        n.origin.x = p[0];
                        n.origin.y = p[1];
                        n.origin.z = p[2];
                        n.origin.eyes = false;
                        next.push_back(n);
                    }
                    i += 3;
                }
            } else if (sub == "rotated") {
                if (i < t.size() && str::Lower(t[i]) == "as") {
                    if (!need(i + 1, 1, "a target"))
                        return 0;
                    for (const ExecContext& c : contexts) {
                        std::vector<Session*> found;
                        if (!select(c, t[i + 1], found, error))
                            continue;
                        for (Session* s : found) {
                            ExecContext n = c;
                            n.origin.yaw = s->Yaw();
                            n.origin.pitch = s->Pitch();
                            next.push_back(n);
                        }
                    }
                    i += 2;
                } else {
                    if (!need(i, 2, "a rotation"))
                        return 0;
                    for (const ExecContext& c : contexts) {
                        ExecContext n = c;
                        for (int k = 0; k < 2; k++) {
                            const std::string& s = t[i + static_cast<size_t>(k)];
                            const bool rel = !s.empty() && s[0] == '~';
                            double v = 0;
                            if ((rel && s.size() > 1 && !Number(s.substr(1), v)) || (!rel && !Number(s, v))) {
                                fail("Expected a rotation, found '" + s + "'");
                                return 0;
                            }
                            float& field = k == 0 ? n.origin.yaw : n.origin.pitch;
                            field = static_cast<float>(rel ? field + v : v);
                        }
                        next.push_back(n);
                    }
                    i += 2;
                }
            } else if (sub == "facing") {
                const auto face = [](ExecContext n, double tx, double ty, double tz) {
                    const double dx = tx - n.origin.x, dy = ty - (n.origin.y + (n.origin.eyes ? kEye : 0.0)),
                                 dz = tz - n.origin.z;
                    const double flat = std::sqrt(dx * dx + dz * dz);
                    n.origin.yaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
                    n.origin.pitch = static_cast<float>(-std::atan2(dy, flat) * 180.0 / kPi);
                    return n;
                };
                if (i < t.size() && str::Lower(t[i]) == "entity") {
                    if (!need(i + 1, 1, "a target"))
                        return 0;
                    const bool eyes = i + 2 < t.size() && str::Lower(t[i + 2]) == "eyes";
                    const bool anchor = i + 2 < t.size() && (str::Lower(t[i + 2]) == "eyes" || str::Lower(t[i + 2]) == "feet");
                    for (const ExecContext& c : contexts) {
                        std::vector<Session*> found;
                        if (!select(c, t[i + 1], found, error))
                            continue;
                        for (Session* s : found)
                            next.push_back(face(c, s->X(), s->Y() - (eyes ? 0.0 : kEye), s->Z()));
                    }
                    i += anchor ? 3 : 2;
                } else {
                    if (!need(i, 3, "a position"))
                        return 0;
                    for (const ExecContext& c : contexts) {
                        double p[3];
                        if (!Coordinates(&t[i], c.origin, true, p, error)) {
                            fail(error);
                            return 0;
                        }
                        next.push_back(face(c, p[0], p[1], p[2]));
                    }
                    i += 3;
                }
            } else if (sub == "anchored") {
                if (!need(i, 1, "eyes or feet"))
                    return 0;
                const std::string which = str::Lower(t[i]);
                if (which != "eyes" && which != "feet") {
                    fail("Invalid entity anchor '" + t[i] + "'");
                    return 0;
                }
                for (ExecContext c : contexts) {
                    c.origin.eyes = which == "eyes";
                    next.push_back(c);
                }
                i++;
            } else if (sub == "align") {
                if (!need(i, 1, "axes"))
                    return 0;
                const std::string axes = str::Lower(t[i]);
                for (ExecContext c : contexts) {
                    if (axes.contains('x'))
                        c.origin.x = std::floor(c.origin.x);
                    if (axes.contains('y'))
                        c.origin.y = std::floor(c.origin.y);
                    if (axes.contains('z'))
                        c.origin.z = std::floor(c.origin.z);
                    next.push_back(c);
                }
                i++;
            } else if (sub == "in") {
                if (!need(i, 1, "a dimension"))
                    return 0;
                const int dim = DimensionNamed(t[i]);
                if (dim < 0) {
                    fail("Unknown dimension '" + t[i] + "'");
                    return 0;
                }
                for (ExecContext c : contexts) {
                    c.origin.dimension = dim;
                    next.push_back(c);
                }
                i++;
            } else if (sub == "if" || sub == "unless") {
                const bool want = sub == "if";
                if (!need(i, 1, "a condition"))
                    return 0;
                const std::string kind = str::Lower(t[i]);
                i++;
                if (kind == "block") {
                    if (!need(i, 4, "a position and a block"))
                        return 0;
                    const std::string block = BareBlock(t[i + 3]);
                    for (const ExecContext& c : contexts) {
                        double p[3];
                        if (!Coordinates(&t[i], c.origin, false, p, error)) {
                            fail(error);
                            return 0;
                        }
                        const int bx = static_cast<int>(std::floor(p[0])), by = static_cast<int>(std::floor(p[1])),
                                  bz = static_cast<int>(std::floor(p[2]));
                        const std::string here = BlockRegistry::Instance().Name(BlockAt(c.origin.dimension, bx, by, bz));
                        const bool match = BareBlock(here.empty() ? "air" : here) == block;
                        if (match == want)
                            next.push_back(c);
                    }
                    i += 4;
                } else if (kind == "entity") {
                    if (!need(i, 1, "a target"))
                        return 0;
                    for (const ExecContext& c : contexts) {
                        std::vector<Session*> found;
                        std::string ignored;
                        const bool any = select(c, t[i], found, ignored) && !found.empty();
                        if (any == want)
                            next.push_back(c);
                    }
                    i++;
                } else if (kind == "dimension") {
                    if (!need(i, 1, "a dimension"))
                        return 0;
                    const int dim = DimensionNamed(t[i]);
                    for (const ExecContext& c : contexts)
                        if ((c.origin.dimension == dim) == want)
                            next.push_back(c);
                    i++;
                } else {
                    fail("Unsupported condition '" + kind + "' (block, entity and dimension are supported)");
                    return 0;
                }
                conditionOnly = i >= t.size();
            } else {
                fail("Incorrect argument for command: '" + t[i - 1] + "'");
                return 0;
            }
            contexts = std::move(next);
            if (contexts.empty() && !conditionOnly) {
                if (!error.empty())
                    fail(error);
                return 0;
            }
        }
        if (conditionOnly) {
            if (contexts.empty())
                fail("Test failed");
            else
                say(contexts.size() == 1 ? "Test passed" : "Test passed, count: " + std::to_string(contexts.size()));
            return static_cast<int>(contexts.size());
        }
        fail("Expected 'run' at the end of /execute");
        return 0;
    }
}
