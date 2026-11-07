#include "world/level/lighting/LightRegistry.hpp"

#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "util/Json.hpp"
#include "util/Log.hpp"

namespace perlin {
    namespace {
        std::string StateText(uint16_t id, const std::string& key) {
            for (const auto& [name, value] : BlockPalette::Instance().States(id)) {
                if (name != key)
                    continue;
                switch (value.kind) {
                case BlockPalette::StateValue::Bool:
                    return value.i ? "true" : "false";
                case BlockPalette::StateValue::Int:
                    return std::to_string(value.i);
                default:
                    return value.s;
                }
            }
            return {};
        }

        int Clamp(int v) {
            return std::clamp(v, 0, kMaxLight);
        }

        int FromComponent(json::View v, bool unit) {
            if (!v.isNumber())
                return -1;
            const double d = v.number();
            return Clamp(static_cast<int>(unit ? d * kMaxLight + 0.5 : d + 0.5));
        }
    }

    LightRegistry& LightRegistry::Instance() {
        static LightRegistry registry;
        return registry;
    }

    LightRegistry::LightRegistry() {
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
        for (auto& slot : occlusion)
            slot.store(0, std::memory_order_relaxed);
    }

    void LightRegistry::SetOcclusion(OcclusionFn fn) {
        std::unique_lock lock(mutex);
        occlusionFn = std::move(fn);
        for (auto& slot : occlusion)
            slot.store(0, std::memory_order_relaxed);
    }

    const FaceOcclusion* LightRegistry::Occlusion(uint16_t id) const {
        uintptr_t v = occlusion[id].load(std::memory_order_acquire);
        if (v == 1)
            return nullptr;
        if (v > 1)
            return reinterpret_cast<const FaceOcclusion*>(v);
        OcclusionFn fn;
        {
            std::shared_lock lock(mutex);
            fn = occlusionFn;
        }
        if (!fn || id == 0)
            return nullptr;
        auto* fresh = new FaceOcclusion();
        if (!fn(id, *fresh)) {
            delete fresh;
            occlusion[id].store(1, std::memory_order_release);
            return nullptr;
        }
        occlusion[id].store(reinterpret_cast<uintptr_t>(fresh), std::memory_order_release);
        return fresh;
    }

    void LightRegistry::Invalidate() {
        std::unique_lock lock(mutex);
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
        for (auto& slot : occlusion)
            slot.store(0, std::memory_order_relaxed);
    }

    void LightRegistry::SetCustom(const std::string& name, int emission, int opacity) {
        std::unique_lock lock(mutex);
        if (emission >= 0)
            customEmission[name] = emission;
        if (opacity >= 0)
            customOpacity[name] = opacity;
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
    }

    void LightRegistry::ClearCustom() {
        std::unique_lock lock(mutex);
        customEmission.clear();
        customOpacity.clear();
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
    }

    void LightRegistry::SetDerivedOpacity(OpacityFn fn) {
        std::unique_lock lock(mutex);
        derivedOpacity = std::move(fn);
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
    }

    bool LightRegistry::HasRules() const {
        std::shared_lock lock(mutex);
        return rulesLoaded;
    }

    void LightRegistry::ParseRules(json::View section, std::vector<Rule>& rules,
                                   std::unordered_map<std::string, size_t>& exact) {
        for (const auto [key, value] : section.members()) {
            Rule rule;
            std::string pattern = key;
            const bool front = !pattern.empty() && pattern.front() == '*';
            const bool back = pattern.size() > 1 && pattern.back() == '*';
            if (front)
                pattern.erase(0, 1);
            if (back)
                pattern.pop_back();
            rule.pattern = pattern;
            rule.match = front && back ? Rule::Match::Contains
                         : front       ? Rule::Match::Suffix
                         : back        ? Rule::Match::Prefix
                                       : Rule::Match::Exact;
            if (value.isNumber()) {
                rule.value = Clamp(value.integer());
            } else if (value.isObject()) {
                rule.state = value["state"].str();
                rule.linear = value["linear"].boolean();
                rule.fallback = Clamp(value["default"].integer());
                for (const auto [k, v] : value["map"].members())
                    if (v.isNumber())
                        rule.map[k] = Clamp(v.integer());
                for (const auto [k, v] : value["requires"].members())
                    rule.conditions.emplace_back(k, v.text());
            } else {
                continue;
            }
            if (rule.match == Rule::Match::Exact)
                exact[rule.pattern] = rules.size();
            rules.push_back(std::move(rule));
        }
    }

    void LightRegistry::LoadRules(const std::string& jsonText) {
        const auto document = json::Document::Parse(jsonText, true);
        if (!document || !document->root().isObject()) {
            Log("light", "light.json is not valid JSON");
            return;
        }
        const json::View root = document->root();
        std::vector<Rule> emission, opacity;
        std::unordered_map<std::string, size_t> emissionIndex, opacityIndex;
        ParseRules(root["emission"], emission, emissionIndex);
        ParseRules(root["opacity"], opacity, opacityIndex);

        std::unique_lock lock(mutex);
        emissionRules = std::move(emission);
        opacityRules = std::move(opacity);
        emissionExact = std::move(emissionIndex);
        opacityExact = std::move(opacityIndex);
        rulesLoaded = true;
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
    }

