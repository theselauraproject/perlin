#pragma once
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace perlin::jshader {
    using StateList = std::vector<std::pair<std::string, std::string>>;

    struct JavaBlock {
        std::string name;
        StateList props;
        const std::string* Find(std::string_view key) const;
    };

    JavaBlock ToJavaBlock(std::string_view bedrockName, const StateList& bedrockStates);

    JavaBlock ToJavaBlock(std::string_view encoded);
    std::string EncodeBedrockBlock(std::string_view name, const StateList& states);

    std::string JavaEntityName(std::string_view bedrockName);
    std::string JavaItemName(std::string_view bedrockName);

    bool BlockInTag(std::string_view tag, std::string_view javaName);

    class BlockIdMatcher {
    public:

        void Build(const std::vector<std::pair<int, std::vector<std::string>>>& ordered);
        int Match(const JavaBlock& block) const;
        int MatchName(std::string_view javaName) const;
        bool Empty() const { return byName.empty() && tags.empty(); }

    private:
        struct Rule {
            std::map<std::string, std::string> predicates;
            int id = -1;
            size_t order = 0;
        };
        struct TagRule {
            std::string tag;
            std::map<std::string, std::string> predicates;
            int id = -1;
        };
        std::unordered_map<std::string, std::vector<Rule>> byName;
        std::vector<TagRule> tags;
        static bool Matches(const std::map<std::string, std::string>& predicates, const JavaBlock& block);
    };

    std::unordered_map<std::string, int>
    FlatIdMap(const std::vector<std::pair<int, std::vector<std::string>>>& ordered);
}
