#include "client/javashaders/Properties.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace perlin::jshader {
    bool Properties::Bool(const std::string& key, bool fallback) const {
        const auto it = map.find(key);
        if (it == map.end())
            return fallback;
        return it->second == "true" || it->second == "1";
    }

    double Properties::Number(const std::string& key, double fallback) const {
        const auto it = map.find(key);
        if (it == map.end() || it->second.empty())
            return fallback;
        char* end = nullptr;
        const double v = std::strtod(it->second.c_str(), &end);
        return end == it->second.c_str() ? fallback : v;
    }

    std::vector<std::pair<std::string, std::string>> Properties::WithPrefix(const std::string& prefix) const {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& e : entries)
            if (e.first.compare(0, prefix.size(), prefix) == 0)
                out.emplace_back(e.first.substr(prefix.size()), e.second);
        return out;
    }

    std::vector<std::string> SplitWords(const std::string& value) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : value) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!cur.empty()) {
                    out.push_back(cur);
                    cur.clear();
                }
            } else {
                cur += c;
            }
        }
        if (!cur.empty())
            out.push_back(cur);
        return out;
    }

    Properties ParseProperties(const std::string& text, MacroMap macros, bool preprocess) {
        const std::string source = preprocess ? ResolveConditionals(text, macros) : text;
        Properties props;
        std::istringstream in(source);
        std::string line;
        std::string joined;
        auto commit = [&](std::string entry) {
            size_t a = 0;
            while (a < entry.size() && std::isspace(static_cast<unsigned char>(entry[a])))
                a++;
            if (a >= entry.size() || entry[a] == '#' || entry[a] == '!')
                return;
            size_t eq = entry.find_first_of("=:", a);
            size_t sp = entry.find_first_of(" \t", a);
            size_t sep = std::min(eq == std::string::npos ? entry.size() : eq,
                                  sp == std::string::npos ? entry.size() : sp);
            std::string key = entry.substr(a, sep - a);
            std::string value;
            if (sep < entry.size()) {
                size_t v = sep;
                while (v < entry.size() && std::isspace(static_cast<unsigned char>(entry[v])))
                    v++;
                if (v < entry.size() && (entry[v] == '=' || entry[v] == ':'))
                    v++;
                while (v < entry.size() && std::isspace(static_cast<unsigned char>(entry[v])))
                    v++;
                value = entry.substr(v);
            }
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
            if (key.empty())
                return;
            props.entries.emplace_back(key, value);
            props.map[key] = value;
        };
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            size_t b = line.find_first_not_of(" \t");
            if (joined.empty() && (b == std::string::npos || line[b] == '#' || line[b] == '!'))
                continue;
            if (!line.empty() && line.back() == '\\') {
                line.pop_back();
                size_t lead = joined.empty() ? 0 : line.find_first_not_of(" \t");
                joined += lead == std::string::npos ? std::string() : line.substr(lead);
                joined += ' ';
                continue;
            }
            size_t lead = joined.empty() ? 0 : line.find_first_not_of(" \t");
            joined += lead == std::string::npos ? std::string() : line.substr(lead);
            commit(joined);
            joined.clear();
        }
        if (!joined.empty())
            commit(joined);
        return props;
    }
}
