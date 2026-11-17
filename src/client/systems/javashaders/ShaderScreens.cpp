#include "client/systems/javashaders/ShaderScreens.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <functional>
#include <set>

namespace perlin::jshader {
    namespace {
        bool Selectable(const ShaderOption& o) {
            return o.kind == ShaderOption::Kind::Toggle ||
                   (o.kind == ShaderOption::Kind::Value && !o.values.empty());
        }

        bool IsTrue(const std::string& v) {
            return v == "true" || v == "1" || v == "on";
        }

        std::set<std::string> PlacedOptions(const ShaderPackInstance& pack) {
            std::set<std::string> placed;
            for (const auto& [key, value] : pack.RawProps().entries) {
                if (key != "screen" && (!key.starts_with("screen.") || key.ends_with(".columns")))
                    continue;
                for (const std::string& w : SplitWords(value))
                    if (!w.empty() && w.front() != '[' && w.front() != '<' && w != "*")
                        placed.insert(w);
            }
            return placed;
        }
    }

    std::string PackLabel(const std::string& text) {
        return str::Trim(str::StripFormatting(text, str::SectionEncoding::Utf8OrLatin1));
    }

    const ShaderOption* FindOption(const ShaderPackInstance& pack, const std::string& name) {
        for (const ShaderOption& o : pack.Options())
            if (o.name == name)
                return &o;
        return nullptr;
    }

    std::string OptionValue(const ShaderPackInstance& pack, const ShaderOption& o,
                            const OptionValues& current) {
        const auto it = current.find(o.name);
        if (o.kind == ShaderOption::Kind::Toggle) {
            const bool on = it != current.end() ? IsTrue(it->second) : o.defaultOn;
            return on ? "true" : "false";
        }
        (void)pack;
        return it != current.end() ? it->second : o.defaultValue;
    }

    ScreenLayout BuildScreen(const ShaderPackInstance& pack, const std::string& name) {
        ScreenLayout out;
        const std::string key = name.empty() ? "screen" : "screen." + name;
        out.title = name.empty() ? std::string() : ScreenTitle(pack, name);
        std::map<std::string, const ShaderOption*> byName;
        std::vector<const ShaderOption*> ordered;
        for (const ShaderOption& o : pack.Options())
            if (Selectable(o) && !byName.contains(o.name)) {
                byName[o.name] = &o;
                if (o.kind == ShaderOption::Kind::Toggle)
                    ordered.push_back(&o);
            }
        for (const ShaderOption& o : pack.Options())
            if (Selectable(o) && o.kind != ShaderOption::Kind::Toggle && byName[o.name] == &o)
                ordered.push_back(&o);
        std::string layout = pack.RawProps().Get(key);
        if (layout.empty()) {
            if (!name.empty())
                return out;
            layout = "*";
        }
        const std::set<std::string> elsewhere = PlacedOptions(pack);
        std::set<std::string> placed;
        for (const std::string& w : SplitWords(layout)) {
            if (w == "<empty>") {
                out.entries.push_back({ScreenEntry::Kind::Empty, {}});
            } else if (w == "<profile>") {
                if (!Profiles(pack).empty())
                    out.entries.push_back({ScreenEntry::Kind::Profile, {}});
            } else if (w.size() > 2 && w.front() == '[' && w.back() == ']') {
                out.entries.push_back({ScreenEntry::Kind::Screen, w.substr(1, w.size() - 2)});
            } else if (w == "*") {
                for (const ShaderOption* o : ordered)
                    if (!elsewhere.contains(o->name) && placed.insert(o->name).second)
                        out.entries.push_back({ScreenEntry::Kind::Option, o->name});
            } else if (byName.contains(w)) {
                if (placed.insert(w).second)
                    out.entries.push_back({ScreenEntry::Kind::Option, w});
            } else {
                out.entries.push_back({ScreenEntry::Kind::Empty, {}});
            }
        }
        const int fallback = out.entries.size() > 18 ? 3 : 2;
        out.columns = std::clamp(static_cast<int>(pack.RawProps().Number(key + ".columns", fallback)), 1, 6);
        return out;
    }

