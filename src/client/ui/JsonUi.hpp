#pragma once
#include "util/JsonValue.hpp"

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin::ui {
    using Vars = std::unordered_map<std::string, json::Value>;

    class UiLibrary {
    public:
        struct Def {
            const json::Value* json = nullptr;
            std::string key;
            std::string ns;
        };

        void SetRoots(std::vector<std::string> roots);
        bool Scan();
        bool Changed() const;

        const Def* Find(const std::string& ns, const std::string& name);

        const std::unordered_map<std::string, std::vector<std::pair<std::string, const json::Value*>>>*
        Nested(const std::string& ns);
        const Vars& GlobalVariables();
        const std::vector<std::string>& Problems() const { return problems; }
        size_t FileCount() const { return files.size(); }
        size_t NamespaceCount() const { return namespaces.size(); }
        bool Has(const std::string& ns) const { return namespaces.contains(ns); }

        bool DefinedBefore(const std::string& ns, size_t root) const;
        size_t RootCount() const { return roots.size(); }

    private:
        struct File {
            std::string rel;
            std::string path;
            size_t root = 0;
            uint64_t stamp = 0;
            std::string ns;
        };
        struct Namespace {
            std::vector<size_t> files;
            bool loaded = false;
            std::unordered_map<std::string, Def> defs;

            std::unordered_map<std::string, std::vector<std::pair<std::string, const json::Value*>>> nested;
        };

        void LoadNamespace(const std::string& name, Namespace& ns);
        static uint64_t Stamp(const std::string& path);

        std::vector<std::string> roots;
        std::vector<File> files;
        std::unordered_map<std::string, Namespace> namespaces;
        std::deque<json::Value> documents;
        Vars globals;
        bool globalsLoaded = false;
        std::string globalsPath;
        std::vector<std::string> problems;
        std::vector<std::string> watched;
        std::vector<std::pair<std::string, uint64_t>> stamps;
    };

    struct Node {
        std::string name;
        std::string path;
        json::Value props = json::Value::MakeObject();
        std::vector<json::Value> bindings;
        std::vector<std::unique_ptr<Node>> children;
        std::unique_ptr<Node> gridTemplate;
        std::vector<std::pair<std::string, std::unique_ptr<Node>>> factoryControls;
        std::string type;
        std::string ns;
    };

    struct BuildReport {
        size_t nodes = 0;
        std::unordered_set<std::string> missingControls;
        std::unordered_set<std::string> unresolvedVariables;
    };

    std::unique_ptr<Node> BuildTree(UiLibrary& library, const std::string& reference, const Vars& rootVars,
                                    BuildReport& report);

    json::Value Substitute(const json::Value& value, const Vars& vars,
                    std::unordered_set<std::string>* unresolved = nullptr);
}