    void LightRegistry::LoadBehaviorPack(const std::string& blocksDir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(blocksDir, ec))
            return;

        std::unordered_map<std::string, int> emission, opacity;
        for (const auto& entry : fs::recursive_directory_iterator(blocksDir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            std::ifstream file(entry.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const auto document = json::Document::Parse(text, true);
            if (!document)
                continue;
            const json::View block = document->root()["minecraft:block"];
            const json::View identifierNode = block["description"]["identifier"];
            if (!identifierNode.isString())
                continue;
            const std::string identifier = identifierNode.str();

            auto read = [&](json::View components) {
                if (!components.isObject())
                    return;
                if (const int v = FromComponent(components["minecraft:light_emission"], false); v >= 0)
                    emission[identifier] = std::max(emission[identifier], v);
                if (const int v = FromComponent(components["minecraft:block_light_emission"], true); v >= 0)
                    emission[identifier] = std::max(emission[identifier], v);
                if (const int v = FromComponent(components["minecraft:light_dampening"], false); v >= 0)
                    opacity[identifier] = v;
                if (const int v = FromComponent(components["minecraft:block_light_filter"], false); v >= 0)
                    opacity[identifier] = v;
            };
            read(block["components"]);
            for (const json::View permutation : block["permutations"].items())
                read(permutation["components"]);
        }

        std::unique_lock lock(mutex);
        packEmission = std::move(emission);
        packOpacity = std::move(opacity);
        for (auto& slot : memo)
            slot.store(kUnresolved, std::memory_order_relaxed);
        if (!packEmission.empty() || !packOpacity.empty())
            Log("light", "behavior pack: {} emitters, {} opacity overrides", packEmission.size(),
                packOpacity.size());
    }

    bool LightRegistry::Matches(const Rule& rule, const std::string& name) {
        switch (rule.match) {
        case Rule::Match::Exact:
            return name == rule.pattern;
        case Rule::Match::Prefix:
            return name.compare(0, rule.pattern.size(), rule.pattern) == 0;
        case Rule::Match::Suffix:
            return name.size() >= rule.pattern.size() &&
                   name.compare(name.size() - rule.pattern.size(), rule.pattern.size(), rule.pattern) == 0;
        case Rule::Match::Contains:
            return name.contains(rule.pattern);
        }
        return false;
    }

    int LightRegistry::Evaluate(const Rule& rule, uint16_t id) {
        if (rule.value >= 0)
            return rule.value;
        for (const auto& [key, expected] : rule.conditions)
            if (StateText(id, key) != expected)
                return rule.fallback;
        if (rule.state.empty())
            return rule.fallback;
        const std::string text = StateText(id, rule.state);
        if (rule.linear)
            return text.empty() ? rule.fallback : Clamp(std::atoi(text.c_str()));
        const auto it = rule.map.find(text);
        return it == rule.map.end() ? rule.fallback : it->second;
    }

    int LightRegistry::RuleValue(const std::vector<Rule>& rules,
                                 const std::unordered_map<std::string, size_t>& exact,
                                 const std::string& name, uint16_t id) const {
        if (const auto it = exact.find(name); it != exact.end())
            return Evaluate(rules[it->second], id);
        for (const Rule& rule : rules)
            if (rule.match != Rule::Match::Exact && Matches(rule, name))
                return Evaluate(rule, id);
        return -1;
    }

    BlockLightProps LightRegistry::Compute(uint16_t id) const {
        BlockLightProps props;
        if (id == 0) {
            memo[id].store(0, std::memory_order_relaxed);
            return props;
        }

        const std::string name = BlockPalette::Instance().Name(id);
        std::shared_lock lock(mutex);

        int emission = -1, opacity = -1;
        if (const auto it = packEmission.find(name); it != packEmission.end())
            emission = it->second;
        if (const auto it = packOpacity.find(name); it != packOpacity.end())
            opacity = it->second;
        if (const auto it = customEmission.find(name); it != customEmission.end())
            emission = it->second;
        if (const auto it = customOpacity.find(name); it != customOpacity.end())
            opacity = it->second;
        if (emission < 0)
            emission = RuleValue(emissionRules, emissionExact, name, id);
        if (opacity < 0)
            opacity = RuleValue(opacityRules, opacityExact, name, id);

        if (opacity < 0 && derivedOpacity)
            opacity = derivedOpacity(id);

        props.emission = static_cast<uint8_t>(std::max(emission, 0));
        props.opacity = static_cast<uint8_t>(opacity < 0 ? kMaxLight : Clamp(opacity));

        if (rulesLoaded && derivedOpacity)
            memo[id].store(static_cast<uint16_t>((props.emission << 8) | props.opacity),
                           std::memory_order_relaxed);
        return props;
    }
}
