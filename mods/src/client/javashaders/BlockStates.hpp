#pragma once
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// java block, entity and item names for what the bedrock client knows, and iris's block.properties matching:
// block entries in file order, then tag entries, the first entry that matches a state wins (iris
// BlockMaterialMapping, putIfAbsent); a predicate on a property the block does not have is ignored
namespace perlin::jshader {
    using StateList = std::vector<std::pair<std::string, std::string>>;

    struct JavaBlock {
        std::string name; // without the minecraft: prefix
        StateList props;  // java property names and values
        const std::string* Find(std::string_view key) const;
    };

    // a bedrock block (name with or without the prefix, its states as strings: bools as "1"/"0" or
    // "true"/"false") as java names it
    JavaBlock ToJavaBlock(std::string_view bedrockName, const StateList& bedrockStates);
    // the encoded form the renderer passes around: "name|key=value|key=value"
    JavaBlock ToJavaBlock(std::string_view encoded);
    std::string EncodeBedrockBlock(std::string_view name, const StateList& states);

    // the java entity / item name for a bedrock one, without the prefix
    std::string JavaEntityName(std::string_view bedrockName);
    std::string JavaItemName(std::string_view bedrockName);

    // the java block tags iris resolves %tag entries against, only those packs list in practice
    bool BlockInTag(std::string_view tag, std::string_view javaName);

    class BlockIdMatcher {
    public:
        // block.<id> lines in file order, each with its entries ("minecraft:lantern:hanging=true", "%leaves")
        void Build(const std::vector<std::pair<int, std::vector<std::string>>>& ordered);
        int Match(const JavaBlock& block) const; // -1 when nothing matches
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

    // entity.properties / item.properties: the last line naming an entry wins (iris parseIdMap)
    std::unordered_map<std::string, int>
    FlatIdMap(const std::vector<std::pair<int, std::vector<std::string>>>& ordered);
}