    bool IsSlider(const ShaderPackInstance& pack, const std::string& option) {
        for (const std::string& w : SplitWords(pack.RawProps().Get("sliders")))
            if (w == option)
                return true;
        return false;
    }

    std::vector<std::pair<std::string, OptionValues>> Profiles(const ShaderPackInstance& pack) {
        std::vector<std::pair<std::string, std::string>> raw;
        for (const auto& [key, value] : pack.RawProps().entries)
            if (key.starts_with("profile.") && key.size() > 8)
                raw.emplace_back(key.substr(8), value);
        std::vector<std::pair<std::string, OptionValues>> out;
        std::function<void(const std::string&, OptionValues&, std::set<std::string>&)> apply =
            [&](const std::string& profile, OptionValues& values, std::set<std::string>& visiting) {
                if (!visiting.insert(profile).second)
                    return;
                std::string text;
                for (const auto& [n, v] : raw)
                    if (n == profile)
                        text = v;
                for (const std::string& w : SplitWords(text)) {
                    if (w.starts_with("profile.")) {
                        apply(w.substr(8), values, visiting);
                        continue;
                    }

                    if (w.starts_with("program.") || w.starts_with("!program."))
                        continue;
                    const size_t sep = w.find_first_of(":=");
                    if (sep != std::string::npos) {
                        values[w.substr(0, sep)] = w.substr(sep + 1);
                    } else if (w.front() == '!') {
                        values[w.substr(1)] = "false";
                    } else {
                        values[w] = "true";
                    }
                }
                visiting.erase(profile);
            };
        std::set<std::string> seen;
        for (const auto& [n, v] : raw) {
            if (!seen.insert(n).second)
                continue;
            OptionValues values;
            std::set<std::string> visiting;
            apply(n, values, visiting);
            out.emplace_back(n, std::move(values));
        }
        return out;
    }

    std::string MatchingProfile(const ShaderPackInstance& pack, const OptionValues& current) {
        for (const auto& [name, values] : Profiles(pack)) {
            bool match = true;
            for (const auto& [opt, v] : values) {
                const ShaderOption* o = FindOption(pack, opt);
                if (!o)
                    continue;
                const std::string now = OptionValue(pack, *o, current);
                match = o->kind == ShaderOption::Kind::Toggle ? IsTrue(now) == IsTrue(v) : now == v;
                if (!match)
                    break;
            }
            if (match)
                return name;
        }
        return {};
    }

    std::string OptionLabel(const ShaderPackInstance& pack, const std::string& option) {
        const std::string l = PackLabel(pack.Lang("option." + option));
        return l.empty() ? option : l;
    }

    std::string OptionComment(const ShaderPackInstance& pack, const ShaderOption& option) {
        const std::string l = PackLabel(pack.Lang("option." + option.name + ".comment"));
        return l.empty() ? PackLabel(option.comment) : l;
    }

    std::string ValueLabel(const ShaderPackInstance& pack, const ShaderOption& option,
                           const std::string& value) {
        const std::string l = PackLabel(pack.Lang("value." + option.name + "." + value));
        if (!l.empty())
            return l;
        if (option.kind == ShaderOption::Kind::Toggle)
            return IsTrue(value) ? "ON" : "OFF";
        return PackLabel(pack.Lang("prefix." + option.name)) + value +
               PackLabel(pack.Lang("suffix." + option.name));
    }

    std::string ScreenTitle(const ShaderPackInstance& pack, const std::string& screen) {
        const std::string l = PackLabel(pack.Lang("screen." + screen));
        return l.empty() ? screen : l;
    }

    std::string ProfileLabel(const ShaderPackInstance& pack, const std::string& profile) {
        if (profile.empty())
            return "Custom";
        const std::string l = PackLabel(pack.Lang("profile." + profile));
        return l.empty() ? profile : l;
    }
}
