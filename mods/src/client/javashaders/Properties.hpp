#pragma once
#include "client/javashaders/Cond.hpp"

#include <map>
#include <string>
#include <vector>

namespace perlin::jshader {
    struct Properties {
        std::vector<std::pair<std::string, std::string>> entries;
        std::map<std::string, std::string> map;

        bool Has(const std::string& key) const { return map.contains(key); }
        std::string Get(const std::string& key, const std::string& fallback = {}) const {
            const auto it = map.find(key);
            return it == map.end() ? fallback : it->second;
        }
        bool Bool(const std::string& key, bool fallback) const;
        double Number(const std::string& key, double fallback) const;
        std::vector<std::pair<std::string, std::string>> WithPrefix(const std::string& prefix) const;
    };

    Properties ParseProperties(const std::string& text, MacroMap macros, bool preprocess = true);
    std::vector<std::string> SplitWords(const std::string& value);
}
